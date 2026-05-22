#ifndef __TCP_SEND_H
#define __TCP_SEND_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

void *tcp_send_thread(void *arg);
void tcp_send_msg_handler(Common_Msg_t *msg);
void tcp_send_thread_wakeup(void);

#ifdef __cplusplus
}
#endif

#endif // __TCP_SEND_H
