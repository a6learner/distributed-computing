#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>

#include "message.h"
#include "log.h"
#include "process.h"
#include "banking.h"

/* ---------------- Lamport clock ---------------- */
static timestamp_t ltime = 0;
static inline timestamp_t get_lamport_time(void)        { return ltime; }
static inline void inc_lamport_time(void)               { ++ltime; }
static inline void sync_lamport_time(timestamp_t other) { ltime = (ltime > other ? ltime : other) + 1; }

/* ---------------- utility ---------------- */
static void fill_msg(Message *m, MessageType t, const void *payload, size_t len) {
    inc_lamport_time();
    m->s_header.s_magic = MESSAGE_MAGIC;
    m->s_header.s_type  = t;
    m->s_header.s_payload_len = len;
    m->s_header.s_local_time  = get_lamport_time();
    if (len && payload) memcpy(m->s_payload, payload, len);
}

static void wait_all(MessageType type, int nproc, local_id self) {
    Message msg;
    for (int i = 1; i < nproc; ++i) {
        if (i == self) continue;
        do { receive(i, &msg); } while (msg.s_header.s_type != type);
        sync_lamport_time(msg.s_header.s_local_time);
    }
}

/* ---------------- parent ---------------- */
void parent_work(int nproc) {
    AllHistory all;
    all.s_history_len = nproc - 1;

    wait_all(STARTED, nproc, PARENT_ID);
    bank_operations(nproc - 1);

    Message stop;
    fill_msg(&stop, STOP, NULL, 0);
    send_multicast(&stop);

    wait_all(DONE, nproc, PARENT_ID);

    Message msg;
    for (int i = 1; i < nproc; ++i) {
        do { receive(i, &msg); } while (msg.s_header.s_type != BALANCE_HISTORY);
        sync_lamport_time(msg.s_header.s_local_time);
        memcpy(&all.s_history[i - 1], msg.s_payload, msg.s_header.s_payload_len);
    }
    print_history(&all);
}

/* ---------------- helper ---------------- */
static void update_history(BalanceHistory *h, balance_t bal,
                           timestamp_t from, timestamp_t to, balance_t pend) {
    if (to > MAX_T) to = MAX_T;
    for (timestamp_t t = from; t <= to; ++t) {
        h->s_history[t].s_balance = bal;
        h->s_history[t].s_balance_pending_in = pend;
        h->s_history[t].s_time = t;
    }
    if (to + 1 > h->s_history_len)
        h->s_history_len = to + 1;
}

/* ---------------- child ---------------- */
void child_work(struct child_arguments a) {
    local_id self = a.self_id;
    int nproc = a.count_nodes;
    balance_t bal = a.balance;
    BalanceHistory hist;
    memset(&hist, 0, sizeof(hist));
    hist.s_id = self;
    update_history(&hist, bal, 0, 0, 0);

    pid_t pid = getpid(), ppid = getppid();
    char buf[BUF_SIZE];

    /* STARTED --------------------------------------------------- */
    snprintf(buf, sizeof(buf), log_started_fmt,
             get_lamport_time(), self, pid, ppid, bal);
    Message started;
    fill_msg(&started, STARTED, buf, strlen(buf));
    shared_logger(buf);
    send_multicast(&started);

    wait_all(STARTED, nproc, self);
    snprintf(buf, sizeof(buf), log_received_all_started_fmt,
             get_lamport_time(), self);
    shared_logger(buf);

    /* MAIN LOOP ------------------------------------------------- */
    int running = 1;
    Message msg;
    while (running) {
        receive_any(&msg);
        sync_lamport_time(msg.s_header.s_local_time);

        switch (msg.s_header.s_type) {
        case TRANSFER: {
            TransferOrder *ord = (TransferOrder *)msg.s_payload;
            if (ord->s_src == self) {
                /* sender - 关键修复：在发送时刻减少余额 */
                Message fwd;
                fill_msg(&fwd, TRANSFER, ord, sizeof(*ord));  // 先准备消息，时钟递增
                
                timestamp_t send_t = get_lamport_time();      // 获取发送时刻
                bal -= ord->s_amount;                         // 在发送时刻减少余额
                
                snprintf(buf, sizeof(buf), log_transfer_out_fmt,
                         send_t, self, ord->s_amount, ord->s_dst);
                shared_logger(buf);
                update_history(&hist, bal, send_t, send_t, 0);

                send(ord->s_dst, &fwd);                       // 发送消息
            } else if (ord->s_dst == self) {
                /* receiver */
                timestamp_t lm = msg.s_header.s_local_time;   // 发送方的时间戳
                timestamp_t recv_t = get_lamport_time();      // 接收时刻
                
                // 标记pending: [lm, recv_t)
                for (timestamp_t t = lm; t < recv_t && t <= MAX_T; t++) {
                    if (t >= hist.s_history_len) {
                        hist.s_history[t].s_balance = bal;
                        hist.s_history[t].s_time = t;
                    }
                    hist.s_history[t].s_balance_pending_in = ord->s_amount;
                }
                
                if (recv_t > hist.s_history_len) {
                    hist.s_history_len = recv_t;
                }
                
                bal += ord->s_amount;
                update_history(&hist, bal, recv_t, recv_t, 0);
                
                snprintf(buf, sizeof(buf), log_transfer_in_fmt,
                         recv_t, self, ord->s_amount, ord->s_src);
                shared_logger(buf);
                
                Message ack;
                fill_msg(&ack, ACK, NULL, 0);
                send(PARENT_ID, &ack);
            }
            break;
        }
        case STOP:
            running = 0;
            break;
        default:
            break;
        }
    }

    /* DONE ------------------------------------------------------ */
    inc_lamport_time();
    snprintf(buf, sizeof(buf), log_done_fmt,
             get_lamport_time(), self, bal);
    shared_logger(buf);
    Message done;
    fill_msg(&done, DONE, buf, strlen(buf));
    send_multicast(&done);

    wait_all(DONE, nproc, self);
    snprintf(buf, sizeof(buf), log_received_all_done_fmt,
             get_lamport_time(), self);
    shared_logger(buf);

    /* BALANCE HISTORY ------------------------------------------- */
    inc_lamport_time();
    hist.s_history_len = get_lamport_time() + 1;
    Message histmsg;
    fill_msg(&histmsg, BALANCE_HISTORY, &hist, sizeof(hist));
    send(PARENT_ID, &histmsg);
}

/* ---------------- transfer() ---------------- */
void transfer(local_id src, local_id dst, balance_t amount) {
    TransferOrder ord = {src, dst, amount};
    Message msg;
    fill_msg(&msg, TRANSFER, &ord, sizeof(ord));
    send(src, &msg);

    Message ack;
    do { receive_any(&ack); } while (ack.s_header.s_type != ACK);
    sync_lamport_time(ack.s_header.s_local_time);
}

/* ---------------- example bank ops ---------------- */
__attribute__((weak))
void bank_operations(local_id max_id) {
    for (local_id i = 1; i < max_id; ++i)
        transfer(i, i + 1, i);
    if (max_id > 1)
        transfer(max_id, 1, 1);
}
