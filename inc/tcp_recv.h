#ifndef __TCP_RECV_H
#define __TCP_RECV_H

void* tcp_recv_thread(void* arg);
void tcp_recv_msg_handler(Common_Msg_t* msg);
void tcp_recv_msg_release_handler(Common_Msg_t* msg);
void tcp_recv_thread_wakeup(void);

#endif //__TCP_RECV_H
