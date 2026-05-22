#ifndef __TCP_SHARED_H
#define __TCP_SHARED_H
#include <pthread.h>
#include <stdbool.h>
typedef struct {
    int Sock;
    bool connected;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} Tcp_Shared_Link_t;

#endif //__TCP_SHARED_H