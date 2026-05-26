#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/uio.h>

#include <pthread.h>
#include "common.h"
#include "udp_send.h"
#include "my_time.h"

/**
 * @brief UDP 发送线程私有数据结构体
 */
typedef struct{
	int Sock;                   /**< UDP 套接字 */
	struct sockaddr_in dest_addr;   /**< 目的地址 */
	uint32_t current_frame_id;  /**< 帧 ID 计数器 */
    bool is_sending;            /**< 是否有新数据需要发送 */
    unsigned char* send_buf;    /**< 发送缓冲区 */
    uint32_t send_buf_len;      /**< 发送缓冲区当前数据长度 */
    Common_Msg_t msg;
    Log_Msg_t log_msg;
    pthread_mutex_t lock;       /**< 互斥锁 */
    pthread_cond_t cond;        /**< 条件变量 */
} UDP_Send_Buffer;

static UDP_Send_Buffer udp_send_buffer;

/**
 * @brief 使用 sendmsg 零拷贝方式发送 UDP 数据包
 * @param sock      UDP 套接字
 * @param header    帧头
 * @param data      数据载荷
 * @param dest_addr 目标地址
 */
static void send_packet_optimized(int sock, Frame_Header *header, uint8_t *data, struct sockaddr_in *dest_addr) {
    struct iovec iov[2];
    struct msghdr msg;

    iov[0].iov_base = header;
    iov[0].iov_len = sizeof(Frame_Header);

    iov[1].iov_base = data;
    iov[1].iov_len = header->data_len;

    memset(&msg, 0, sizeof(msg));
    msg.msg_name = dest_addr;
    msg.msg_namelen = sizeof(struct sockaddr_in);
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;

    if (sendmsg(sock, &msg, 0) < 0) {
        perror("sendmsg error");
    }
}

/**
 * @brief 初始化 UDP 发送模块
 * @param udp_config UDP 配置指针
 * @param ip         目标 IP
 * @param port       目标端口
 * @return 0=成功
 */
static int Udp_Init(UDP_Send_Buffer *udp_config, const char *ip, uint16_t port)
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
    memset(&udp_send_buffer.log_msg, 0, sizeof(udp_send_buffer.log_msg));
    memset(&udp_send_buffer.msg, 0, sizeof(udp_send_buffer.msg));
	udp_config->current_frame_id = 0;
	int snd_buf = 1024 * 1024;
    setsockopt(udp_config->Sock, SOL_SOCKET, SO_SNDBUF, &snd_buf, sizeof(snd_buf));
	printf("UDP Init Success: Target %s:%d\n", ip, port);
    udp_config->is_sending = false;
    udp_config->send_buf = malloc(1024 * 256);
    udp_config->send_buf_len = 0;
    pthread_mutex_init(&udp_config->lock, NULL);
    pthread_cond_init(&udp_config->cond, NULL);
    return 0;
}

/**
 * @brief 释放 UDP 发送模块资源
 */
static void Udp_Release(void)
{
    free(udp_send_buffer.send_buf);
    msg_unregister_module(MODULE_ID_UDP);
    pthread_mutex_destroy(&udp_send_buffer.lock);
    pthread_cond_destroy(&udp_send_buffer.cond);
    close(udp_send_buffer.Sock);
}

/**
 * @brief 将数据分帧通过 UDP 发送（自动分包）
 * @param udp       UDP 配置指针
 * @param send_data 要发送的数据
 * @param send_len  数据长度
 */
static void Udp_Send_Frame(UDP_Send_Buffer *udp, uint8_t *send_data, uint32_t send_len) 
{
    const int CHUNK_SIZE = 1400;
    uint16_t total_pkgs = (send_len + CHUNK_SIZE - 1) / CHUNK_SIZE;
    uint64_t ts = (uint64_t)gettime_us(); 

    for (uint16_t i = 0; i < total_pkgs; i++) {
        if(!running){
            break;
        }
        uint16_t current_chunk = (send_len - i * CHUNK_SIZE > CHUNK_SIZE) ? 
                                 CHUNK_SIZE : (send_len - i * CHUNK_SIZE);

        Frame_Header hdr;
        hdr.magic = 0xABCD;
        hdr.frame_id = udp->current_frame_id;
        hdr.pkg_cnt = total_pkgs;
        hdr.type = IMAGE;
        hdr.pkg_id = i;
        hdr.data_len = current_chunk;
        hdr.timestamp = ts;
        hdr.reserved1 = 0;
        hdr.reserved2 = 0;
        hdr.crc = crc16_ccitt((uint8_t *)&hdr, sizeof(Frame_Header) - 2);
        send_packet_optimized(udp->Sock, &hdr, send_data + (i * CHUNK_SIZE), &udp->dest_addr);
    }
    udp->current_frame_id++; 
}

/**
 * @brief UDP 发送线程入口
 * @param arg 传入参数（目标 IP 字符串）
 * @return NULL
 *
 * 等待条件变量，收到图像数据后通过 UDP 分帧发送
 */
void* udp_send_thread(void *arg)
{
    char* ip_address = (char *)arg;
    Udp_Init(&udp_send_buffer, ip_address, 8080);
    while(running){
        pthread_mutex_lock(&udp_send_buffer.lock);
        while((!udp_send_buffer.is_sending) && running){
            pthread_cond_wait(&udp_send_buffer.cond, &udp_send_buffer.lock);
        }
        if(!running){
            pthread_mutex_unlock(&udp_send_buffer.lock);
            break;
        }
        udp_send_buffer.is_sending = true;
        uint32_t frame_len = udp_send_buffer.send_buf_len;
        uint8_t* data_to_send = udp_send_buffer.send_buf;
        pthread_mutex_unlock(&udp_send_buffer.lock);

        Udp_Send_Frame(&udp_send_buffer, data_to_send, frame_len);

        pthread_mutex_lock(&udp_send_buffer.lock);
        udp_send_buffer.is_sending = false;
        pthread_mutex_unlock(&udp_send_buffer.lock);
    }
    Udp_Release();
    return NULL;
}

/**
 * @brief UDP 发送模块的消息处理函数
 * @param msg 接收到的消息
 *
 * 支持 MSG_TYPE_IMAGE：将图像数据拷贝到发送缓冲区并通知线程
 */
void udp_msg_handler(Common_Msg_t* msg)
{
    switch(msg->msg_type){
        case MSG_TYPE_IMAGE:
            pthread_mutex_lock(&udp_send_buffer.lock);
            if(udp_send_buffer.is_sending){
            }
            else{
                Image_Data* img_data = (Image_Data*)msg->data;
                udp_send_buffer.send_buf_len = img_data->len;
                memcpy(udp_send_buffer.send_buf, img_data->data, img_data->len);
                udp_send_buffer.is_sending = true;
                pthread_cond_signal(&udp_send_buffer.cond);
            }
            pthread_mutex_unlock(&udp_send_buffer.lock);
            break;
        case MSG_TYPE_ALARM:
        case MSG_TYPE_LOG:
        case MSG_TYPE_COMMAND:
        case MSG_TYPE_BIGDATA:
        default:
            break;
    }
}

/**
 * @brief 唤醒 UDP 发送线程（用于程序退出时）
 */
void udp_thread_wakeup(void)
{
    pthread_mutex_lock(&udp_send_buffer.lock);
    pthread_cond_signal(&udp_send_buffer.cond);
    pthread_mutex_unlock(&udp_send_buffer.lock);
}