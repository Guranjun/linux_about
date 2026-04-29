#include "common.h"
#include "msg_deliver.h"
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#define MAX_MSG_QUEUE_SIZE 16

typedef struct{
    Common_Msg_t msg_buffer[MAX_MSG_QUEUE_SIZE]; //消息缓冲区，最多存储10条消息
    uint8_t head; //消息队列头索引
    uint8_t tail; //消息队列尾索引
    uint8_t count; //当前消息数量
    pthread_mutex_t lock; //互斥锁，保护消息队列访问
    pthread_cond_t cond; //条件变量，通知消息队列更新
}Msg_Queue_t;

static Msg_Queue_t msg_queue;

/*图像处理模块消息处理相关函数*/
__attribute__((weak)) void V4L2_msg_handler(Common_Msg_t *msg)
{
    (void)msg;
}
__attribute__((weak)) void V4L2_msg_release_handler(Common_Msg_t *msg)
{
    (void)msg;
}
/*UDP模块消息处理相关函数*/
__attribute__((weak)) void udp_msg_handler(Common_Msg_t *msg)
{
    (void)msg;
}
__attribute__((weak)) void udp_msg_release_handler(Common_Msg_t *msg)
{
    (void)msg;
}
/*告警模块消息处理相关函数*/
__attribute__((weak)) void alarm_msg_handler(Common_Msg_t *msg)
{
    (void)msg;
}
__attribute__((weak)) void alarm_msg_release_handler(Common_Msg_t *msg)
{
    (void)msg;
}
/*日志模块消息处理相关函数*/
__attribute__((weak)) void logger_msg_handler(Common_Msg_t *msg)
{
    (void)msg;
}
__attribute__((weak)) void logger_msg_release_handler(Common_Msg_t *msg)
{
    (void)msg;
}
/*存储模块消息处理相关函数*/
__attribute__((weak)) void storage_msg_handler(Common_Msg_t *msg)
{
    (void)msg;
}
__attribute__((weak)) void storage_msg_release_handler(Common_Msg_t *msg)
{
    (void)msg;
}
/*tcp发送模块消息处理相关函数*/
__attribute__((weak)) void tcp_msg_handler(Common_Msg_t *msg)
{
    (void)msg;
}
__attribute__((weak)) void tcp_msg_release_handler(Common_Msg_t *msg)
{
    (void)msg;
}
/*上位机命令模块消息处理相关函数*/
__attribute__((weak)) void command_msg_handler(Common_Msg_t *msg)
{
    (void)msg;
}
__attribute__((weak)) void command_msg_release_handler(Common_Msg_t *msg)
{
    (void)msg;
}

static MsgRouteTable_t msg_route_table[] = {
    {MODULE_ID_V4L2, V4L2_msg_handler, V4L2_msg_release_handler},
    {MODULE_ID_UDP, udp_msg_handler, udp_msg_release_handler},
    {MODULE_ID_ALARM, alarm_msg_handler, alarm_msg_release_handler},
    {MODULE_ID_LOGGER, logger_msg_handler, logger_msg_release_handler},
    {MODULE_ID_STORAGE, storage_msg_handler, storage_msg_release_handler},
    {MODULE_ID_TCP, tcp_msg_handler, tcp_msg_release_handler},
    {MODULE_ID_COMMAND, command_msg_handler, command_msg_release_handler},
};

Common_Msg_t msg_make(Module_ID_e src, Module_ID_e dst, uint32_t len, Msg_Type_e type, void* data)
{
    Common_Msg_t msg;
    msg.src_module = src;
    msg.dst_module = dst;
    msg.data_len = len;
    msg.msg_type = type;
    msg.data = data;
    return msg;
}

void msg_init(void)
{
    msg_queue.head = 0;
    msg_queue.tail = 0;
    msg_queue.count = 0;
    pthread_mutex_init(&msg_queue.lock, NULL);
    pthread_cond_init(&msg_queue.cond, NULL);
}
void msg_send(Common_Msg_t* msg)
{
    pthread_mutex_lock(&msg_queue.lock);
    if(msg_queue.count < MAX_MSG_QUEUE_SIZE){
        msg_queue.msg_buffer[msg_queue.tail] = *msg;
        msg_queue.tail = (msg_queue.tail + 1) % MAX_MSG_QUEUE_SIZE;
        msg_queue.count++;
        pthread_cond_signal(&msg_queue.cond); //通知消息队列有新消息
    }
    pthread_mutex_unlock(&msg_queue.lock);
}
int msg_receive(Common_Msg_t* msg)
{
    pthread_mutex_lock(&msg_queue.lock);
    while(msg_queue.count == 0 && running) {
        pthread_cond_wait(&msg_queue.cond, &msg_queue.lock); //等待新消息到来
    }
    if(!running || msg_queue.count == 0) {
        pthread_mutex_unlock(&msg_queue.lock);
        return -1; //应用正在关闭或没有消息可接收
    }
    *msg = msg_queue.msg_buffer[msg_queue.head];
    msg_queue.head = (msg_queue.head + 1) % MAX_MSG_QUEUE_SIZE;
    msg_queue.count--;
    pthread_mutex_unlock(&msg_queue.lock);
    return 0; //成功接收消息
}
void msg_release(Common_Msg_t* msg)
{
    //根据消息类型释放消息数据资源
    switch(msg->msg_type){
        case MSG_TYPE_IMAGE:
            //释放图像数据资源
            V4L2_msg_release_handler(msg);
            break;
        case MSG_TYPE_ALARM:
            //释放告警数据资源
            alarm_msg_release_handler(msg);
            break;
        case MSG_TYPE_LOG:
            //释放日志数据资源
            logger_msg_release_handler(msg);
            break;
        case MSG_TYPE_COMMAND:
            //释放命令数据资源
            command_msg_release_handler(msg);
            break;
        default:
            break;

    }
}
void msg_cleanup(void)
{
    pthread_mutex_destroy(&msg_queue.lock);
    pthread_cond_destroy(&msg_queue.cond);
}

void* msg_deliver_thread(void* arg)
{
    Common_Msg_t msg;
    while(running){
        if(msg_receive(&msg) == 0){
            /*
            msg_route_table[msg.dst_module].handler(&msg);
            */
            for(int i = 0; i < sizeof(msg_route_table)/sizeof(MsgRouteTable_t); i++){
                if(msg_route_table[i].mod_id == msg.dst_module){
                    msg_route_table[i].handler(&msg);
                    break;
                }
            }
        }
        else{
            //接收消息失败，可能是应用正在关闭，继续循环检查running标志
            continue;
        }
        /*资源回收*/
        msg_release(&msg);
    }
    msg_cleanup();
    return NULL;
}
void msg_thread_wakeup(void)
{
    pthread_mutex_lock(&msg_queue.lock);
    pthread_cond_signal(&msg_queue.cond);
    pthread_mutex_unlock(&msg_queue.lock);
}