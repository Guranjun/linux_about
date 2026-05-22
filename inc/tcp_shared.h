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
void Tcp_Shared_Post_Link(Tcp_Shared_Link_t *p_link);
Tcp_Shared_Link_t* Tcp_Shared_Wait_Link(void);

#endif //__TCP_SHARED_H