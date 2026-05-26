#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include "common.h"
#include "my_time.h"
#include "tcp_send.h"
#include "msg_about.h"
#include "tcp_shared.h"
#include "crc.h"
#define TCP_SEND_PORT 8080
#define TCP_CHUNK_SIZE 1400U
#define TCP_SEND_BUFFER_INIT_SIZE (256U * 1024U)

/**
 * @brief TCP 发送数据类型枚举
 */
typedef enum{
    TCP_DATA_NORMAL = 0, /**< 普通数据（从发送缓冲区拷贝） */
    TCP_DATA_BIG         /**< 大数据（零拷贝引用外部数据） */
} TCP_DATA_TYPE;

/**
 * @brief TCP 发送线程私有数据结构体
 */
typedef struct {
    struct sockaddr_in dest_addr;       /**< 目标地址 */
    uint32_t current_frame_id;          /**< 当前帧 ID 计数器 */
    bool is_sending;                    /**< 是否有数据待发送 */
    FILE_TYPE type;                     /**< 发送数据的文件类型 */
    TCP_DATA_TYPE data_type;            /**< 发送数据类型（正常/大数据） */
    uint8_t *send_buf;                  /**< 普通数据发送缓冲区 */
    uint32_t send_buf_len;              /**< 发送缓冲区当前数据长度 */
    uint32_t send_buf_capacity;         /**< 发送缓冲区容量 */
    BigData_Msg_t* p_bigdata_msg;       /**< 大数据消息指针 */
    Module_ID_e src_module;             /**< 数据来源模块 ID */
    Log_Msg_t log_msg;                  /**< 日志消息缓冲区 */
    Tcp_Shared_Link_t link;             /**< 共享链接信息（与 tcp_recv 共享） */
} Tcp_Data_Buffer;

static Tcp_Data_Buffer tcp_data_buffer;

/**
 * @brief 关闭 TCP  socket 并重置连接状态
 * @param tcp_config TCP 配置指针
 */
static void Tcp_Close_Socket(Tcp_Data_Buffer *tcp_config)
{
    if (tcp_config->link.Sock >= 0) {
        close(tcp_config->link.Sock);
        tcp_config->link.Sock = -1;
    }
    tcp_config->link.connected = false;
}

/**
 * @brief 打开 TCP socket 并尝试连接目标服务器
 * @param tcp_config TCP 配置指针
 * @return 0=成功, -1=失败
 *
 * 使用非阻塞 connect + select 超时机制，避免长时间阻塞
 */
static int Tcp_Open_And_Connect(Tcp_Data_Buffer *tcp_config)
{
    int send_buf_size = 1024 * 1024;

    Tcp_Close_Socket(tcp_config);

    tcp_config->link.Sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_config->link.Sock < 0) {
        perror("tcp socket");
        return -1;
    }

    setsockopt(tcp_config->link.Sock, SOL_SOCKET, SO_SNDBUF, &send_buf_size, sizeof(send_buf_size));
    //设为非阻塞连接，因为阻塞连接阻塞时间过长
    int flags = fcntl(tcp_config->link.Sock, F_GETFL, 0);
    if (flags < 0 || fcntl(tcp_config->link.Sock, F_SETFL, flags | O_NONBLOCK) < 0) {
        Tcp_Close_Socket(tcp_config);
        return -1;
    }
    //尝试连接
    int ret = connect(tcp_config->link.Sock, (struct sockaddr *)&tcp_config->dest_addr, sizeof(tcp_config->dest_addr));
    if (ret < 0) {
        if (errno == EINPROGRESS) {
            // 用 select 设置 50ms 超时
            struct timeval tv = {0, 50000}; // 50ms
            fd_set wfd;
            FD_ZERO(&wfd);
            FD_SET(tcp_config->link.Sock, &wfd);

            ret = select(tcp_config->link.Sock + 1, NULL, &wfd, NULL, &tv);
            if (ret <= 0 || !running) {
                Tcp_Close_Socket(tcp_config);
                return -1;
            }

            // 二次确认网络通路
            int err = 0;
            socklen_t len = sizeof(err);
            if (getsockopt(tcp_config->link.Sock, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
                Tcp_Close_Socket(tcp_config);
                return -1;
            }
        } else {
            Tcp_Close_Socket(tcp_config);
            return -1;
        }
    }
    //切换为阻塞连接发送
    if (fcntl(tcp_config->link.Sock, F_SETFL, flags) < 0) {
        Tcp_Close_Socket(tcp_config);
        return -1;
    }

    return 0;
}

/**
 * @brief 初始化 TCP 发送模块
 * @param tcp_config TCP 配置指针
 * @param ip         目标 IP 地址
 * @param port       目标端口
 * @return 0=成功, -1=失败
 *
 * 分配发送缓冲区，初始化互斥锁/条件变量，并尝试首次连接
 */
