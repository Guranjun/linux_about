#include "sqlite3.h"
#include "common.h"
#include "msg_about.h"
#include "sqlite_about.h"
#include "cJSON.h"
#include "log.h"
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/mman.h>   
#include <unistd.h> 

#define MAX_DB_SIZE 16384
#define DELETE_LOG_NUM 512

/**
 * @brief 日志模块数据结构体
 */
typedef struct{
    Log_Buffer_t Buffer_A;         /**< 缓冲区 A */
    Log_Buffer_t Buffer_B;         /**< 缓冲区 B */
    Log_Buffer_t* input_ptr;       /**< 指向当前接收数据的缓冲区 */
    Log_Buffer_t* process_ptr;     /**< 指向待写入数据库的缓冲区 */
    pthread_mutex_t lock;
    pthread_cond_t cond;
    sqlite3* db;                   /**< 数据库句柄 */
    Command_Msg_t cmd_msg;         /**< 接收到的命令消息 */
}LOG_DATA_BUF;

static LOG_DATA_BUF log_data_buf;

/**
 * @brief 初始化日志数据缓冲区
 */
static void log_init(void)
{   
    log_data_buf.Buffer_A.count = 0;
    log_data_buf.Buffer_B.count = 0;
    log_data_buf.input_ptr = &log_data_buf.Buffer_A;
    log_data_buf.process_ptr = &log_data_buf.Buffer_B;
    memset(&log_data_buf.cmd_msg, 0, sizeof(Command_Msg_t));
    pthread_mutex_init( &log_data_buf.lock, NULL);
    pthread_cond_init( &log_data_buf.cond, NULL);
} 

/**
 * @brief 释放日志模块资源
 */
static void log_deinit(void)
{
    msg_unregister_module(MODULE_ID_LOGGER);
    pthread_mutex_destroy( &log_data_buf.lock);
    pthread_cond_destroy( &log_data_buf.cond);
}

/**
 * @brief 按需通过 mmap 将日志文件发往上位机
 * @param cp_fd 日志文件描述符
 */
void export_logs_on_demand(int cp_fd)
{
    BigData_Msg_t* bigdata_msg = malloc(sizeof(BigData_Msg_t));
    int fd = cp_fd;
    struct stat st;
    fstat(fd, &st);
    size_t file_size = st.st_size;
    void* addr = mmap(NULL, file_size, PROT_READ, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        close(fd);
        perror("mmap");
        return ;
    }
    bigdata_msg->data_ptr = addr;
    bigdata_msg->total_len = file_size;
    bigdata_msg->type = DB;
    bigdata_msg->status = SEND;
    bigdata_msg->fd = fd;
    msg_dispatch(MODULE_ID_LOGGER, MODULE_ID_TCP_SEND, file_size, MSG_TYPE_BIGDATA, bigdata_msg);
}

/**
 * @brief 构造日志消息
 * @param log_msg   日志消息指针
 * @param level     日志等级
 * @param timestamp 时间戳
 * @param module    模块 ID
 * @param content   日志内容
 */
void log_make(Log_Msg_t* log_msg, LOG_LEVEL level, uint64_t timestamp, Module_ID_e module, const char* content)
{
    log_msg->level = level;
    log_msg->timestamp = timestamp;
    log_msg->module = module;
    memcpy(log_msg->content, content, sizeof(log_msg->content));
}

/**
 * @brief 日志处理线程入口
 * @param arg 传入参数（不使用）
 * @return NULL
 *
 * 后台将日志批量写入 SQLite 数据库，
 * 当收到 CMD_LOG_UPLOAD_DB 命令时通过 mmap 上传日志文件
 */
