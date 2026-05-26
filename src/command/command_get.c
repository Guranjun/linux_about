#include "common.h"
#include "command_get.h"
#include "json_about.h"
#include "msg_about.h"
#include "my_time.h"
#include "file_query.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Command_Msg_t cmd_msg;
    bool cmd_pending;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    Log_Msg_t log_msg;
} Command_Data_t;

static Command_Data_t cmd_data;

static void command_data_init(void)
{
    memset(&cmd_data, 0, sizeof(cmd_data));
    pthread_mutex_init(&cmd_data.lock, NULL);
    pthread_cond_init(&cmd_data.cond, NULL);
}

static void command_data_deinit(void)
{
    msg_unregister_module(MODULE_ID_COMMAND);
    pthread_mutex_destroy(&cmd_data.lock);
    pthread_cond_destroy(&cmd_data.cond);
}

/**
 * @brief 命令分发器，根据 cmd 执行对应操作
 */
static void command_dispatch(Command_Msg_t *cmd)
{
    if (cmd == NULL) return;

    switch (cmd->cmd) {
        case CMD_STORAGE_QUERY_FILES: {
            handle_query_files();
            break;
        }

        /* ---- 预留：后续其他查询/控制命令在此新增 case ---- */
        /* case CMD_SOMETHING: { ...  break; } */

        default: {
            log_make(&cmd_data.log_msg, WARN, gettime_us(),
                     MODULE_ID_COMMAND, "Unknown cmd");
            msg_dispatch(MODULE_ID_COMMAND, MODULE_ID_LOGGER,
                         sizeof(cmd_data.log_msg), MSG_TYPE_LOG, &cmd_data.log_msg);
            break;
        }
    }
}

/* ======================== 消息处理函数 ======================== */

void command_msg_handler(Common_Msg_t *msg)
{
    if (msg == NULL) return;

    switch (msg->msg_type) {
        case MSG_TYPE_COMMAND: {
            Command_Msg_t *cmd = (Command_Msg_t *)msg->data;

            pthread_mutex_lock(&cmd_data.lock);
            /* 保存到内部缓冲区，等待线程处理 */
            memcpy(&cmd_data.cmd_msg, cmd, sizeof(Command_Msg_t));
            cmd_data.cmd_msg.param = NULL; /* 不引用外部 param，避免野指针 */
            cmd_data.cmd_pending = true;
            pthread_cond_signal(&cmd_data.cond);
            pthread_mutex_unlock(&cmd_data.lock);
            break;
        }
        case MSG_TYPE_BIGDATA: {
            /* BigData 发送完成后的回调清理 */
            BigData_Msg_t *b_msg = (BigData_Msg_t *)msg->data;
            if (b_msg == NULL) break;

            if (b_msg->status == DONE || b_msg->status == FILE_DELIVER_ERROR) {
                if (b_msg->data_ptr != NULL) {
                    free(b_msg->data_ptr);  /* cJSON_PrintUnformatted 的 malloc */
                }
                free(b_msg);
                msg->data = NULL;
            } else if (b_msg->status == RESEND) {
                msg_dispatch(MODULE_ID_COMMAND, MODULE_ID_TCP_SEND,
                             b_msg->total_len, MSG_TYPE_BIGDATA, b_msg);
            }
            break;
        }
        default:
            break;
    }
}

void command_msg_release_handler(Common_Msg_t *msg)
{
    (void)msg;
}

/* ======================== 线程入口 ======================== */

void* command_process_thread(void* arg)
{
    (void)arg;

    command_data_init();
    msg_register_module(MODULE_ID_COMMAND, command_msg_handler, command_msg_release_handler);

    log_make(&cmd_data.log_msg, INFO, gettime_us(),
             MODULE_ID_COMMAND, "Command module started");
    msg_dispatch(MODULE_ID_COMMAND, MODULE_ID_LOGGER,
                 sizeof(cmd_data.log_msg), MSG_TYPE_LOG, &cmd_data.log_msg);

    while (running) {
        pthread_mutex_lock(&cmd_data.lock);
        while (!cmd_data.cmd_pending && running) {
            pthread_cond_wait(&cmd_data.cond, &cmd_data.lock);
        }
        if (!running) {
            pthread_mutex_unlock(&cmd_data.lock);
            break;
        }
        Command_Msg_t local_cmd;
        memcpy(&local_cmd, &cmd_data.cmd_msg, sizeof(Command_Msg_t));
        cmd_data.cmd_pending = false;
        pthread_mutex_unlock(&cmd_data.lock);

        command_dispatch(&local_cmd);
    }

    command_data_deinit();
    return NULL;
}

void command_thread_wakeup(void)
{
    pthread_mutex_lock(&cmd_data.lock);
    pthread_cond_signal(&cmd_data.cond);
    pthread_mutex_unlock(&cmd_data.lock);
}