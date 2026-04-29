#include "sqlite3.h"
#include "pthread.h"
#include "common.h"
#include "cJSON.h"
#include <stdlib.h>
#define MAX_INPUT_LOG_COUNT 50
#define MAX_OUTPUT_LOG_COUNT 100
typedef struct{
    Log_Msg_t *log_msg[MAX_INPUT_LOG_COUNT];
    uint16_t count;
    pthread_mutex_t lock_input, lock_output;
    pthread_cond_t cond_input, cond_output;
}LOG_INPUT_DATA_BUF;
typedef struct{
    Log_Msg_t *log_msg[MAX_OUTPUT_LOG_COUNT];
    uint16_t count;
    pthread_mutex_t lock_input, lock_output;
    pthread_cond_t cond_input, cond_output;
    
}LOG_OUTPUT_DATA_BUF;
//声明输入日志队列
static LOG_INPUT_DATA_BUF log_data_buf;
//声明输出日志队列
//static 
static void log_init(void)
{
    for(int i = 0; i < MAX_INPUT_LOG_COUNT; i++){
        /*初始化input队列*/
        log_data_buf.log_msg[i] = malloc(128);
    }
    log_data_buf.count = 0;
    pthread_mutex_init( log_data_buf.lock_input, NULL);
    pthread_cond_init( log_data_buf.cond_input, NULL);
    pthread_mutex_init( log_data_buf.lock_output, NULL);
    pthread_cond_init( log_data_buf.cond_output, NULL);
} 
static log_release(void)
{
    for(int i = 0; i < MAX_INPUT_LOG_COUNT; i++){
        free(log_data_buf.log_msg);
    }
    pthread_mutex_destroy( log_data_buf.lock_input, NULL);
    pthread_cond_destroy( log_data_buf.cond_input, NULL);
    pthread_mutex_destroy( log_data_buf.lock_output, NULL);
    pthread_cond_destroy( log_data_buf.cond_output, NULL);
}
void export_logs_on_demand(int count)
{
    /*如果上位机发送命令要提交日志*/
    /*采用动态的数据链路
    *从数据库采集count条数据，然后再以json的格式组成一个字符串，调用udp/tcp上传日志
    */
}
void logger_msg_handler(Common_Msg_t* msg)
{
    pthread_mutex_lock(log_data_buf.lock_input);
    Log_Msg_t *data = msg->data ;
    memcpy(log_data_buf.log_msg[log_data_buf.count] , data , sizeof(Log_Msg_t));
    /*处理逻辑还未完成*/
    pthread_mutex_unlock(log_data_buf.lock_input);
}