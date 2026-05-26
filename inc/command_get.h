#ifndef __COMMAND_GET_H
#define __COMMAND_GET_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 命令处理线程入口
 * @param arg 传入参数（不使用）
 * @return NULL
 *
 * 注册到 MODULE_ID_COMMAND 消息通道，循环等待命令消息并分发处理
 */
void* command_process_thread(void* arg);

/**
 * @brief 唤醒命令处理线程（用于程序退出时）
 */
void  command_thread_wakeup(void);

#ifdef __cplusplus
}
#endif

#endif //__COMMAND_GET_H
