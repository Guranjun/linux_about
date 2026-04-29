#ifndef __MSG_DELIVER_H
#define __MSG_DELIVER_H
#include "common.h"
#ifdef __cplusplus
extern "C"{
#endif

typedef struct{
    Module_ID_e mod_id; //模块ID
    MsgHandler_t handler; //消息处理函数指针
    MsgReleaseHandler_t release_handler; //消息资源释放函数指针
}MsgRouteTable_t; //消息路由表结构体，具体定义根据实际需求设计

void msg_thread_wakeup(void);
#ifdef __cplusplus
}
#endif

#endif // __MSG_DELIVER_H