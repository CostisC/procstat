
#include <string.h>
#include <sstream>
#include <string>
#include <dirent.h>
#include <stdlib.h>

#include "MonPID.h"
#include "taskstats.h"

#define PROC_STAT            "/proc/%u/stat"
#define PROC_STAT_SIZE       sizeof(PROC_STAT) + 6

#define PROC_STATUS          "/proc/%u/status"
#define PROC_STATUS_SIZE     sizeof(PROC_STATUS) + 6

#define PROC_TASK            "/proc/%u/task"
#define PROC_TASK_SIZE       sizeof(PROC_TASK) + 6

#define VMRSS   "VmRSS:"

using namespace std;


MonPID::MonPID (pid_t pid_val)
    : pid (pid_val)
    , cpu_total(0)
    , cpu_delta(0)
    , vmRSS(0)
    , found (false)
    , initial_sample(true)
{
    if (!skip_taskstat) {
        if (!taskstat::is_socket_alive())
            if (taskstat::nl_init() == CRITICAL_FAIL)
                skip_taskstat = true;
    }

    if (pid_val != 0)
        if(update())
            found = true;
}

bool MonPID::skip_taskstat = false;

int MonPID::fetch_taskstats(pid_t pid, taskstats *ts) {
    char task_dir[PROC_TASK_SIZE];

    snprintf(task_dir, PROC_TASK_SIZE, PROC_TASK, unsigned(pid));
    DIR *taskdir = opendir(task_dir);
    if (taskdir == NULL) {
        OvlError("Failed to open '%s (process: %s)', errno %d: %s",
                 task_dir, name.c_str(),
                 errno, strerror(errno));
        return FAIL;
    }

    // Read the directories in the task directory, which represent the thread IDs of that PID
    // We are interested in the per-PID metrics, so we sum the per-TID metrics
    struct dirent* entry;
    char *endptr = NULL;
    int rc;
    while ((entry = readdir(taskdir)))
    {
        pid_t tid = strtol(entry->d_name, &endptr, 10);
        // Skip non-numeric entries
        if (*endptr != '\0')
            continue;

        taskstats temp_ts;
        if ((rc = taskstat::nl_taskstats_info(tid, &temp_ts)) != SUCCESS)
            break;
        #define MEMBR_ADD(X)    ts->X += temp_ts.X;
        MEMBR_ADD(read_bytes)
        MEMBR_ADD(write_bytes)
        MEMBR_ADD(blkio_delay_total)
        MEMBR_ADD(cpu_delay_total)
        MEMBR_ADD(swapin_delay_total)
        #undef MEMBR_ADD
    }

    if (closedir(taskdir))
        OvlError("Failed to close '%s' dir, errno %d: %s",
                 task_dir, errno, strerror(errno));
    return rc;

}

bool MonPID::update ()
{
    char pps_name[PROC_STAT_SIZE];

    snprintf (pps_name, PROC_STAT_SIZE, PROC_STAT, unsigned (pid));
    ProcFileData statfile (pps_name);
    if (! statfile.refresh ())
        return false;

    // Get the process's name (the name between the parentheses)
    char* lp_pos = strchr (const_cast<char*>(statfile.data ()), '(');
    char* rp_pos = strrchr (const_cast<char*>(statfile.data ()), ')');
    if (lp_pos == NULL || rp_pos == NULL)
    {   // Oops!! the Kernel made a mistake??
        OvlError ("Parse error on %s: '%s'",
                  statfile.path (), statfile.data ());
        return false;
    }

    if (name.empty ())
        name = string (lp_pos + 1, rp_pos - lp_pos - 1);

    // We're updating, this entry is found
    found = true;

    // Make a string stream out of the statistics string
    istringstream is (rp_pos + 1);

    // Skip the next 12 (more) strings (status values)
    string skip;
    for (int n = 1; n < 12; ++n)
        is >> skip;

    // Read the user-land and kernel-space jiffies
    OVLValue utime = 0;
    OVLValue stime = 0;
    is >> utime >> stime;
    OVLValue new_total = utime + stime;

    // Update cpu with the new latest data
    if (initial_sample)
        cpu_total = new_total;

    cpu_delta = new_total - cpu_total;
    cpu_total = new_total;


    // Update the VM
    char ppsus_name[PROC_STATUS_SIZE];
    snprintf (ppsus_name, PROC_STATUS_SIZE, PROC_STATUS, unsigned (pid));
    ProcFileData statusfile (ppsus_name);
    if (statusfile.refresh ())
    {
        OVLValue new_data = statusfile.get_value (VMRSS);
        // Convert from KiB to bytes
        vmRSS = new_data << 10;
    }

    // Update the I/O metrics, from taskstats
    if (!skip_taskstat) {
        int rc;
        taskstats ts;
        memset(&ts, 0, sizeof (taskstats));
        if ((rc = fetch_taskstats(pid, &ts)) == CRITICAL_FAIL) {
            OvlWarn("Taskstats fetch failed. Try to re-establish the netlink connection");
            if ((rc = taskstat::nl_init()) == CRITICAL_FAIL) {
                skip_taskstat = true;
                OvlError("Something is wrong with the taskstats netlink connection. I/O metrics will be excluded");

            } else if ((rc = fetch_taskstats(pid, &ts)) == CRITICAL_FAIL) {
                skip_taskstat = true;
                OvlError("Second attempt to reconnect to netlink failed. I/O metrics will be excluded");
            }
        }
        if (rc == SUCCESS) {
            if (initial_sample) {
                read_bytes          = ts.read_bytes;
                write_bytes         = ts.write_bytes;
                blkio_delay_total   = ts.blkio_delay_total;
                swapin_delay_total  = ts.swapin_delay_total;
                cpu_delay_total     = ts.cpu_delay_total;

            }
            read_bytes_delta    = llabs(ts.read_bytes - read_bytes);
            read_bytes          = ts.read_bytes;
            write_bytes_delta   = llabs(ts.write_bytes - write_bytes);
            write_bytes         = ts.write_bytes;
            blkio_delay_delta   = llabs(ts.blkio_delay_total - blkio_delay_total);
            blkio_delay_total   = ts.blkio_delay_total;
            swapin_delay_delta  = llabs(ts.swapin_delay_total - swapin_delay_total);
            swapin_delay_total  = ts.swapin_delay_total;
            cpu_delay_delta     = llabs(ts.cpu_delay_total - cpu_delay_total);
            cpu_delay_total     = ts.cpu_delay_total;
        } else
            return false;
    }
    initial_sample = false;
    return true;
}

