/*
-----------------------------------------------------------------------------
    ProcUsage
    Per process resource usage

    Author: CostisC
-----------------------------------------------------------------------------
*/

#ifndef PROC_USAGE_H
#define PROC_USAGE_H

#include <unistd.h>
#include <string>
#include <linux/taskstats.h>
#include "ProcFile.h"

class MonPID
{
    pid_t	        pid;
    std::string	    name;
    OVLValue        cpu_total;
    OVLValue        cpu_delta;
    OVLValue        vmRSS;
    OVLValue        read_bytes;
    OVLValue        read_bytes_delta;
    OVLValue        write_bytes;
    OVLValue        write_bytes_delta;
    // following delay fields are in nanoseconds
    OVLValue        blkio_delay_total;
    OVLValue        blkio_delay_delta;
    OVLValue        swapin_delay_total;
    OVLValue        swapin_delay_delta;
    OVLValue        cpu_delay_total;
    OVLValue        cpu_delay_delta;

    bool            found;              // set to true, if the update gets successful
    bool            initial_sample;     // true, during the first sampling

    static bool skip_taskstat;

    int fetch_taskstats(pid_t pid, taskstats* ts);

public:
    MonPID(pid_t = 0);


/* Upate the monitored PID data or return false if the PID is no longer accessible.
   Reads /proc/<pid>/stat and updates usage with the sum of the utime, stime, cutime,
   and cstime (process and cumulative user and system 'jiffies') read,
   and /proc/<pid>/status for the memory part.
*/
    bool update();

    void trace() const;

    void set_found(bool v) { found = v; };
    bool isfound() const { return found; };
    std::string get_name() const { return name; }
    pid_t get_pid() const { return pid; }
    void set_name(std::string str) { name = str; }
    OVLValue get_cpu() const { return cpu_delta; }
    OVLValue get_RSS() const { return vmRSS; }
    OVLValue get_read_bytes_delta() const { return read_bytes_delta; }
    OVLValue get_write_bytes_delta() const { return write_bytes_delta; }
    OVLValue get_blkio_delay_delta() const { return blkio_delay_delta; }
    OVLValue get_swapin_delay_delta() const { return swapin_delay_delta; }
    OVLValue get_cpu_delay_delta() const { return cpu_delay_delta; }

    MonPID& operator+=(const MonPID& right);

    friend bool compare_by_CPU(const MonPID&, const MonPID&);
    friend bool compare_by_RSS(const MonPID&, const MonPID&);
    friend bool compare_by_IO_Read_Bytes(const MonPID&, const MonPID&);
    friend bool compare_by_IO_Write_Bytes(const MonPID&, const MonPID&);
    friend bool compare_by_cpu_delay(const MonPID&, const MonPID&);
    friend bool compare_by_blkio_delay(const MonPID&, const MonPID&);
    friend bool compare_by_swapin_delay(const MonPID&, const MonPID&);

};


// sorting comparators (to sort in descending order)
bool compare_by_CPU(const MonPID&, const MonPID&);
bool compare_by_RSS(const MonPID&, const MonPID&);
bool compare_by_IO_Read_Bytes(const MonPID&, const MonPID&);
bool compare_by_IO_Write_Bytes(const MonPID&, const MonPID&);
bool compare_by_cpu_delay(const MonPID&, const MonPID&);
bool compare_by_blkio_delay(const MonPID&, const MonPID&);
bool compare_by_swapin_delay(const MonPID&, const MonPID&);

#endif      // PROC_USAGE_H
