/*
-----------------------------------------------------------------------------
    procstat
    Telegraf module to get the CPU, Memory and I/O usage of the running processes

    Author: CostisC
-----------------------------------------------------------------------------
*/

#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <chrono>

#include "MonPID.h"
#include "CpuUsage.h"

#define K 1000
#define M (K*K)

using namespace std;

static volatile sig_atomic_t newPoll = 0;

template<typename T> using pComparator = bool (*)(const T&, const T&);

namespace {
    // Sort a vector, according to the comparator function and return the first N entries
    template <typename T>
    vector<T> sort(vector<T>& t, pComparator<T> pC, ushort N=5)
    {
        ::sort(t.begin(), t.end(), pC);

        ushort size = (t.size() < N)? t.size() : N;
        vector<T> output(size,0);
        for (short i=0; i<size; i++) {
            output[i] = t[i];
        }
        return output;
    }
}

class Measurements {

    typedef unordered_map<pid_t, MonPID>  mProcesses;
    typedef mProcesses::iterator  mProcesses_iter;
    typedef unordered_set<string> strSet;
    typedef OVLValue monPidAccessor() const;
    using SteadyClock = std::chrono::steady_clock;
    using TimePoint = std::chrono::time_point<SteadyClock>;


    mProcesses map_processes;
    unsigned CPU_jiffies = 1;
    short nCores = 1;
    strSet sDuplicateProcs, sIncludeProcs;
    unordered_map<string, vector<int>> mRenameProcs;
    mProcesses mFinalProcHolder;
    unordered_map<pid_t, unordered_map<string, ushort>> mRanksTracker;

    TimePoint timepoint = SteadyClock::now();

    ushort bucket_size = 5;
    bool aggregate = false;
    float minCPU = 3.0;
    float minRSS = 2.0e+7;  // 20 MB
    float minIObytes = 5.0e+6; // 5 MB/s
    float minIOdelays = 300.0e+6; // 300 msec/s


	void removeSpaces(string& strInput)
	{
	    char* str = const_cast<char*>(strInput.c_str());

	    int count = 0;

	    for (int i = 0; str[i]; i++)
	        if (str[i] != ' ')
	            str[count++] = str[i];
	    str[count] = '\0';

	    strInput = str;
	}



	void init_process_name()
    {
        mRenameProcs.clear();
        mFinalProcHolder.clear();
        mRanksTracker.clear();
    }


    // Get the top consumers while filtering out, based on the minimum threasholds
    void top_consumers(float threshold, monPidAccessor MonPID::*accessor, pComparator<MonPID> pC,
        vector<MonPID> &vProcsToSort, const string rank_label)
    {
        vector<MonPID> vTemp = sort(vProcsToSort, pC, bucket_size);

        for (ushort i=0; i<vTemp.size(); i++) {
            if ((vTemp[i].*accessor)() <= threshold)
                break;
            else
            {
                pid_t pid = vTemp[i].get_pid();
                mFinalProcHolder[pid] = vTemp[i];
                mRanksTracker[pid][rank_label] = i+1;
            }
        }
    }

    // rename multiple processes by appending an index to their names
    string process_name(const string pname, const int pid)
    {
        // track the processes per pid
        auto it = mRenameProcs.find(pname);
        // processes by this name  not yet recorded
        if (it == mRenameProcs.end()) {
            mRenameProcs.insert(pair<string, vector<int>> (pname, {pid}));
            return pname;
        } else {
            vector<int>& vPids = it->second;
            auto vIter = find(vPids.begin(), vPids.end(), pid);
            if (vIter == vPids.end()) {     // this process is not yet recorded
                vPids.push_back(pid);
                vIter = vPids.end() - 1;
            }
            int index = distance(vPids.begin(), vIter);
            if (index == 0) {
                return pname;
            } else {
                ostringstream newProcessName;
                newProcessName << pname << "_" << index;
                return newProcessName.str();
            }
        }
    }

public:

    Measurements() {
        nCores = sysconf(_SC_NPROCESSORS_ONLN);
        if (nCores == 0) {
            throw "Unable to get number of CPU cores";
        }

    }

    void set_bucket_size(ushort N) { bucket_size = N; }
    void set_aggregate() { aggregate = true; }
    void set_minCPU(float thr) { minCPU = thr; }
    void set_minRSS(float thr) { minRSS = thr; }
    void set_minIObytes(float thr) { minIObytes = thr; }
    void set_minIOdelays(float thr) { minIOdelays= thr; }

	void set_includeProcs(string str) {

	    string field;

	    removeSpaces(str);
		// remove bracket enclosure, if there
	    string::size_type start_pos, end_pos;
	    if ((start_pos = str.find_first_of('[')) == string::npos)
	        start_pos = -1;
	    if ((end_pos = str.find_last_of(']')) == string::npos)
	        end_pos = str.length();

	    start_pos++;

	    str = str.substr(start_pos, end_pos-start_pos);
	    stringstream ssInput(str);


	    while ( getline(ssInput, field, ',') ){
        	sIncludeProcs.insert(field);
	    }
	}

