#include "sqlite3.h"
#include "common.h"
#include "msg_about.h"
#include "sqlite_about.h"
#include "cJSON.h"
//#include "cJSON.h"
//#include <corecrt_search.h>
//#include <cstdint>
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


#define MAX_DB_SIZE 16384   //应该设为16384

#define DELETE_LOG_NUM 512

typedef struct{
    Log_Buffer_t Buffer_A;
    Log_Buffer_t Buffer_B;
    Log_Buffer_t* input_ptr;//指向接收数据的日志块
    Log_Buffer_t* process_ptr;//指向待存入数据库的日志块
    pthread_mutex_t lock;
    pthread_cond_t cond;
    sqlite3* db;
    Command_Msg_t cmd_msg;
}LOG_DATA_BUF;

//声明日志队列
static LOG_DATA_BUF log_data_buf;

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

static void log_deinit(void)
{
    msg_unregister_module(MODULE_ID_LOGGER);
    pthread_mutex_destroy( &log_data_buf.lock);
    pthread_cond_destroy( &log_data_buf.cond);
}

void export_logs_on_demand(int cp_fd)
{
    /*如果上位机发送命令要提交日志*/
    /*采用动态的数据链路
    *从数据库采集count条数据，然后再以json的格式组成一个字符串，调用udp/tcp上传日志
    */
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

void log_make(Log_Msg_t* log_msg, LOG_LEVEL level, uint64_t timestamp, Module_ID_e module, const char* content)
{
    log_msg->level = level;
    log_msg->timestamp = timestamp;
    log_msg->module = module;
    memcpy(log_msg->content, content, sizeof(log_msg->content));
}
void* logger_process_thread(void* arg)
{
    log_init();
    log_data_buf.db = DB_Init();
    uint16_t db_sql_count;
    while(running){
        pthread_mutex_lock(&log_data_buf.lock);
        while(log_data_buf.input_ptr->count < 30 && running){
            /*为防止一直没有新的日志到来，设置了一个定时检查写入sql的语句，防止日志在缓冲区持续堆积
            */
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
    db_save_batch(log_data_buf.db, log_data_buf.input_ptr);//防止程序退出时还有没写的日志
    pthread_mutex_unlock(&log_data_buf.lock);
    
    sqlite3_close(log_data_buf.db);
    log_deinit();
    return NULL;

}
void logger_msg_handler(Common_Msg_t* msg)
{

        switch(msg->msg_type){
            case MSG_TYPE_IMAGE:
                //处理图像数据消息
                break;
            case MSG_TYPE_ALARM:
                //处理告警数据消息
                break;
            case MSG_TYPE_LOG:{
                //处理日志数据消息
                Log_Msg_t *data = msg->data ;
                pthread_mutex_lock(&log_data_buf.lock);
                memcpy(&log_data_buf.input_ptr->items[log_data_buf.input_ptr->count] , data , sizeof(Log_Msg_t));
                /*处理逻辑还未完成*/
                log_data_buf.input_ptr->items[log_data_buf.input_ptr->count].timestamp = data->timestamp;
                log_data_buf.input_ptr->count++;
                if(log_data_buf.input_ptr->count > 30){
                    pthread_cond_signal(&log_data_buf.cond);
                }
                pthread_mutex_unlock(&log_data_buf.lock);
                break;
            }
            case MSG_TYPE_COMMAND:{
                //处理命令数据消息
                Command_Msg_t* cmd_msg = (Command_Msg_t*)msg->data;
                pthread_mutex_lock(&log_data_buf.lock);
                log_data_buf.cmd_msg.cmd = cmd_msg->cmd;
                log_data_buf.cmd_msg.src = cmd_msg->src;
                log_data_buf.cmd_msg.type = cmd_msg->type;
                //param json节点，本模块不需要额外的数据，所以不解析
                log_data_buf.cmd_msg.param = NULL;
                if (log_data_buf.cmd_msg.cmd >= 0) {
                    pthread_cond_signal(&log_data_buf.cond);
                }
                pthread_mutex_unlock(&log_data_buf.lock);
                break;
            }
            case MSG_TYPE_BIGDATA:{
                BigData_Msg_t* b_msg = (BigData_Msg_t*)msg->data;
                if(b_msg->status == DONE){
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
void logger_msg_release_handler(Common_Msg_t* msg)
{
    return;
}
void logger_thread_wakeup(void)
{
    pthread_mutex_lock(&log_data_buf.lock);
    pthread_cond_signal(&log_data_buf.cond);
    pthread_mutex_unlock(&log_data_buf.lock);
}
