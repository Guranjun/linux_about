#ifndef __LOG_H
#define __LOG_H
#include "common.h"
#define MAX_LOG_COUNT 50
typedef struct{
    Log_Msg_t items[MAX_LOG_COUNT];
    int count;
}Log_Buffer_t;
void* logger_process_thread(void* arg);
void logger_thread_wakeup(void);
void export_logs_on_demand(int cp_fd);
#endif // __LOG_H