#ifndef __COMMAND_GET_H
#define __COMMAND_GET_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

void* command_process_thread(void* arg);
void  command_thread_wakeup(void);

#ifdef __cplusplus
}
#endif

#endif //__COMMAND_GET_H