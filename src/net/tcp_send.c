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

#include "common.h"
#include "my_time.h"
#include "tcp_send.h"
#include "msg_about.h"

#define TCP_SEND_PORT 8080
#define TCP_CHUNK_SIZE 1400U
#define TCP_SEND_BUFFER_INIT_SIZE (256U * 1024U)
typedef enum{
    TCP_DATA_NORMAL = 0,
    TCP_DATA_BIG
} TCP_DATA_TYPE;
typedef struct {
    int Sock;
    struct sockaddr_in dest_addr;
    uint32_t current_frame_id;
    bool connected;
    bool is_sending;
    FILE_TYPE type;
    TCP_DATA_TYPE data_type;
    uint8_t *send_buf;
    uint32_t send_buf_len;
    uint32_t send_buf_capacity;
    BigData_Msg_t* p_bigdata_msg;
    Module_ID_e src_module;
    Log_Msg_t log_msg;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} Tcp_Data_Buffer;

static Tcp_Data_Buffer tcp_data_buffer;

static void Tcp_Close_Socket(Tcp_Data_Buffer *tcp_config)
{
    if (tcp_config->Sock >= 0) {
        close(tcp_config->Sock);
        tcp_config->Sock = -1;
    }
    tcp_config->connected = false;
}

static int Tcp_Open_And_Connect(Tcp_Data_Buffer *tcp_config)
{
    int send_buf_size = 1024 * 1024;

    Tcp_Close_Socket(tcp_config);

    tcp_config->Sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_config->Sock < 0) {
        perror("tcp socket");
        return -1;
    }

    setsockopt(tcp_config->Sock, SOL_SOCKET, SO_SNDBUF, &send_buf_size, sizeof(send_buf_size));

    if (connect(tcp_config->Sock,
                (struct sockaddr *)&tcp_config->dest_addr,
                sizeof(tcp_config->dest_addr)) < 0) {
        perror("tcp connect");
        Tcp_Close_Socket(tcp_config);
        return -1;
    }

    tcp_config->connected = true;
    return 0;
}

static int Tcp_Init(Tcp_Data_Buffer *tcp_config, const char *ip, uint16_t port)
{
    memset(tcp_config, 0, sizeof(*tcp_config));
    memset(&tcp_config->log_msg, 0, sizeof(tcp_config->log_msg));
    tcp_config->p_bigdata_msg = NULL;
    tcp_config->Sock = -1;
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

    pthread_mutex_init(&tcp_config->lock, NULL);
    pthread_cond_init(&tcp_config->cond, NULL);

    if (Tcp_Open_And_Connect(tcp_config) == 0) {
        printf("TCP Init Success: Target %s:%d\n", ip, port);
    } else {
        printf("TCP Init Warning: connect %s:%d failed, will retry on send\n", ip, port);
    }

    return 0;
}

static void Tcp_Deinit(void)
{
    free(tcp_data_buffer.send_buf);
    tcp_data_buffer.send_buf = NULL;
    tcp_data_buffer.send_buf_capacity = 0;
    pthread_mutex_destroy(&tcp_data_buffer.lock);
    pthread_cond_destroy(&tcp_data_buffer.cond);
    Tcp_Close_Socket(&tcp_data_buffer);
}

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

        if (Tcp_Send_Packet(tcp->Sock, &hdr, send_data + offset) != 0) {
            return -1;
        }
    }

    //++tcp->current_frame_id;
    return 0;
}

