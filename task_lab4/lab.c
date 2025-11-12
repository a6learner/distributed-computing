#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#include "message.h"
#include "log.h"
#include "process.h"

/** Value of this variable is unique for each process
 * Students can use it for implementation of Lamport's clock or remove it
 */
int lamport_time = 0;

void parent_work(int count_nodes)
{
    /* STUDENT IMPLEMENTATION STARTED */
}

void child_work(struct child_arguments args)
{
    /* Child arguments */
    local_id self_id = args.self_id;
    int count_nodes = args.count_nodes;
    uint8_t balance = args.balance;
    bool mutex_usage = args.mutex_usage;
    int count_prints = self_id * 5;

    /* System process identifiers used for logs */
    pid_t self_pid = getpid();
    pid_t parent_pid = getppid();

    /* STUDENT IMPLEMENTATION STARTED */
}