static int Tcp_Init(Tcp_Data_Buffer *tcp_config, const char *ip, uint16_t port)
{
    memset(tcp_config, 0, sizeof(*tcp_config));
    memset(&tcp_config->log_msg, 0, sizeof(tcp_config->log_msg));
    tcp_config->p_bigdata_msg = NULL;
    tcp_config->link.Sock = -1;
    tcp_config->dest_addr.sin_family = AF_INET;
    tcp_config->dest_addr.sin_port = htons(port);
    tcp_config->dest_addr.sin_addr.s_addr = inet_addr(ip);
    tcp_config->current_frame_id = 0;
    tcp_config->data_type = TCP_DATA_NORMAL;
    tcp_config->type = NORMAL;
    tcp_config->src_module = MODULE_ID_MAX;
    tcp_config->is_sending = false;
    tcp_config->send_buf_capacity = TCP_SEND_BUFFER_INIT_SIZE;
    tcp_config->send_buf = (uint8_t *)malloc(tcp_config->send_buf_capacity);
    tcp_config->send_buf_len = 0;
    if (tcp_config->send_buf == NULL) {
        perror("tcp send buffer malloc");
        return -1;
    }

    pthread_mutex_init(&tcp_config->link.lock, NULL);
    pthread_cond_init(&tcp_config->link.cond, NULL);

    if (Tcp_Open_And_Connect(tcp_config) == 0) {
        printf("TCP Init Success: Target %s:%d\n", ip, port);
        tcp_config->link.connected = true;
    } else {
        printf("TCP Init Warning: connect %s:%d failed, will retry on send\n", ip, port);
    }

    return 0;
}

/**
 * @brief 反初始化 TCP 发送模块，释放资源
 */
static void Tcp_Deinit(void)
{
    free(tcp_data_buffer.send_buf);
    tcp_data_buffer.send_buf = NULL;
    tcp_data_buffer.send_buf_capacity = 0;
    msg_unregister_module(MODULE_ID_TCP_SEND);
    pthread_mutex_destroy(&tcp_data_buffer.link.lock);
    pthread_cond_destroy(&tcp_data_buffer.link.cond);
    Tcp_Close_Socket(&tcp_data_buffer);
}

/**
 * @brief 检查并确保发送缓冲区有足够容量
 * @param tcp_config   TCP 配置指针
 * @param required_len 所需的最小字节数
 * @return 0=足够, -1=不足（建议改用大数据路径）
 */
static int Tcp_Ensure_Send_Buffer(Tcp_Data_Buffer *tcp_config, uint32_t required_len)
{

    if (required_len <= tcp_config->send_buf_capacity) {
        return 0;
    }
    fprintf(stderr,
            "tcp send buffer overflow: required=%u, capacity=%u, use BIGDATA zero-copy path instead\n",
            required_len,
            tcp_config->send_buf_capacity);
    return -1;
    
}

/**
 * @brief 阻塞发送指定长度的数据到 socket
 * @param sock socket 描述符
 * @param buf  数据缓冲区
 * @param len  要发送的字节数
 * @return 0=成功, -1=失败
 */
static int Tcp_Send_All(int sock, const uint8_t *buf, size_t len)
{
    size_t total_sent = 0;

    while (total_sent < len) {
        ssize_t sent;
#ifdef MSG_NOSIGNAL
        sent = send(sock, buf + total_sent, len - total_sent, MSG_NOSIGNAL);
#else
        sent = send(sock, buf + total_sent, len - total_sent, 0);
#endif
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (sent == 0) {
            return -1;
        }
        total_sent += (size_t)sent;
    }

    return 0;
}

/**
 * @brief 发送一个数据包（帧头 + 载荷）
 * @param sock   socket 描述符
 * @param header 帧头指针
 * @param data   载荷数据指针
 * @return 0=成功, -1=失败
 */
static int Tcp_Send_Packet(int sock, const Frame_Header *header, const uint8_t *data)
{
    if (Tcp_Send_All(sock, (const uint8_t *)header, sizeof(*header)) != 0) {
        return -1;
    }

    if (header->data_len == 0U) {
        return 0;
    }

    return Tcp_Send_All(sock, data, header->data_len);
}

/**
 * @brief 将数据分帧发送（自动分包）
 * @param tcp        TCP 配置指针
 * @param send_data  要发送的数据
 * @param send_len   数据长度
 * @param type       文件类型
 * @return 0=成功, -1=失败
 *
 * 数据按 TCP_CHUNK_SIZE (1400 字节) 分包，每包添加 32 字节帧头
 */
