#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#include "message.h"
#include "log.h"
#include "process.h"

/* ================= PARENT ================= */
void parent_work(int count_nodes)
{
    int children_total = count_nodes - 1;

    /* Receive STARTED from all children */
    for (int i = 1; i <= children_total; i++) {
        Message msg;
        receive(i, &msg);  // 每个子进程都要收一次
    }

    /* Receive DONE from all children */
    for (int i = 1; i <= children_total; i++) {
        Message msg;
        receive(i, &msg);
    }

    /* 父进程不打印任何日志 */
}

/* ================= CHILD ================= */
void child_work(struct child_arguments args)
{
    local_id self_id = args.self_id;
    int count_nodes = args.count_nodes;
    pid_t self_pid = getpid();
    pid_t parent_pid = getppid();

    /* -------- Phase 1: STARTED -------- */
    {
        char payload[BUF_SIZE];
        snprintf(payload, sizeof(payload),
                 log_started_fmt, 0, self_id, self_pid, parent_pid, args.balance);

        Message msg;
        fill_message(&msg, STARTED, 0, payload, (size_t)strlen(payload));
        send_multicast(&msg);

        shared_logger(payload);
    }

    /* Receive STARTED from all other children */
    {
        for (int i = 1; i < count_nodes; i++) {
            if (i == self_id) continue; // 跳过自己
            Message msg;
            receive(i, &msg);  // 指定来源
        }

        char buf[BUF_SIZE];
        snprintf(buf, sizeof(buf), log_received_all_started_fmt, 0, self_id);
        shared_logger(buf);
    }

    /* -------- Phase 2: useful work (none in this lab) -------- */
    // 跳过

    /* -------- Phase 3: DONE -------- */
    {
        char payload[BUF_SIZE];
        snprintf(payload, sizeof(payload),
                 log_done_fmt, 0, self_id, args.balance);

        Message msg;
        fill_message(&msg, DONE, 0, payload, (size_t)strlen(payload));
        send_multicast(&msg);

        shared_logger(payload);
    }

    /* Receive DONE from all other children */
    {
        for (int i = 1; i < count_nodes; i++) {
            if (i == self_id) continue;
            Message msg;
            receive(i, &msg);
        }

        char buf[BUF_SIZE];
        snprintf(buf, sizeof(buf), log_received_all_done_fmt, 0, self_id);
        shared_logger(buf);
    }
}
