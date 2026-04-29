#ifndef __UDP_SEND_H
#define __UDP_SEND_H
#include <stdint.h>
//typedef struct Udp_Config Udp_Config;

void* udp_send_thread(void *arg);
void udp_thread_wakeup(void);
#endif