void *tcp_send_thread(void *arg)
{
    char *ip_address = (char *)arg;

    if (Tcp_Init(&tcp_data_buffer, ip_address, TCP_SEND_PORT) != 0) {
        return NULL;
    }

    while (running) {
        uint32_t frame_len;
        uint8_t *data_to_send;
        FILE_TYPE current_file_type;
        TCP_DATA_TYPE current_data_type;
        Module_ID_e target_module = MODULE_ID_MAX;
        pthread_mutex_lock(&tcp_data_buffer.lock);
        while ((!tcp_data_buffer.is_sending) && running) {
            pthread_cond_wait(&tcp_data_buffer.cond, &tcp_data_buffer.lock);
        }
        if (!running) {
            pthread_mutex_unlock(&tcp_data_buffer.lock);
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
        pthread_mutex_unlock(&tcp_data_buffer.lock);

        if (!tcp_data_buffer.connected) {
            Tcp_Open_And_Connect(&tcp_data_buffer);
        }
        bool send_success = false;
        if (tcp_data_buffer.connected) {
            if (Tcp_Send_Frame(&tcp_data_buffer, data_to_send, frame_len, current_file_type) == 0) {
                send_success = true;
            }
        }

        pthread_mutex_lock(&tcp_data_buffer.lock);
        if (send_success) {
            tcp_data_buffer.current_frame_id++;
        }
        if (current_data_type == TCP_DATA_BIG) {
            tcp_data_buffer.p_bigdata_msg->status = send_success ? DONE : ERROR;
            msg_dispatch(MODULE_ID_TCP, 
                         target_module, 
                         tcp_data_buffer.p_bigdata_msg->total_len, 
                         MSG_TYPE_BIGDATA, 
                         tcp_data_buffer.p_bigdata_msg);
            tcp_data_buffer.p_bigdata_msg = NULL;
        }
        tcp_data_buffer.is_sending = false;
        pthread_mutex_unlock(&tcp_data_buffer.lock);
    }

    Tcp_Deinit();
    return NULL;
}

void tcp_msg_handler(Common_Msg_t *msg)
{
    switch (msg->msg_type) {
        case MSG_TYPE_IMAGE: {
            Image_Data *img_data = (Image_Data *)msg->data;

            if (img_data == NULL || img_data->data == NULL || img_data->len == 0U) {
                break;
            }

            pthread_mutex_lock(&tcp_data_buffer.lock);
            if (!tcp_data_buffer.is_sending) {
                if (Tcp_Ensure_Send_Buffer(&tcp_data_buffer, img_data->len) == 0) {
                    tcp_data_buffer.data_type = TCP_DATA_NORMAL;
                    tcp_data_buffer.send_buf_len = img_data->len;
                    memcpy(tcp_data_buffer.send_buf, img_data->data, img_data->len);
                    tcp_data_buffer.type = NORMAL;
                    tcp_data_buffer.is_sending = true;
                    pthread_cond_signal(&tcp_data_buffer.cond);
                }
            }
            pthread_mutex_unlock(&tcp_data_buffer.lock);
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
            pthread_mutex_lock(&tcp_data_buffer.lock);
            if(!tcp_data_buffer.is_sending) {
                tcp_data_buffer.data_type = TCP_DATA_BIG;
                tcp_data_buffer.p_bigdata_msg = big_msg;
                tcp_data_buffer.is_sending = true;
                tcp_data_buffer.src_module = msg->src_module;
                tcp_data_buffer.type = big_msg->type;
                pthread_cond_signal(&tcp_data_buffer.cond);
                //msg->src_module = MODULE_ID_TCP;
                pthread_mutex_unlock(&tcp_data_buffer.lock);
            }
            else {
                //申请重发
                //memset(&tcp_config->bigdata_msg, 0, sizeof(tcp_config->bigdata_msg));
                pthread_mutex_unlock(&tcp_data_buffer.lock);
                big_msg->status = RESEND;
                msg_dispatch(MODULE_ID_TCP, msg->src_module, big_msg->total_len, MSG_TYPE_BIGDATA, big_msg);
            }
            
            break;
        }
        default:
            break;
    }
}

void tcp_thread_wakeup(void)
{
    pthread_mutex_lock(&tcp_data_buffer.lock);
    pthread_cond_signal(&tcp_data_buffer.cond);
    pthread_mutex_unlock(&tcp_data_buffer.lock);
}
