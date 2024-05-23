
#include <stdio.h>
#include "CpuUsage.h"


// Fetch the data for the cpu named from the contents of /proc/stat
// ('cpu ' for totals, cpu0..cpu15 for individual cores)
// Returns false if the CPU named isn't found or if the format is bad
bool CpuUsage::fetch (ProcFileData& proc_stat, const char *cpuname)
{
    if (!proc_stat.refresh())
        return false;
    const char* info = proc_stat.data_after (cpuname);
    if (info == 0)
        return false;

    this->cpuname = cpuname;

    int nread = sscanf (info, "%llu %llu %llu %llu %llu %llu %llu %llu",
                        &user, &nice, &sys, &idle,
                        &iowait, &irq, &softirq, &stolen);

    // Linux kernel 2 has at least 4 values, less than that is unrecoverable
    if (nread < 4)
    {
        OvlError ("%s info incomplete", cpuname);
        return false;
    }

    else if (nread < 7)
    {   // Kernel 2.6 has at least 7 values, but we don't need them all
        iowait = irq = softirq = stolen = 0;

        static bool warned_7 = false;
        if (! warned_7)
        {
            OvlWarn ("Missing CPU fields: iowait, irq, softirq, stolen");
            warned_7 = true;
        }
    }

    else if (nread < 8)
    {
        // Kernel 2.6.11 has all 8 fields,
        stolen = 0;
        static bool warned_8 = false;
        if (! warned_8)
        {
            OvlWarn ("Missing CPU field: stolen");
            warned_8 = true;
        }
    }
    return true;
}

// Show the collected CPU metrics
void CpuUsage::trace()
{
    OvlInfo("%s stats:\n\
\t user=%llu\n\
\t nice=%llu\n\
\t sys=%llu\n\
\t idle=%llu\n\
\t iowait=%llu\n\
\t irq=%llu\n\
\t softirq=%llu\n\
\t stolen=%llu\n",
        cpuname,
        user,
        nice,
        sys,
        idle,
        iowait,
        irq,
        softirq,
        stolen);
}

//// Compute the idle cycles between the CpuUsage given
//static unsigned delta_idle (const CpuUsage& curr, const CpuUsage& prev)
//{
//    return unsigned (curr.idle - prev.idle);
//}

// Compute the total cycles between the CpuUsage given
static unsigned delta_total (const CpuUsage& curr, const CpuUsage& prev)
{
    return unsigned (curr.user    - prev.user)
         + unsigned (curr.nice    - prev.nice)
         + unsigned (curr.sys     - prev.sys)
         + unsigned (curr.idle    - prev.idle)
         + unsigned (curr.iowait  - prev.iowait)
         + unsigned (curr.irq     - prev.irq)
         + unsigned (curr.softirq - prev.softirq)
         + unsigned (curr.stolen  - prev.stolen);
}

bool getCPU(unsigned* delta_jiffies)
{
    static ProcFileData proc_stat(PROC_STAT);

    // Alternate between data sets to compute deltas
    // (toggle is the current set, 1 - toggle is 'last time')
    static int toggle = 1;
    static CpuUsage cpu_usage[2];
    toggle = 1 - toggle;

    if (! cpu_usage[toggle].fetch(proc_stat, CPU_NAME))
        return false;
    if (cpu_usage[1-toggle].isempty())
        return false;


    *delta_jiffies = delta_total(cpu_usage[toggle], cpu_usage[1-toggle]);

    return true;

}


