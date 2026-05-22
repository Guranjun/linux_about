#include "common.h"
#include "msg_about.h"
#include "msg_deliver.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_MSG_QUEUE_SIZE 32
#define MAX_LOG_MSG_SIZE 64

typedef struct {
    Common_Msg_t msg_buffer[MAX_MSG_QUEUE_SIZE];
    Log_Msg_t log_msg_buffer[MAX_LOG_MSG_SIZE];
    uint8_t log_idx;
    uint8_t count;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} Msg_Queue_t;

static Msg_Queue_t msg_queue;
static pthread_once_t msg_once = PTHREAD_ONCE_INIT;
static int msg_initialized = 0;

__attribute__((weak)) void V4L2_msg_handler(Common_Msg_t *msg) { (void)msg; }
__attribute__((weak)) void V4L2_msg_release_handler(Common_Msg_t *msg) { (void)msg; }
__attribute__((weak)) void udp_msg_handler(Common_Msg_t *msg) { (void)msg; }
__attribute__((weak)) void udp_msg_release_handler(Common_Msg_t *msg) { (void)msg; }
__attribute__((weak)) void alarm_msg_handler(Common_Msg_t *msg) { (void)msg; }
__attribute__((weak)) void alarm_msg_release_handler(Common_Msg_t *msg) { (void)msg; }
__attribute__((weak)) void logger_msg_handler(Common_Msg_t *msg) { (void)msg; }
__attribute__((weak)) void logger_msg_release_handler(Common_Msg_t *msg) { (void)msg; }
__attribute__((weak)) void storage_msg_handler(Common_Msg_t *msg) { (void)msg; }
__attribute__((weak)) void storage_msg_release_handler(Common_Msg_t *msg) { (void)msg; }
__attribute__((weak)) void tcp_send_msg_handler(Common_Msg_t *msg) { (void)msg; }
__attribute__((weak)) void tcp_send_msg_release_handler(Common_Msg_t *msg) { (void)msg; }
__attribute__((weak)) void tcp_recv_msg_handler(Common_Msg_t *msg) { (void)msg; }
__attribute__((weak)) void tcp_recv_msg_release_handler(Common_Msg_t *msg) { (void)msg; }
__attribute__((weak)) void command_msg_handler(Common_Msg_t *msg) { (void)msg; }
__attribute__((weak)) void command_msg_release_handler(Common_Msg_t *msg) { (void)msg; }

static void msg_queue_init_once(void)
{
    memset(&msg_queue, 0, sizeof(msg_queue));
    pthread_mutex_init(&msg_queue.lock, NULL);
    pthread_cond_init(&msg_queue.cond, NULL);
    msg_initialized = 1;
    msg_register_module(MODULE_ID_V4L2, V4L2_msg_handler, V4L2_msg_release_handler);
    msg_register_module(MODULE_ID_UDP, udp_msg_handler, udp_msg_release_handler);
    msg_register_module(MODULE_ID_ALARM, alarm_msg_handler, alarm_msg_release_handler);
    msg_register_module(MODULE_ID_LOGGER, logger_msg_handler, logger_msg_release_handler);
    msg_register_module(MODULE_ID_STORAGE, storage_msg_handler, storage_msg_release_handler);
    msg_register_module(MODULE_ID_TCP, tcp_send_msg_handler, tcp_send_msg_release_handler);
    msg_register_module(MODULE_ID_COMMAND, command_msg_handler, command_msg_release_handler);
}

static void msg_ensure_init(void)
{
    pthread_once(&msg_once, msg_queue_init_once);
}

static Common_Msg_t msg_prepare_for_queue(const Common_Msg_t *msg)
{
    Common_Msg_t queued;

    queued = *msg;

    if (queued.msg_type == MSG_TYPE_LOG && queued.data != NULL) {
        uint32_t copy_len = queued.data_len;

        if (copy_len > (uint32_t)sizeof(Log_Msg_t)) {
            copy_len = (uint32_t)sizeof(Log_Msg_t);
        }

        memset(&msg_queue.log_msg_buffer[msg_queue.log_idx], 0, sizeof(Log_Msg_t));
        memcpy(&msg_queue.log_msg_buffer[msg_queue.log_idx], queued.data, copy_len);
        queued.data = &msg_queue.log_msg_buffer[msg_queue.log_idx];
        queued.data_len = copy_len;
        msg_queue.log_idx = (uint8_t)((msg_queue.log_idx + 1U) % MAX_LOG_MSG_SIZE);
    }

    return queued;
}