static int Tcp_Send_Frame(Tcp_Data_Buffer *tcp, const uint8_t *send_data, uint32_t send_len, FILE_TYPE type)
{
    uint16_t total_pkgs;
    uint64_t ts;
    uint16_t i;

    if (send_len == 0U) {
        return 0;
    }

    total_pkgs = (uint16_t)((send_len + TCP_CHUNK_SIZE - 1U) / TCP_CHUNK_SIZE);
    ts = (uint64_t)gettime_us();

    for (i = 0; i < total_pkgs; ++i) {
        uint32_t offset = (uint32_t)i * TCP_CHUNK_SIZE;
        uint16_t current_chunk = (uint16_t)((send_len - offset > TCP_CHUNK_SIZE) ?
                                 TCP_CHUNK_SIZE : (send_len - offset));
        Frame_Header hdr;

        if (!running) {
            return -1;
        }

        hdr.magic = 0xABCD;
        hdr.frame_id = tcp->current_frame_id;
        hdr.pkg_cnt = total_pkgs;
        hdr.pkg_id = i;
        hdr.type = type;
        hdr.data_len = current_chunk;
        hdr.timestamp = ts;
        hdr.reserved1 = 0;
        hdr.reserved2 = 0;
        hdr.crc = crc16_ccitt((uint8_t *)&hdr, sizeof(Frame_Header) - 2);
        if (Tcp_Send_Packet(tcp->link.Sock, &hdr, send_data + offset) != 0) {
            return -1;
        }
    }

    //++tcp->current_frame_id;
    return 0;
}

/**
 * @brief TCP 发送线程入口
 * @param arg 传入参数（目标 IP 地址字符串）
 * @return NULL
 *
 * 线程流程：
 * 1. 初始化 TCP 连接
 * 2. 等待条件变量（有数据发送或 5s 心跳超时）
 * 3. 连接断开时主动重连
 * 4. 发送数据帧
 * 5. 大数据发送完成后通知来源模块
 */
void *tcp_send_thread(void *arg)
{
    char *ip_address = (char *)arg;

    if (Tcp_Init(&tcp_data_buffer, ip_address, TCP_SEND_PORT) != 0) {
        return NULL;
    }
    Tcp_Shared_Post_Link(&tcp_data_buffer.link);
    while (running) {
        uint32_t frame_len;
        uint8_t *data_to_send;
        FILE_TYPE current_file_type;
        TCP_DATA_TYPE current_data_type;
        Module_ID_e target_module = MODULE_ID_MAX;

        pthread_mutex_lock(&tcp_data_buffer.link.lock);
        while ((!tcp_data_buffer.is_sending) && running) {
            /* 每 5 秒超时唤醒一次，用于空闲时重连检测 */
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 5;
            int ret = pthread_cond_timedwait(&tcp_data_buffer.link.cond,
                                              &tcp_data_buffer.link.lock, &ts);
            if (ret == ETIMEDOUT && !tcp_data_buffer.link.connected && running) {
                /* 超时且连接已断开，退出 while 执行重连逻辑 */
                break;
            }
        }
        if (!running) {
            pthread_mutex_unlock(&tcp_data_buffer.link.lock);
            break;
        }
        current_data_type = tcp_data_buffer.data_type;
        current_file_type = tcp_data_buffer.type;
        target_module = tcp_data_buffer.src_module;

        if(current_data_type == TCP_DATA_NORMAL){
            frame_len = tcp_data_buffer.send_buf_len;
            data_to_send = tcp_data_buffer.send_buf;
        }
        else{
            frame_len = tcp_data_buffer.p_bigdata_msg->total_len;
            data_to_send = tcp_data_buffer.p_bigdata_msg->data_ptr;
        }
        bool is_conn = tcp_data_buffer.link.connected; // 锁内读取当前状态
        pthread_mutex_unlock(&tcp_data_buffer.link.lock);

        bool send_success = false;

        if (!is_conn) {
            /* 连接断开时尝试重连（无论是否有数据要发） */
            if (Tcp_Open_And_Connect(&tcp_data_buffer) == 0) {
                pthread_mutex_lock(&tcp_data_buffer.link.lock);
                tcp_data_buffer.link.connected = true;
                pthread_cond_broadcast(&tcp_data_buffer.link.cond);
                pthread_mutex_unlock(&tcp_data_buffer.link.lock);
                is_conn = true;
                printf("TCP reconnected successfully\n");
            } else if (running) {
                /* 重连失败，回 cond_wait 继续等待 */
                continue;
            }
        }

        /* 如果是心跳超时唤醒但没有实际数据，不发送，回 cond_wait */
        if (!tcp_data_buffer.is_sending && running) {
            continue;
        }

        if (is_conn) {
            if (Tcp_Send_Frame(&tcp_data_buffer, data_to_send, frame_len, current_file_type) == 0) {
                send_success = true;
            } else {
                printf("Send file type %d failed, disconnected\n", current_data_type);
                pthread_mutex_lock(&tcp_data_buffer.link.lock);
                tcp_data_buffer.link.connected = false; // 锁内安全标记断线
                pthread_mutex_unlock(&tcp_data_buffer.link.lock);
                send_success = false;
            }
        } 
        else {
            //连接失败
            send_success = false;
        }
        
        pthread_mutex_lock(&tcp_data_buffer.link.lock);
        if (send_success) {
            tcp_data_buffer.current_frame_id++;
        }
        if (current_data_type == TCP_DATA_BIG) {
            tcp_data_buffer.p_bigdata_msg->status = send_success ? DONE : FILE_DELIVER_ERROR;
            //建议这个操作在锁外执行，引入一个新的指针变量，保存这个p_bigdata_msg的指针值
            msg_dispatch(MODULE_ID_TCP_SEND, 
                         target_module, 
                         tcp_data_buffer.p_bigdata_msg->total_len, 
                         MSG_TYPE_BIGDATA, 
                         tcp_data_buffer.p_bigdata_msg);
            tcp_data_buffer.p_bigdata_msg = NULL;
        }
        tcp_data_buffer.is_sending = false;
        pthread_mutex_unlock(&tcp_data_buffer.link.lock);
    }

    Tcp_Deinit();
    return NULL;
}

