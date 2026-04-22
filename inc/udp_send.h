#ifndef __UDP_SEND_H
#define __UDP_SEND_H
#include <stdint.h>
typedef struct Udp_Config Udp_Config;
int Udp_Init(Udp_Config *udp_config, const char *ip, uint16_t port);
void Udp_Send_Frame(Udp_Config *udp, uint8_t *jpg_data, uint32_t jpg_len);
void* udp_send_thread(void *arg);

#endif