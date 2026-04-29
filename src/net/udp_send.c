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


typedef struct{
	int Sock;			//UDP套接字
	struct sockaddr_in dest_addr;	//目的地址
	uint32_t current_frame_id; // 帧ID计数器
    bool is_sending; //标志位，指示是否有新数据需要发送
    unsigned char* send_buf; // 发送缓冲区，足够大以容纳最大UDP包
    uint32_t send_buf_len; // 发送缓冲区当前数据长度
    pthread_mutex_t lock; //互斥锁，保护数据访问
    pthread_cond_t cond; //条件变量，通知数据更新
} UDP_Send_Buffer; //UDP发送线程私有数据结构体定义
static UDP_Send_Buffer udp_send_buffer; //UDP发送线程私有数据实例
static void send_packet_optimized(int sock, Frame_Header *header, uint8_t *image_data, struct sockaddr_in *dest_addr) {
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
	//初始化帧id计数器
	udp_config->current_frame_id = 0;
	int snd_buf = 1024 * 1024; // 设置 1MB 发送缓存
    setsockopt(udp_config->Sock, SOL_SOCKET, SO_SNDBUF, &snd_buf, sizeof(snd_buf));
	printf("UDP Init Success: Target %s:%d\n", ip, port);
    udp_config->is_sending = false; //初始状态没有数据需要发送
    udp_config->send_buf = malloc(1024 * 256); // 分配足够大的发送缓冲区
    udp_config->send_buf_len = 0;
    pthread_mutex_init(&udp_config->lock, NULL);
    pthread_cond_init(&udp_config->cond, NULL);
    return 0;
}
static void Udp_Send_Frame(UDP_Send_Buffer *udp, uint8_t *send_data, uint32_t send_len) 
{
    const int CHUNK_SIZE = 1400; // 避开以太网 MTU 限制
    uint16_t total_pkgs = (send_len + CHUNK_SIZE - 1) / CHUNK_SIZE;
    uint32_t ts = (uint32_t)time(NULL); 

    for (uint16_t i = 0; i < total_pkgs; i++) {
        if(!running){
            break; // 如果应用正在停止，提前退出循环
        }
        uint16_t current_chunk = (send_len - i * CHUNK_SIZE > CHUNK_SIZE) ? 
                                 CHUNK_SIZE : (send_len - i * CHUNK_SIZE);

        Frame_Header hdr;
        hdr.magic = 0xABCD;
        hdr.frame_id = udp->current_frame_id;
        hdr.pkg_cnt = total_pkgs;
        hdr.pkg_id = i;
        hdr.data_len = current_chunk;
        hdr.timestamp = ts;

        send_packet_optimized(udp->Sock, &hdr, send_data + (i * CHUNK_SIZE), &udp->dest_addr);
    }
    udp->current_frame_id++; 
}
void* udp_send_thread(void *arg)
{
    char* ip_address = (char *)arg;
    Udp_Init(&udp_send_buffer, ip_address, 8080);
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
       
        pthread_mutex_lock(&udp_send_buffer.lock);
        while((!udp_send_buffer.is_sending) && running){
            pthread_cond_wait(&udp_send_buffer.cond, &udp_send_buffer.lock);
        }
        /*第二步*/
        udp_send_buffer.is_sending = true; //重置标志位
        uint32_t frame_len = udp_send_buffer.send_buf_len;
        uint8_t* data_to_send = udp_send_buffer.send_buf;
        pthread_mutex_unlock(&udp_send_buffer.lock);
        /*第三步*/
        Udp_Send_Frame(&udp_send_buffer, data_to_send, frame_len);
        /*第四步*/
        pthread_mutex_lock(&udp_send_buffer.lock);
        udp_send_buffer.is_sending = false; //重置标志位，表示数据已经发送完毕
        pthread_mutex_unlock(&udp_send_buffer.lock);
    }
    close(udp_send_buffer.Sock);
    return NULL;
}
void udp_msg_handler(Common_Msg_t* msg)
{
    //根据消息类型处理消息数据
    switch(msg->msg_type){
        case MSG_TYPE_IMAGE:
            //处理图像数据消息
            pthread_mutex_lock(&udp_send_buffer.lock);
            if(udp_send_buffer.is_sending){
                //如果上一次数据还没有发送完毕，可以选择丢弃新数据或者覆盖旧数据，这里选择覆盖旧数据
               // printf("Warning: Previous frame data is still being sent, new frame data will overwrite it.\n");
            }
            else{
                Image_Data* img_data = (Image_Data*)msg->data;
                udp_send_buffer.send_buf_len = img_data->len;
                memcpy(udp_send_buffer.send_buf, img_data->data, img_data->len);
                udp_send_buffer.is_sending = true; //设置标志位，表示有新数据需要发送
                pthread_cond_signal(&udp_send_buffer.cond); //通知UDP发送线程有新数据
            }
            pthread_mutex_unlock(&udp_send_buffer.lock);
            break;
        case MSG_TYPE_ALARM:
            //处理告警数据消息
            break;
        case MSG_TYPE_LOG:
            //处理日志数据消息
            break;
        case MSG_TYPE_COMMAND:
            //处理命令数据消息
            break;
        default:
            break;
    }
}
void udp_thread_wakeup(void)
{
    pthread_mutex_lock(&udp_send_buffer.lock);
    pthread_cond_signal(&udp_send_buffer.cond);
    pthread_mutex_unlock(&udp_send_buffer.lock);
}