/**
 * @brief TCP 发送模块的消息处理函数
 * @param msg 接收到的消息
 *
 * 支持的消息类型:
 * - MSG_TYPE_IMAGE: 将图像数据拷贝到发送缓冲区，通知线程发送
 * - MSG_TYPE_BIGDATA: 引用大数据消息，零拷贝发送
 */
void tcp_send_msg_handler(Common_Msg_t *msg)
{
    switch (msg->msg_type) {
        case MSG_TYPE_IMAGE: {
            Image_Data *img_data = (Image_Data *)msg->data;

            if (img_data == NULL || img_data->data == NULL || img_data->len == 0U) {
                break;
            }

            pthread_mutex_lock(&tcp_data_buffer.link.lock);
            if (!tcp_data_buffer.is_sending) {
                if (Tcp_Ensure_Send_Buffer(&tcp_data_buffer, img_data->len) == 0) {
                    tcp_data_buffer.data_type = TCP_DATA_NORMAL;
                    tcp_data_buffer.send_buf_len = img_data->len;
                    memcpy(tcp_data_buffer.send_buf, img_data->data, img_data->len);
                    tcp_data_buffer.type = NORMAL;
                    tcp_data_buffer.is_sending = true;
                    pthread_cond_signal(&tcp_data_buffer.link.cond);
                }
            }
            pthread_mutex_unlock(&tcp_data_buffer.link.lock);
            break;
        }
        case MSG_TYPE_ALARM:
        case MSG_TYPE_LOG:
        case MSG_TYPE_COMMAND:
            break;
        case MSG_TYPE_BIGDATA:{
            //这里是大数据文件发送相关操作
            BigData_Msg_t* big_msg = (BigData_Msg_t*)msg->data;
            if(big_msg == NULL || big_msg->data_ptr == NULL || big_msg->total_len == 0U){
                break;
            }
            pthread_mutex_lock(&tcp_data_buffer.link.lock);
            if(!tcp_data_buffer.is_sending) {
                tcp_data_buffer.data_type = TCP_DATA_BIG;
                tcp_data_buffer.p_bigdata_msg = big_msg;
                tcp_data_buffer.is_sending = true;
                tcp_data_buffer.src_module = msg->src_module;
                tcp_data_buffer.type = big_msg->type;
                pthread_cond_signal(&tcp_data_buffer.link.cond);
                //msg->src_module = MODULE_ID_TCP_SEND;
                pthread_mutex_unlock(&tcp_data_buffer.link.lock);
            }
            else {
                //申请重发
                //memset(&tcp_config->bigdata_msg, 0, sizeof(tcp_config->bigdata_msg));
                pthread_mutex_unlock(&tcp_data_buffer.link.lock);
                big_msg->status = RESEND;
                msg_dispatch(MODULE_ID_TCP_SEND, msg->src_module, big_msg->total_len, MSG_TYPE_BIGDATA, big_msg);
            }
            
            break;
        }
        default:
            break;
    }
}

/**
 * @brief 唤醒 TCP 发送线程（用于程序退出时）
 */
void tcp_send_thread_wakeup(void)
{
    pthread_mutex_lock(&tcp_data_buffer.link.lock);
    pthread_cond_signal(&tcp_data_buffer.link.cond);
    pthread_mutex_unlock(&tcp_data_buffer.link.lock);
}