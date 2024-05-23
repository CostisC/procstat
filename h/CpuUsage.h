
/*
-----------------------------------------------------------------------------
    CPU Usage

    Author: CostisC
-----------------------------------------------------------------------------
*/

#ifndef CPU_USAGE_H
#define CPU_USAGE_H

#include "ProcFile.h"


// CPU statistics are in /proc/stat
#define PROC_STAT   "/proc/stat"

// Name of total CPU
#define CPU_NAME  "cpu "

/*
 CPU data in /proc/stat -- stored (per-CPU) is a line of numbers
 Generally we don't care much, we simply need the total and idle
 counts to get 'usage'.
 Counts are actually the number of 'jiffies' the kernel schedules
 Jiffies are 1/'$HZ' -- this is 1/250 on kernel 2.6.16.60-0.66.1-smp
*/

class CpuUsage
{
public:
    OVLValue user, nice, sys, idle;
    OVLValue iowait, irq, softirq, stolen;

    // CPU type last queried (it can be either the total or specific CPU core)
    const char* cpuname;

    CpuUsage()
        : user (0), nice (0), sys (0), idle (0)
        , iowait (0), irq (0), softirq (0), stolen (0)
    { }


    // True if the data (just the idle count, but it's enough) is empty.
    // Just used to suppress the very first report.
    bool isempty () const { return idle == 0; }

    // Fetch data from proc_stat for the named CPU
    bool fetch(ProcFileData& proc_stat, const char *cpuname);

    void trace();


};

// CPU jiffies passed since last called time
// Return true if a valid delta could be computed
extern bool getCPU(unsigned* delta);

#endif      // CPU_USAGE_H