    bool scan_all_processes()
    {

        // Read all entries in /proc -- skipping those that don't start with 1..9
        DIR* procdir = opendir ("/proc");
        if (procdir == NULL)
        {
            OvlError("Failed to read '/proc', errno %d: %s",
                errno, strerror(errno));
            return false;
        }

        // Mark all the monitored entries 'not found' so we'll know which ones
        // to remove at the end.
        for (auto& it : map_processes) {
            it.second.set_found(false);
        }

        if (aggregate) sDuplicateProcs.clear();


        // Scan all PID (numeric) subdirectories of /proc
        // Add each PID found to map_processes, updating any previously found entries
        struct dirent *entry;
        char *endptr = NULL;
        strSet proc_names;
        pair<mProcesses_iter, bool>  insert_iter;

        for (entry = readdir (procdir); entry != NULL; entry = readdir (procdir))
        {
            pid_t pid = strtol(entry->d_name, &endptr, 10);
            // Skip non-numeric entries
            if (*endptr != '\0')
                continue;

            auto it = map_processes.find(pid);
            if (it == map_processes.end()) {
                pair<pid_t, MonPID> newElement (pid, MonPID(pid));

                // a very short-lived process may have been scanned, but ended before it got read
                if (!newElement.second.isfound())
                    continue;

                insert_iter = map_processes.insert(newElement);
                if (insert_iter.second)
                    it = insert_iter.first;
                else {
                    OvlError("Failed to insert new process '%s'", newElement.second.get_name().c_str());
                    continue;
                }

                #ifdef DEBUG
                OvlInfo("New process:\t %u (%s)\n",
                    pid, insert_iter.first->second.get_name().c_str());
                #endif //DEBUG
            } else {
                it->second.update();
                //it->second.trace();
            }

            // track duplicate instances of executables
            string name = it->second.get_name();
            auto v_it = proc_names.find(name);
            if (v_it == proc_names.end())
                proc_names.insert(name);
            else if(aggregate)
                sDuplicateProcs.insert(name);
        }

        if (closedir(procdir))
            OvlError("Failed to close '/proc' dir, errno %d: %s",
                errno, strerror(errno));

        // Clean up all previously found processes, which however are no longer running
        auto it = map_processes.begin();
        while (it != map_processes.end()) {
            if (! it->second.isfound()) {
                #ifdef DEBUG
                OvlInfo("Removed process: %u (%s)\n", it->first, it->second.get_name().c_str());
                #endif //DEBUG
                it = map_processes.erase(it);
            } else
                ++it;
        }

        return true;
    }

    bool getCPU()
    {
        return ::getCPU(&CPU_jiffies);
    }


