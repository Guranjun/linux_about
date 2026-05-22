#include <stddef.h>
#include "tcp_shared.h"
static Tcp_Shared_Link_t *g_shared_link_ptr = NULL;
static pthread_mutex_t g_shared_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_shared_cond = PTHREAD_COND_INITIALIZER;

// 【发送端调用】投递初始化好的指针
void Tcp_Shared_Post_Link(Tcp_Shared_Link_t *p_link)
{
    pthread_mutex_lock(&g_shared_lock);
    g_shared_link_ptr = p_link;
    // 唤醒可能正在苦苦等待指针的接收线程
    pthread_cond_broadcast(&g_shared_cond);
    pthread_mutex_unlock(&g_shared_lock);
}

// 【接收端调用】阻塞等待，直到拿到指针
Tcp_Shared_Link_t* Tcp_Shared_Wait_Link(void)
{
    pthread_mutex_lock(&g_shared_lock);
    // 如果发送端还没投递（指针为 NULL），接收线程就在这里安全地睡着，绝不盲目往下跑
    while (g_shared_link_ptr == NULL) {
        pthread_cond_wait(&g_shared_cond, &g_shared_lock);
    }
    Tcp_Shared_Link_t *p = g_shared_link_ptr;
    pthread_mutex_unlock(&g_shared_lock);
    return p;
}