void* logger_process_thread(void* arg)
{
    log_init();
    log_data_buf.db = DB_Init();
    uint16_t db_sql_count;
    while(running){
        pthread_mutex_lock(&log_data_buf.lock);
        while(log_data_buf.input_ptr->count < 30 && running){
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 10;
            pthread_cond_timedwait(&log_data_buf.cond, &log_data_buf.lock, &ts);
            if(log_data_buf.input_ptr->count > 0 || log_data_buf.cmd_msg.cmd != 0){
                break;
            }
        }
        if(get_db_count(log_data_buf.db, &db_sql_count) == 0){
            if(db_sql_count >= MAX_DB_SIZE){
                delete_db_msg(log_data_buf.db, DELETE_LOG_NUM);
            }
        }
        Log_Buffer_t* temp = log_data_buf.input_ptr;
        log_data_buf.input_ptr = log_data_buf.process_ptr;
        log_data_buf.process_ptr = temp;
        
        pthread_mutex_unlock(&log_data_buf.lock);
        db_save_batch(log_data_buf.db, log_data_buf.process_ptr);
        pthread_mutex_lock(&log_data_buf.lock);
        if(log_data_buf.cmd_msg.cmd == CMD_LOG_UPLOAD_DB){
            log_data_buf.cmd_msg.cmd = 0;
            pthread_mutex_unlock(&log_data_buf.lock);
            upload_db_to_server(log_data_buf.db);
        }
        else{
            pthread_mutex_unlock(&log_data_buf.lock);
        }
    }
    pthread_mutex_lock(&log_data_buf.lock);
    db_save_batch(log_data_buf.db, log_data_buf.input_ptr);
    pthread_mutex_unlock(&log_data_buf.lock);
    
    sqlite3_close(log_data_buf.db);
    log_deinit();
    return NULL;

}

/**
 * @brief 日志模块的消息处理函数
 * @param msg 接收到的消息
 *
 * 支持:
 * - MSG_TYPE_LOG: 缓存日志到缓冲区
 * - MSG_TYPE_COMMAND: 执行命令（如上传数据库）
 * - MSG_TYPE_BIGDATA: 大数据发送完成后的资源清理
 */
void logger_msg_handler(Common_Msg_t* msg)
{
        switch(msg->msg_type){
            case MSG_TYPE_IMAGE:
                break;
            case MSG_TYPE_ALARM:
                break;
            case MSG_TYPE_LOG:{
                Log_Msg_t *data = msg->data ;
                pthread_mutex_lock(&log_data_buf.lock);
                memcpy(&log_data_buf.input_ptr->items[log_data_buf.input_ptr->count] , data , sizeof(Log_Msg_t));
                log_data_buf.input_ptr->items[log_data_buf.input_ptr->count].timestamp = data->timestamp;
                log_data_buf.input_ptr->count++;
                if(log_data_buf.input_ptr->count > 30){
                    pthread_cond_signal(&log_data_buf.cond);
                }
                pthread_mutex_unlock(&log_data_buf.lock);
                break;
            }
            case MSG_TYPE_COMMAND:{
                Command_Msg_t* cmd_msg = (Command_Msg_t*)msg->data;
                pthread_mutex_lock(&log_data_buf.lock);
                log_data_buf.cmd_msg.cmd = cmd_msg->cmd;
                log_data_buf.cmd_msg.src = cmd_msg->src;
                log_data_buf.cmd_msg.type = cmd_msg->type;
                log_data_buf.cmd_msg.param = NULL;
                if (log_data_buf.cmd_msg.cmd >= 0) {
                    pthread_cond_signal(&log_data_buf.cond);
                }
                pthread_mutex_unlock(&log_data_buf.lock);
                break;
            }
            case MSG_TYPE_BIGDATA:{
                BigData_Msg_t* b_msg = (BigData_Msg_t*)msg->data;
                if(b_msg->status == DONE || b_msg->status == FILE_DELIVER_ERROR){
                    if(b_msg->data_ptr != MAP_FAILED){
                    munmap(b_msg->data_ptr, b_msg->total_len);
                    }
                    if(b_msg->fd >= 0){
                        close(b_msg->fd);
                    }
                    free(b_msg);
                    msg->data = NULL;
                }
                else if(b_msg->status == RESEND){
                    msg_dispatch(MODULE_ID_LOGGER, MODULE_ID_TCP_SEND, b_msg->total_len, MSG_TYPE_BIGDATA, b_msg);
                }
                break;
            }
            default:
                break;
        }
}

/**
 * @brief 日志模块的消息释放处理函数
 */
void logger_msg_release_handler(Common_Msg_t* msg)
{
    return;
}

/**
 * @brief 唤醒日志处理线程（用于程序退出时）
 */
void logger_thread_wakeup(void)
{
    pthread_mutex_lock(&log_data_buf.lock);
    pthread_cond_signal(&log_data_buf.cond);
    pthread_mutex_unlock(&log_data_buf.lock);
}