MonPID& MonPID::operator+=(const MonPID& right)
{
    if (this == &right) return *this;
    if (name.empty()) name = right.name;

    #define MEMBR_ADD(X)    X  += right.X;
    MEMBR_ADD(cpu_total)
    MEMBR_ADD(cpu_delta)
    MEMBR_ADD(vmRSS)
    MEMBR_ADD(read_bytes)
    MEMBR_ADD(read_bytes_delta)
    MEMBR_ADD(write_bytes)
    MEMBR_ADD(write_bytes_delta)
    MEMBR_ADD(blkio_delay_total)
    MEMBR_ADD(blkio_delay_delta)
    MEMBR_ADD(swapin_delay_total)
    MEMBR_ADD(swapin_delay_delta)
    MEMBR_ADD(cpu_delay_total)
    MEMBR_ADD(cpu_delay_delta)
    #undef MEMBR_ADD

    return *this;
}


bool compare_by_CPU(const MonPID& a, const MonPID& b)
{
    return a.cpu_delta > b.cpu_delta;
}

bool compare_by_RSS(const MonPID& a, const MonPID& b)
{
    return a.vmRSS > b.vmRSS;
}

bool compare_by_IO_Read_Bytes(const MonPID& a, const MonPID& b) {
    return a.read_bytes_delta > b.read_bytes_delta;
}

bool compare_by_IO_Write_Bytes(const MonPID& a, const MonPID& b) {
    return a.write_bytes_delta > b.write_bytes_delta;
}

bool compare_by_cpu_delay(const MonPID& a, const MonPID& b) {
    return a.cpu_delay_delta > b.cpu_delay_delta;
}

bool compare_by_blkio_delay(const MonPID& a, const MonPID& b) {
    return a.blkio_delay_delta > b.blkio_delay_delta;
}

bool compare_by_swapin_delay(const MonPID& a, const MonPID& b) {
    return a.swapin_delay_delta > b.swapin_delay_delta;
}

// Show the collected metrics
void MonPID::trace() const
{
    OvlInfo("%s stats:\n\
\t pid = %u\n\
\t cpu = %llu\n\
\t cpu_delta = %llu\n\
\t VmRSS = %llu bytes\n\
\t read_bytes = %lld bytes\n\
\t read_bytes_delta = %lld bytes\n\
\t write_bytes = %lld bytes\n\
\t write_bytes_delta = %lld bytes\n\
\t blkio_delay_total = %lld nanosec\n\
\t blkio_delay_delta = %lld nanosec\n\
\t swapin_delay_total = %lld nanosec\n\
\t swapin_delay_delta = %lld nanosec\n\
\t cpu_delay_total = %lld nanosec\n\
\t cpu_delay_delta = %lld nanosec\n\
\t found = %d\n",
        name.c_str(),
        pid,
        cpu_total,
        cpu_delta,
        vmRSS,
        read_bytes,
        read_bytes_delta,
        write_bytes,
        write_bytes_delta,
        blkio_delay_total,
        blkio_delay_delta,
        swapin_delay_total,
        swapin_delay_delta,
        cpu_delay_total,
        cpu_delay_delta,
        found);
}
