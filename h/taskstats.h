
#ifndef TASKSTATS_H
#define TASKSTATS_H

#include <sys/types.h>
#include <linux/taskstats.h>

typedef enum {
    CRITICAL_FAIL   = -2,
    FAIL            = -1,
    SUCCESS         =  0
} nl_rc;

namespace taskstat {
    nl_rc nl_init(void);
    void nl_fini(void);
    bool is_socket_alive();

    nl_rc nl_taskstats_info(pid_t, taskstats*);

    void dump_ts(taskstats& ts);

}

#endif // TASKSTATS_H
