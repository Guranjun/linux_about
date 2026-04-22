#include <stdio.h>      // 用于 printf 和 perror
#include <stdlib.h>     // 用于 exit, malloc, free
#include <string.h>     // 用于 memset, memcpy
#include <unistd.h>     // 用于 close() 函数
#include <stdint.h>     // 用于 uint8_t, uint32_t 等标准类型定义
#include <time.h>       // 用于 time() 获取时间戳

/* 网络编程相关 */
#include <sys/types.h>
#include <sys/socket.h> // socket, sendmsg, setsockopt
#include <netinet/in.h> // struct sockaddr_in
#include <arpa/inet.h>  // inet_addr
#include <sys/uio.h>    // 用于 readv/writev 和 struct iovec (sendmsg 依赖)

/* 多线程与工程头文件 */
#include <pthread.h>    // 必须包含，用于互斥锁和条件变量
#include "common.h"     // 包含共享缓冲区结构体定义
#include "udp_send.h"   // 包含 Udp_Config 和 Frame_Header 的定义

struct Udp_Config {
	int Sock;			//UDP套接字
	struct sockaddr_in dest_addr;	//目的地址
	uint32_t current_frame_id; // 帧ID计数器
};
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
int Udp_Init(Udp_Config *udp_config, const char *ip, uint16_t port)
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
void Udp_Send_Frame(Udp_Config *udp, uint8_t *jpg_data, uint32_t jpg_len) 
{
    const int CHUNK_SIZE = 1400; // 避开以太网 MTU 限制
    uint16_t total_pkgs = (jpg_len + CHUNK_SIZE - 1) / CHUNK_SIZE;
    uint32_t ts = (uint32_t)time(NULL); 

    for (uint16_t i = 0; i < total_pkgs; i++) {
        if(!running){
            break; // 如果应用正在停止，提前退出循环
        }
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
}
void* udp_send_thread(void *arg)
{
    char* ip_address = (char *)arg;
    Udp_Config udp;
    Udp_Init(&udp, ip_address, 8080);
    while(running){
        /*发送操作配合cond和mutex
        *第一步：等待条件变量，直到有新数据可发送
        *       使用while循环检查是否有新数据
        *第二步：获取锁，获取标志位和数据指针
        *       释放锁
        *第三步：发送数据
        *第四步：获取锁，重置标志位
        *       释放锁
        */
        /*第一步*/
       
        pthread_mutex_lock(&camera_udp_shared_buffer.lock);
        while((camera_udp_shared_buffer.latest_index == -1 || camera_udp_shared_buffer.is_sending == 1) && running){
            pthread_cond_wait(&camera_udp_shared_buffer.cond, &camera_udp_shared_buffer.lock);
        }
        /*第二步*/
        camera_udp_shared_buffer.is_sending = 1;
        int index_to_send = camera_udp_shared_buffer.latest_index;
        camera_udp_shared_buffer.latest_index = -1; // 重置为-1表示数据已被取走
        uint32_t frame_len = camera_udp_shared_buffer.frame_len[index_to_send];
        uint8_t* data_to_send = camera_udp_shared_buffer.camera_data[index_to_send];
        pthread_mutex_unlock(&camera_udp_shared_buffer.lock);
        /*第三步*/
        Udp_Send_Frame(&udp, data_to_send, frame_len);
        /*第四步*/
        pthread_mutex_lock(&camera_udp_shared_buffer.lock);
        camera_udp_shared_buffer.is_sending = 0;
        pthread_cond_signal(&camera_udp_shared_buffer.cond);
        pthread_mutex_unlock(&camera_udp_shared_buffer.lock);
    }
    close(udp.Sock);
    return NULL;
}