    void output_top_processes()
    {
		init_process_name();
        vector<MonPID> vProcsToSort, vProcsInclude;
        unordered_map<string, MonPID> mDuplProc;

        // Track the elapsed time since the last sampling
        const TimePoint time_sample = SteadyClock::now();
        int seconds_lapse =  chrono::duration_cast<chrono::seconds>(time_sample - timepoint).count();
        if (seconds_lapse == 0) seconds_lapse = 1;
        timepoint = time_sample;

        for (const auto& it : map_processes) {
            // Compose a vector out of the map of running processes, for their sorting
            // Keep the 'include procs' in a separate vector, to add them in the end
            string name = it.second.get_name();
            if (sDuplicateProcs.find(name) == sDuplicateProcs.end())
            {
                if (sIncludeProcs.find(name) == sIncludeProcs.end())
                    vProcsToSort.push_back(it.second);
                else
                    vProcsInclude.push_back(it.second);
            } else {
                // aggregate instances of the same exec, if that was opted for
                auto m_it = mDuplProc.find(name);
                if (m_it == mDuplProc.end())
                    mDuplProc.insert(make_pair(name, it.second));
                else
                    m_it->second += it.second;
            }
        }

        // append the aggregate measurements
        for (const auto& it : mDuplProc) {
            string name = it.second.get_name();
            if (sIncludeProcs.find(name) == sIncludeProcs.end())
                vProcsToSort.push_back(it.second);
            else
                vProcsInclude.push_back(it.second);
        }


        // get the top consumers...
        // ... of CPU usage
        top_consumers(minCPU*CPU_jiffies/100/nCores,
            &MonPID::get_cpu,
            compare_by_CPU,
            vProcsToSort,
            "cpu_usage_topk_rank");

        // ... of Memory
        top_consumers(minRSS,
            &MonPID::get_RSS,
            compare_by_RSS,
            vProcsToSort,
            "memory_rss_topk_rank");

        // ... of read bytes
        top_consumers(minIObytes*seconds_lapse,
            &MonPID::get_read_bytes_delta,
            compare_by_IO_Read_Bytes,
            vProcsToSort,
            "read_bytes_topk_rank");

        // ... of written bytes
        top_consumers(minIObytes*seconds_lapse,
            &MonPID::get_write_bytes_delta,
            compare_by_IO_Write_Bytes,
            vProcsToSort,
            "write_bytes_topk_rank");

        // ... of block I/O delays
        top_consumers(minIOdelays*seconds_lapse,
            &MonPID::get_blkio_delay_delta,
            compare_by_blkio_delay,
            vProcsToSort,
            "blkio_delay_topk_rank");

        // ... of swap-in delays
        top_consumers(minIOdelays*seconds_lapse,
            &MonPID::get_swapin_delay_delta,
            compare_by_swapin_delay,
            vProcsToSort,
            "swapin_delay_topk_rank");

        // ... of cpu delays
        top_consumers(minIOdelays*seconds_lapse,
            &MonPID::get_cpu_delay_delta,
            compare_by_cpu_delay,
            vProcsToSort,
            "cpu_delay_topk_rank");

        // Finally include the explicitly monitored processes; rank them with a fictional 99th order
        for (const auto& it : vProcsInclude) {
            pid_t pid = it.get_pid();
            mFinalProcHolder[pid] = it;
            mRanksTracker[pid]["cpu_usage_topk_rank"]       = 99;
            mRanksTracker[pid]["memory_rss_topk_rank"]      = 99;
            mRanksTracker[pid]["read_bytes_topk_rank"]      = 99;
            mRanksTracker[pid]["write_bytes_topk_rank"]     = 99;
            mRanksTracker[pid]["blkio_delay_topk_rank"]     = 99;
            mRanksTracker[pid]["swapin_delay_topk_rank"]    = 99;
            mRanksTracker[pid]["cpu_delay_topk_rank"]       = 99;
        }


        // the Line Protocol output
        for (const auto& it : mFinalProcHolder) {

            float cpu_usage = 100*nCores * it.second.get_cpu()/(float) CPU_jiffies;

            ostringstream strRanks;
            for (const auto& iit : mRanksTracker[it.first])
                strRanks << "," << iit.first << "=" << iit.second << "i";

			string name = process_name(it.second.get_name(), it.first);

            //it.second.trace();

            cout << "procstat,process_name=" << name <<
                " cpu_usage="       << cpu_usage                                          <<
                ",memory_rss="      << it.second.get_RSS()                                << 'i' <<
                ",read_bytes="      << it.second.get_read_bytes_delta()/seconds_lapse     << 'i' <<
                ",write_bytes="     << it.second.get_write_bytes_delta()/seconds_lapse    << 'i' <<
                ",cpu_delay="       << it.second.get_cpu_delay_delta()/M/seconds_lapse    << 'i' <<
                ",blkio_delay="     << it.second.get_blkio_delay_delta()/M/seconds_lapse  << 'i' <<
                ",swapin_delay="    << it.second.get_swapin_delay_delta()/M/seconds_lapse << 'i' <<
                strRanks.str() << endl;

        }

        #ifdef DEBUG
            cout << endl;
        #endif

    }

};



static void sig_handler(int sig)
{
    newPoll = 1;
}

inline string parseEnv(const char* var)
{
	char* s = getenv(var);
    if (s) return s;
    return "";
}

void getEnvVars(Measurements& measurements) {

    string var;
    var = parseEnv("bucket_size");
    if (!var.empty())
        measurements.set_bucket_size(stoi(var));

    var = parseEnv("aggregate");
    if (var == "true" || var == "True")
        measurements.set_aggregate();

    var = parseEnv("minCPU");
    if (!var.empty())
        measurements.set_minCPU(stof(var));

    var = parseEnv("minRSS");           // in MB
    if (!var.empty())
        measurements.set_minRSS(stoi(var)*M);

    var = parseEnv("minIObytes");       // in KB
    if (!var.empty())
        measurements.set_minIObytes(stoi(var)*K);

    var = parseEnv("minIOdelays");      // in msec
    if (!var.empty())
        measurements.set_minIOdelays(stoi(var)*M);

    var = parseEnv("includeProcs");
    if (!var.empty())
        measurements.set_includeProcs(var);
}



// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ########### MAIN ############
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

int main() {


    // Establish signal handling for SIGUSR1
    if (signal(SIGUSR1, sig_handler) == SIG_ERR) {
        OvlError("Failed to establish signal handler\n");
        return 2;
    }


    try {
        Measurements measurements;
        getEnvVars(measurements);

        while (1) {
    #ifndef DEBUG
            pause();
            if (newPoll) {
    #endif
                if (measurements.scan_all_processes() && measurements.getCPU())
                    measurements.output_top_processes();

                newPoll = 0;
    #ifndef DEBUG
            } else
                break;
    #else
            sleep(3);
    #endif
        }

    }
    catch (const char* e) {
        OvlError("%s\n", e);
        return 1;
    }

    return 0;
}