static void msg_queue_push(const Common_Msg_t *msg)
{
#ifdef MSG_ENABLE_PRIORITY
    uint8_t insert_at = msg_queue.count;

    while (insert_at > 0U &&
           msg_queue.msg_buffer[insert_at - 1U].priority < msg->priority) {
        msg_queue.msg_buffer[insert_at] = msg_queue.msg_buffer[insert_at - 1U];
        --insert_at;
    }

    msg_queue.msg_buffer[insert_at] = *msg;
#else
    msg_queue.msg_buffer[msg_queue.count] = *msg;
#endif

    ++msg_queue.count;
}

static int msg_queue_pop(Common_Msg_t *msg)
{
    uint8_t remaining;

    if (msg_queue.count == 0U) {
        return -1;
    }

    *msg = msg_queue.msg_buffer[0];
    remaining = (uint8_t)(msg_queue.count - 1U);
    if (remaining > 0U) {
        memmove(&msg_queue.msg_buffer[0],
                &msg_queue.msg_buffer[1],
                (size_t)remaining * sizeof(Common_Msg_t));
    }
    msg_queue.count = remaining;

    return 0;
}

static void msg_release(Common_Msg_t *msg)
{
    if (msg == NULL) {
        return;
    }
    msg_module_release_handler(msg);
}

void msg_init(void)
{
    msg_ensure_init();
}

Common_Msg_t msg_make(Module_ID_e src, Module_ID_e dst, uint32_t len, Msg_Type_e type, void *data)
{
    Common_Msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.src_module = src;
    msg.dst_module = dst;
    msg.data_len = len;
    msg.msg_type = type;
    msg.data = data;
#ifdef MSG_ENABLE_PRIORITY
    msg.priority = MSG_PRIORITY_NORMAL;
#endif

    return msg;
}

#ifdef MSG_ENABLE_PRIORITY
Common_Msg_t msg_make_with_priority(Module_ID_e src,
                                    Module_ID_e dst,
                                    uint32_t len,
                                    Msg_Type_e type,
                                    Msg_Priority_e priority,
                                    void *data)
{
    Common_Msg_t msg = msg_make(src, dst, len, type, data);

    msg.priority = priority;
    return msg;
}

void msg_set_priority(Common_Msg_t *msg, Msg_Priority_e priority)
{
    if (msg != NULL) {
        msg->priority = priority;
    }
}
#endif

int msg_send(Common_Msg_t *msg)
{
    Common_Msg_t queued;

    if (msg == NULL) {
        return -1;
    }

    msg_ensure_init();

    pthread_mutex_lock(&msg_queue.lock);

    if (msg_queue.count >= MAX_MSG_QUEUE_SIZE) {
        pthread_mutex_unlock(&msg_queue.lock);
        msg_release(msg);
        return -1;
    }

    queued = msg_prepare_for_queue(msg);
    msg_queue_push(&queued);
    pthread_cond_signal(&msg_queue.cond);
    pthread_mutex_unlock(&msg_queue.lock);

    return 0;
}

int msg_receive(Common_Msg_t *msg)
{
    if (msg == NULL) {
        return -1;
    }

    msg_ensure_init();

    pthread_mutex_lock(&msg_queue.lock);
    while (msg_queue.count == 0U && running) {
        pthread_cond_wait(&msg_queue.cond, &msg_queue.lock);
    }

    if (msg_queue.count == 0U) {
        pthread_mutex_unlock(&msg_queue.lock);
        return -1;
    }

    msg_queue_pop(msg);
    pthread_mutex_unlock(&msg_queue.lock);

    return 0;
}

void msg_deliver_lock(void)
{
    //msg_ensure_init();
    pthread_mutex_lock(&msg_queue.lock);
}

void msg_deliver_unlock(void)
{
    pthread_mutex_unlock(&msg_queue.lock);
}

void msg_cleanup(void)
{
    if (!msg_initialized) {
        return;
    }

    pthread_mutex_lock(&msg_queue.lock);
    msg_queue.count = 0U;
    pthread_mutex_unlock(&msg_queue.lock);
}

void *msg_deliver_thread(void *arg)
{
    Common_Msg_t msg;

    (void)arg;
    msg_init();

    while (running) {
        if (msg_receive(&msg) != 0) {
            continue;
        }
        msg_module_handler(&msg);
        msg_release(&msg);
    }

    msg_cleanup();
    return NULL;
}

void msg_thread_wakeup(void)
{
    msg_ensure_init();
    pthread_mutex_lock(&msg_queue.lock);
    pthread_cond_signal(&msg_queue.cond);
    pthread_mutex_unlock(&msg_queue.lock);
}
