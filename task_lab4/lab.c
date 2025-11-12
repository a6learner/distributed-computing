#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>

#include "message.h"
#include "log.h"
#include "process.h"

/* ============ Lamport Clock ============ */
static timestamp_t lamport_time = 0;

static timestamp_t get_lamport_time(void) {
    return lamport_time;
}

static void inc_lamport_time(void) {
    lamport_time++;
}

static void update_lamport_time(timestamp_t received_time) {
    if (received_time > lamport_time) {
        lamport_time = received_time;
    }
    lamport_time++;
}

/* ============ Global Variables ============ */
static local_id my_id = 0;
static int process_count = 0;

// Mutual exclusion state
static bool am_requesting = false;
static timestamp_t my_request_time = 0;
static int reply_count = 0;
static bool deferred_replies[MAX_PROCESS_ID + 1];

// DONE tracking
static bool received_done[MAX_PROCESS_ID + 1];
static int done_counter = 0;

/* ============ Helper Functions ============ */
static void create_message(Message *msg, MessageType type, const char *payload) {
    inc_lamport_time();
    
    msg->s_header.s_magic = MESSAGE_MAGIC;
    msg->s_header.s_type = type;
    msg->s_header.s_local_time = get_lamport_time();
    
    if (payload != NULL) {
        size_t len = strlen(payload);
        msg->s_header.s_payload_len = len;
        memcpy(msg->s_payload, payload, len);
    } else {
        msg->s_header.s_payload_len = 0;
    }
}

static void mark_done_received(local_id from) {
    if (from > 0 && from < process_count && from != my_id) {
        if (!received_done[from]) {
            received_done[from] = true;
            done_counter++;
        }
    }
}

/* ============ Ricart-Agrawala Algorithm ============ */
static void handle_cs_request_msg(local_id from, timestamp_t req_time) {
    bool should_reply_now = false;
    
    if (!am_requesting) {
        // Not requesting - grant immediately
        should_reply_now = true;
    } else {
        // Both requesting - compare timestamps
        if (req_time < my_request_time) {
            should_reply_now = true;
        } else if (req_time > my_request_time) {
            should_reply_now = false;
        } else {
            // Same timestamp - use process ID
            should_reply_now = (from < my_id);
        }
    }
    
    if (should_reply_now) {
        Message reply;
        create_message(&reply, CS_REPLY, NULL);
        send(from, &reply);
    } else {
        deferred_replies[from] = true;
    }
}

static void enter_critical_section(void) {
    am_requesting = true;
    reply_count = 0;
    
    // Send CS_REQUEST to all processes
    Message request;
    create_message(&request, CS_REQUEST, NULL);
    my_request_time = request.s_header.s_local_time;
    
    for (local_id i = 0; i < process_count; i++) {
        if (i != my_id) {
            send(i, &request);
        }
    }
    
    // Wait for all replies
    int needed_replies = process_count - 1; // All except self
    
    while (reply_count < needed_replies) {
        Message msg;
        local_id sender = receive_any(&msg);
        update_lamport_time(msg.s_header.s_local_time);
        
        switch (msg.s_header.s_type) {
            case CS_REPLY:
                reply_count++;
                break;
            
            case CS_REQUEST:
                handle_cs_request_msg(sender, msg.s_header.s_local_time);
                break;
            
            case DONE:
                mark_done_received(sender);
                break;
            
            default:
                break;
        }
    }
}

static void leave_critical_section(void) {
    am_requesting = false;
    
    // Send deferred replies
    for (local_id i = 0; i < process_count; i++) {
        if (deferred_replies[i]) {
            Message reply;
            create_message(&reply, CS_REPLY, NULL);
            send(i, &reply);
            deferred_replies[i] = false;
        }
    }
}

/* ============ Parent Process ============ */
void parent_work(int count_nodes) {
    process_count = count_nodes;
    my_id = PARENT_ID;
    
    int done_received_count = 0;
    int expected_done = count_nodes - 1; // All children
    
    while (done_received_count < expected_done) {
        Message msg;
        local_id sender = receive_any(&msg);
        update_lamport_time(msg.s_header.s_local_time);
        
        switch (msg.s_header.s_type) {
            case CS_REQUEST: {
                // Parent always grants permission immediately
                Message reply;
                create_message(&reply, CS_REPLY, NULL);
                send(sender, &reply);
                break;
            }
            
            case DONE:
                done_received_count++;
                break;
            
            default:
                break;
        }
    }
}

/* ============ Child Process ============ */
void child_work(struct child_arguments args) {
    my_id = args.self_id;
    process_count = args.count_nodes;
    bool use_mutex = args.mutex_usage;
    
    // Initialize state
    for (int i = 0; i <= MAX_PROCESS_ID; i++) {
        deferred_replies[i] = false;
        received_done[i] = false;
    }
    done_counter = 0;
    
    char buffer[BUF_SIZE];
    
    /* ========== PHASE 1: STARTED ========== */
    snprintf(buffer, BUF_SIZE, log_started_fmt,
             get_lamport_time(), my_id, getpid(), getppid(), 0);
    shared_logger(buffer);
    
    Message started_msg;
    create_message(&started_msg, STARTED, buffer);
    send_multicast(&started_msg);
    
    // Wait for STARTED from all other children
    int started_count = 0;
    int expected_started = process_count - 2; // All except self and parent
    
    while (started_count < expected_started) {
        Message msg;
        local_id sender = receive_any(&msg);
        update_lamport_time(msg.s_header.s_local_time);
        
        switch (msg.s_header.s_type) {
            case STARTED:
                started_count++;
                break;
            
            case CS_REQUEST:
                handle_cs_request_msg(sender, msg.s_header.s_local_time);
                break;
            
            default:
                break;
        }
    }
    
    snprintf(buffer, BUF_SIZE, log_received_all_started_fmt,
             get_lamport_time(), my_id);
    shared_logger(buffer);
    
    /* ========== PHASE 2: Main Work ========== */
    int total_iterations = my_id * 5;
    
    for (int iteration = 1; iteration <= total_iterations; iteration++) {
        if (use_mutex) {
            enter_critical_section();
        }
        
        snprintf(buffer, BUF_SIZE, log_loop_operation_fmt,
                 my_id, iteration, total_iterations);
        print(buffer);
        
        if (use_mutex) {
            leave_critical_section();
        }
    }
    
    /* ========== PHASE 3: DONE ========== */
    snprintf(buffer, BUF_SIZE, log_done_fmt,
             get_lamport_time(), my_id, 0);
    shared_logger(buffer);
    
    Message done_msg;
    create_message(&done_msg, DONE, buffer);
    send_multicast(&done_msg);
    
    // Wait for DONE from all other children
    int expected_done = process_count - 2;
    
    while (done_counter < expected_done) {
        Message msg;
        local_id sender = receive_any(&msg);
        update_lamport_time(msg.s_header.s_local_time);
        
        switch (msg.s_header.s_type) {
            case DONE:
                mark_done_received(sender);
                break;
            
            case CS_REQUEST: {
                // Always reply immediately in phase 3
                Message reply;
                create_message(&reply, CS_REPLY, NULL);
                send(sender, &reply);
                break;
            }
            
            default:
                break;
        }
    }
    
    snprintf(buffer, BUF_SIZE, log_received_all_done_fmt,
             get_lamport_time(), my_id);
    shared_logger(buffer);
}
