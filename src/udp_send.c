#include "udp_send.h"
#include "common.h"
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct{
	int Sock;			//UDP套接字
	struct sockaddr_in dest_addr;	//目的地址
	uint32_t current_frame_id; // 帧ID计数器
} Udp_Config;
void send_packet_optimized(int sock, Frame_Header *header, uint8_t *image_data, struct sockaddr_in *dest_addr) {
    struct iovec iov[2];
    struct msghdr msg;

    // 第一块：包头
    iov[0].iov_base = header;
    iov[0].iov_len = sizeof(Frame_Header); // 确保类型名正确

    // 第二块：图像数据分片
    iov[1].iov_base = image_data;
    iov[1].iov_len = header->data_len;

    // 填充 msghdr 结构
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = dest_addr;
    msg.msg_namelen = sizeof(struct sockaddr_in);
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;

    if (sendmsg(sock, &msg, 0) < 0) {
        perror("sendmsg error");
    }
}
void Udp_Init(Udp_Config *udp_config, const char *ip, uint16_t port)
{
	udp_config->Sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (udp_config->Sock < 0) {
		perror("socket");
		exit(-1);
	}
	memset(&udp_config->dest_addr, 0, sizeof(udp_config->dest_addr));
	udp_config->dest_addr.sin_family = AF_INET;
	udp_config->dest_addr.sin_port = htons(port);
	udp_config->dest_addr.sin_addr.s_addr = inet_addr(ip);
	//初始化帧id计数器
	udp_config->current_frame_id = 0;
	int snd_buf = 1024 * 1024; // 设置 1MB 发送缓存
    setsockopt(udp_config->Sock, SOL_SOCKET, SO_SNDBUF, &snd_buf, sizeof(snd_buf));
	printf("UDP Init Success: Target %s:%d\n", ip, port);
    return 0;

}
/*void Udp_Send_Frame(Udp_Config *udp, uint8_t *jpg_data, uint32_t jpg_len) 
{
    const int CHUNK_SIZE = 1400; // 避开以太网 MTU 限制
    uint16_t total_pkgs = (jpg_len + CHUNK_SIZE - 1) / CHUNK_SIZE;
    uint32_t ts = (uint32_t)time(NULL); 

    for (uint16_t i = 0; i < total_pkgs; i++) {
        uint16_t current_chunk = (jpg_len - i * CHUNK_SIZE > CHUNK_SIZE) ? 
                                 CHUNK_SIZE : (jpg_len - i * CHUNK_SIZE);

        Frame_Header hdr;
        hdr.magic = 0xABCD;
        hdr.frame_id = udp->current_frame_id;
        hdr.pkg_cnt = total_pkgs;
        hdr.pkg_id = i;
        hdr.data_len = current_chunk;
        hdr.timestamp = ts;

        send_packet_optimized(udp->Sock, &hdr, jpg_data + (i * CHUNK_SIZE), &udp->dest_addr);
    }
    udp->current_frame_id++; 
}*/
void* udp_send_thread(void *arg)
{
    char* ip_address = (char *)arg;
    Udp_Config udp;
    Udp_Init(&udp, ip_address, 8080);
    while(running){
        /*发送操作配合cond和mutex*/
    }
}
