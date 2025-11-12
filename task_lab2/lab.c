#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#include "message.h"
#include "log.h"
#include "process.h"
#include "banking.h"

/**

 * Message flow:
 *   Parent -> Source (TRANSFER)
 *   Source -> Destination (TRANSFER)
 *   Destination -> Parent (ACK)
 *
 *  After all transfers:
 *   Parent sends STOP
 *   Children exchange DONE
 *   Each child sends BALANCE_HISTORY to Parent
 */



/*---------------------------------------------------------------
 * Helper function: Wait for and receive message of given type
 * from all child processes (1..count_nodes-1)
 *--------------------------------------------------------------*/
static void wait_for_all(MessageType type, int count_nodes) {
    Message msg;
    for (int i = 1; i < count_nodes; ++i) {
        receive(i, &msg);
    }
}



void parent_work(int count_nodes)
{
    AllHistory all_history;
    all_history.s_history_len = count_nodes - 1;

    Message msg;
    // wait for all children STARTED
    wait_for_all(STARTED, count_nodes);


    bank_operations(count_nodes - 1);


    {
        Message stop_msg;
        timestamp_t now = get_physical_time();
        fill_message(&stop_msg, STOP, now, NULL, 0);
        send_multicast(&stop_msg);
    }

    //Wait for all children DONE
    wait_for_all(DONE, count_nodes);


    //Collect BALANCE_HISTORY from all children
    for (int i = 1; i < count_nodes; ++i) {
        receive(i, &msg);
        if (msg.s_header.s_type == BALANCE_HISTORY) {
            BalanceHistory *h = (BalanceHistory *) msg.s_payload;
            memcpy(&all_history.s_history[i - 1], h, msg.s_header.s_payload_len);
        }
    }

    //Print all histories to stdout
    print_history(&all_history);
}




void child_work(struct child_arguments args)
{
    
    local_id self_id   = args.self_id;
    int count_nodes    = args.count_nodes;
    balance_t balance  = args.balance;

    // Prepare BalanceHistory structure
    BalanceHistory history;
    history.s_id = self_id;
    history.s_history_len = 1;
    memset(history.s_history, 0, sizeof(history.s_history));
    history.s_history[0].s_balance = balance;
    history.s_history[0].s_time = get_physical_time();
    history.s_history[0].s_balance_pending_in = 0;

    // System PIDs for logs
    pid_t self_pid   = getpid();
    pid_t parent_pid = getppid();


    // PHASE 1: Send STARTED, wait for all others' STARTED

    {
        Message msg;
        timestamp_t t = get_physical_time();
        char buffer[BUF_SIZE];
        snprintf(buffer, sizeof(buffer), log_started_fmt, t, self_id, self_pid, parent_pid, balance);
        shared_logger(buffer);

        fill_message(&msg, STARTED, t, buffer, strlen(buffer));
        send_multicast(&msg);

        // Wait for STARTED from all others
        Message recv_msg;
        for (int i = 1; i < count_nodes; ++i) {
            if (i == self_id) continue;
            receive(i, &recv_msg);
        }

        timestamp_t now = get_physical_time();
        snprintf(buffer, sizeof(buffer), log_received_all_started_fmt, now, self_id);
        shared_logger(buffer);
    }

    
    // PHASE 2: Main work loop – handle TRANSFER and STOP
    
    int active = 1;
    while (active) {
        Message msg;
        local_id from = receive_any(&msg);
        (void) from;
        MessageHeader *h = &msg.s_header;

        switch (h->s_type) {
        case TRANSFER: {
            TransferOrder *order = (TransferOrder *) msg.s_payload;

            timestamp_t now = get_physical_time();

            if (order->s_src == self_id) {
                // This process is the SOURCE
                balance -= order->s_amount;
                history.s_history[now].s_balance = balance;
                history.s_history[now].s_time = now;
                history.s_history[now].s_balance_pending_in = 0;
                history.s_history_len = now + 1;

                // Log money out
                char buf[BUF_SIZE];
                snprintf(buf, sizeof(buf), log_transfer_out_fmt, now, self_id, order->s_amount, order->s_dst);
                shared_logger(buf);

                // Forward TRANSFER to destination
                Message transfer_msg;
                fill_message(&transfer_msg, TRANSFER, now, order, sizeof(TransferOrder));
                send(order->s_dst, &transfer_msg);

            } else if (order->s_dst == self_id) {
                // This process is the DESTINATION
                balance += order->s_amount;
                history.s_history[now].s_balance = balance;
                history.s_history[now].s_time = now;
                history.s_history[now].s_balance_pending_in = 0;
                history.s_history_len = now + 1;

                // Log money in
                char buf[BUF_SIZE];
                snprintf(buf, sizeof(buf), log_transfer_in_fmt, now, self_id, order->s_amount, order->s_src);
                shared_logger(buf);

                // Send ACK to parent
                Message ack_msg;
                fill_message(&ack_msg, ACK, now, NULL, 0);
                send(PARENT_ID, &ack_msg);
            }
            break;
        }

        case STOP:
            active = 0;
            break;

        case DONE:
            break;

        default:
            break;
        }
    }


    // PHASE 3: Termination – send DONE to all, wait for all DONE

    {
        timestamp_t now = get_physical_time();

        char buf[BUF_SIZE];
        snprintf(buf, sizeof(buf), log_done_fmt, now, self_id, balance);
        shared_logger(buf);

        Message done_msg;
        fill_message(&done_msg, DONE, now, buf, strlen(buf));
        send_multicast(&done_msg);

        // Wait for DONE from all others
        Message msg;
        for (int i = 1; i < count_nodes; ++i) {
            if (i == self_id) continue;
            receive(i, &msg);
        }

        now = get_physical_time();
        snprintf(buf, sizeof(buf), log_received_all_done_fmt, now, self_id);
        shared_logger(buf);

        // Prepare and send BALANCE_HISTORY to parent
        Message bh_msg;
        timestamp_t t = get_physical_time();
        uint16_t psize = 2 * sizeof(uint8_t) + history.s_history_len * sizeof(BalanceState);
        fill_message(&bh_msg, BALANCE_HISTORY, t, &history, psize);
        send(PARENT_ID, &bh_msg);
    }
}




// Implementation of transfer() - used by parent process

void transfer(local_id src, local_id dst, balance_t amount)
{
    TransferOrder order = {src, dst, amount};

    // 1. Prepare TRANSFER message for source
    Message msg;
    timestamp_t t = get_physical_time();
    fill_message(&msg, TRANSFER, t, &order, sizeof(TransferOrder));

    // 2. Send it to source process
    send(src, &msg);

    // 3. Wait for ACK from destination
    Message ack;
    while (1) {
        receive_any(&ack);
        if (ack.s_header.s_type == ACK)
            break;
    }
}




__attribute__((weak)) void bank_operations(local_id max_id)
{
    for (int i = 1; i < max_id; ++i) {
        transfer(i, i + 1, i);
    }
    if (max_id > 1) {
        transfer(max_id, 1, 1);
    }
}
