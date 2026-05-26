#include <stddef.h>
#include "tcp_shared.h"

static Tcp_Shared_Link_t *g_shared_link_ptr = NULL;
static pthread_mutex_t g_shared_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_shared_cond = PTHREAD_COND_INITIALIZER;

/**
 * @brief 【发送端调用】投递初始化好的共享链接指针
 * @param p_link TCP 共享链接指针
 *
 * 广播唤醒等待中的接收线程
 */
void Tcp_Shared_Post_Link(Tcp_Shared_Link_t *p_link)
{
    pthread_mutex_lock(&g_shared_lock);
    g_shared_link_ptr = p_link;
    pthread_cond_broadcast(&g_shared_cond);
    pthread_mutex_unlock(&g_shared_lock);
}

/**
 * @brief 【接收端调用】阻塞等待，直到拿到共享链接指针
 * @return TCP 共享链接指针
 */
Tcp_Shared_Link_t* Tcp_Shared_Wait_Link(void)
{
    pthread_mutex_lock(&g_shared_lock);
    while (g_shared_link_ptr == NULL) {
        pthread_cond_wait(&g_shared_cond, &g_shared_lock);
    }
    Tcp_Shared_Link_t *p = g_shared_link_ptr;
    pthread_mutex_unlock(&g_shared_lock);
    return p;
}