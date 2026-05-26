#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "common.h"
#include "tcp_recv.h"
#include "tcp_shared.h"
#include "msg_about.h"
#include "my_time.h"
#include "crc.h"
#include "cJSON.h"
#include "json_about.h"
#include <stdint.h>
#include <stdbool.h>

#define RECV_BUFFER_SIZE 512
#define MAX_FRAME_DATA_LEN (1 * 512U)
#define CMD_WAIT_TIMEOUT_US 2000000ULL  /* 2秒 */
void tcp_recv_cmd_processed(void);
typedef struct{
    Tcp_Shared_Link_t* link;
    cJSON* json_root;
    Command_Msg_t cmd_msg;
    bool cmd_msg_received;
    pthread_mutex_t cmd_lock;
    pthread_cond_t cmd_cond;
    uint8_t recv_buffer[RECV_BUFFER_SIZE];
    uint32_t recv_buffer_len;
    uint64_t last_cmd_ts;
    Log_Msg_t log_msg;
}Tcp_Recv_Buffer;

static Tcp_Recv_Buffer tcp_recv_buffer;

static void Tcp_Recv_Init(void)
{
    memset(&tcp_recv_buffer, 0, sizeof(tcp_recv_buffer));
    tcp_recv_buffer.json_root = NULL;
    tcp_recv_buffer.link = NULL;
    tcp_recv_buffer.cmd_msg_received = false;
    tcp_recv_buffer.recv_buffer_len = 0;
    tcp_recv_buffer.last_cmd_ts = gettime_us();
    pthread_mutex_init(&tcp_recv_buffer.cmd_lock, NULL);
    pthread_cond_init(&tcp_recv_buffer.cmd_cond, NULL);
}

static void Tcp_Recv_Deinit(void)
{
    msg_unregister_module(MODULE_ID_TCP_RECV);
    tcp_recv_buffer.link = NULL;
    pthread_mutex_destroy(&tcp_recv_buffer.cmd_lock);
    pthread_cond_destroy(&tcp_recv_buffer.cmd_cond);
}

void tcp_recv_msg_handler(Common_Msg_t* msg)
{
    (void)msg;
}

void tcp_recv_msg_release_handler(Common_Msg_t* msg)
{
    switch(msg->msg_type){
        case MSG_TYPE_ALARM:{
            break;
        }
        case MSG_TYPE_BIGDATA:{
            break;
        }
        case MSG_TYPE_COMMAND:{
            tcp_recv_cmd_processed();
            break;
        }
        case MSG_TYPE_IMAGE:{
            break;
        }
        case MSG_TYPE_LOG:{
            break;
        }
        default:{
            break;
        }
    }
}

/**
 * @brief 模块内调用，通知上一个指令已处理完毕，可以接收下一个
 */
void tcp_recv_cmd_processed(void)
{
    pthread_mutex_lock(&tcp_recv_buffer.cmd_lock);
    if (tcp_recv_buffer.json_root != NULL) {
        cJSON_Delete(tcp_recv_buffer.json_root);
        tcp_recv_buffer.json_root = NULL;
    }
    tcp_recv_buffer.cmd_msg_received = false;
    pthread_cond_signal(&tcp_recv_buffer.cmd_cond);
    pthread_mutex_unlock(&tcp_recv_buffer.cmd_lock);
}

/**
 * @brief 阻塞等待上一个指令被处理完，超时 CMD_WAIT_TIMEOUT_US 后强制继续
 * @return 0 正常放行, -1 超时强制放行
 */
static int tcp_recv_wait_cmd_ready(void)
{
    uint64_t deadline = gettime_us() + CMD_WAIT_TIMEOUT_US;

    pthread_mutex_lock(&tcp_recv_buffer.cmd_lock);
    while (tcp_recv_buffer.cmd_msg_received && running) {
        uint64_t now = gettime_us();
        if (now >= deadline) {
            /* 超时，强制清除 */
            tcp_recv_buffer.cmd_msg_received = false;
            pthread_mutex_unlock(&tcp_recv_buffer.cmd_lock);
            log_make(&tcp_recv_buffer.log_msg, WARN, gettime_us(),
                     MODULE_ID_TCP_RECV, "Cmd wait timeout, force proceed");
            msg_dispatch(MODULE_ID_TCP_RECV, MODULE_ID_LOGGER, sizeof(tcp_recv_buffer.log_msg), MSG_TYPE_LOG, &tcp_recv_buffer.log_msg);
            if (tcp_recv_buffer.json_root != NULL) {
                cJSON_Delete(tcp_recv_buffer.json_root);
                tcp_recv_buffer.json_root = NULL;
            }
            return -1;
        }

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        uint64_t remain_us = deadline - now;
        if (remain_us > 100000) remain_us = 100000; /* 最多等 100ms，方便 running 中断 */
        ts.tv_nsec += (long)(remain_us * 1000);
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&tcp_recv_buffer.cmd_cond, &tcp_recv_buffer.cmd_lock, &ts);
    }
    pthread_mutex_unlock(&tcp_recv_buffer.cmd_lock);
    return 0;
}

/**
 * @brief 标记指令已发出，阻塞后续新指令直到被处理
 */
static void tcp_recv_mark_cmd_sent(void)
{
    pthread_mutex_lock(&tcp_recv_buffer.cmd_lock);
    tcp_recv_buffer.cmd_msg_received = true;
    pthread_mutex_unlock(&tcp_recv_buffer.cmd_lock);
}

/**
 * @brief 从TCP socket阻塞接收指定长度的数据
 * @param sock   socket描述符
 * @param data   接收缓冲区
 * @param length 期望接收的字节数
 * @return 0成功, -1失败(连接断开或错误)
 */
static int tcp_recv_all(int sock, uint8_t* data, uint32_t length)
{
    uint32_t total_recv = 0;

    while (total_recv < length) {
        ssize_t n = recv(sock, data + total_recv, length - total_recv, 0);
        if (n <= 0) {
            if (n == 0) {
                return -1;
            }
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                if (!running) {
                    return -1;
                }
                continue;
            }
            return -1;
        }
        total_recv += (uint32_t)n;
    }

    return 0;
}

/**
 * @brief 在滑动窗口的指定偏移处尝试匹配 0xAB + 0xCD
 */
static int try_match_header_at(int sock, uint8_t* buf, int idx, int hdr_size)
{
    if (buf[idx] != 0xAB) {
        return 0;
    }

    uint8_t cd_byte;
    if (idx + 1 < hdr_size) {
        cd_byte = buf[idx + 1];
    } else {
        if (tcp_recv_all(sock, &cd_byte, 1) != 0) {
            return -1;
        }
    }

    if (cd_byte != 0xCD) {
        return 0;
    }

    int after_cd_idx = idx + 2;
    int bytes_after_cd = (after_cd_idx < hdr_size) ? (hdr_size - after_cd_idx) : 0;

    if (bytes_after_cd > 0) {
        memmove(buf + 2, buf + after_cd_idx, (size_t)bytes_after_cd);
    }

    int need_read = hdr_size - 2 - bytes_after_cd;
    if (need_read > 0) {
        if (tcp_recv_all(sock, buf + 2 + bytes_after_cd, (uint32_t)need_read) != 0) {
            return -1;
        }
    }

    buf[0] = 0xAB;
    buf[1] = 0xCD;
    ((Frame_Header*)buf)->magic = 0xABCD;

    uint16_t crc = crc16_ccitt(buf, (size_t)(hdr_size - 2));
    if (crc == ((Frame_Header*)buf)->crc) {
        return 1;
    }

    return 0;
}

/**
 * @brief 滑动窗口搜索帧头
 */
static int tcp_search_frame_header(int sock, Frame_Header* out_hdr)
{
    uint8_t* buf = (uint8_t*)out_hdr;
    const int HDR_SIZE = (int)sizeof(Frame_Header);

    while (1) {
        for (int i = 1; i < HDR_SIZE; i++) {
            int ret = try_match_header_at(sock, buf, i, HDR_SIZE);
            if (ret == 1) {
                return 0;
            }
            if (ret == -1) {
                return -1;
            }
        }

        uint8_t new_byte;
        if (tcp_recv_all(sock, &new_byte, 1) != 0) {
            return -1;
        }

        memmove(buf, buf + 1, (size_t)(HDR_SIZE - 1));
        buf[HDR_SIZE - 1] = new_byte;
    }
}

/**
 * @brief 根据帧头 type 字段，通过消息系统分发给对应模块
 */
static void cmd_deliver(Frame_Header* hdr, uint8_t* data)
{
    int ver;

    if (data == NULL || hdr->data_len == 0) {
        return;
    }

    data[hdr->data_len] = '\0';
    tcp_recv_buffer.json_root = cJSON_Parse((const char*)data);
    printf("json parse %s\n", tcp_recv_buffer.json_root ? "OK" : "FAIL");
    if (!tcp_recv_buffer.json_root) {
        printf("error: %s\n", cJSON_GetErrorPtr());
        return;
    }

    /* 校验 ver */
    ver = json_get_int_def(tcp_recv_buffer.json_root, "ver", -1);
    if (ver != 0) {
        log_make(&tcp_recv_buffer.log_msg, WARN, gettime_us(),
                 MODULE_ID_TCP_RECV, "bad ver in cmd json");
        msg_dispatch(MODULE_ID_TCP_RECV, MODULE_ID_LOGGER,
                     sizeof(tcp_recv_buffer.log_msg), MSG_TYPE_LOG, &tcp_recv_buffer.log_msg);
        cJSON_Delete(tcp_recv_buffer.json_root);
        tcp_recv_buffer.json_root = NULL;
        return;
    }

    /* 用 json_about.h API 填充 cmd_msg */
    if (json_parse_command(tcp_recv_buffer.json_root, &tcp_recv_buffer.cmd_msg) != 0) {
        cJSON_Delete(tcp_recv_buffer.json_root);
        tcp_recv_buffer.json_root = NULL;
        log_make(&tcp_recv_buffer.log_msg, WARN, gettime_us(),
                 MODULE_ID_TCP_RECV, "parse cmd json failed");
        msg_dispatch(MODULE_ID_TCP_RECV, MODULE_ID_LOGGER,
                     sizeof(tcp_recv_buffer.log_msg), MSG_TYPE_LOG, &tcp_recv_buffer.log_msg);
        return;
    }

    int module_id = json_get_int_def(tcp_recv_buffer.json_root, "mod", -1);
    int type = tcp_recv_buffer.cmd_msg.type;

    if (module_id < 0 || module_id >= MODULE_ID_MAX) {
        log_make(&tcp_recv_buffer.log_msg, WARN, gettime_us(),
                 MODULE_ID_TCP_RECV, "bad mod in cmd json");
        msg_dispatch(MODULE_ID_TCP_RECV, MODULE_ID_LOGGER,
                     sizeof(tcp_recv_buffer.log_msg), MSG_TYPE_LOG, &tcp_recv_buffer.log_msg);
        cJSON_Delete(tcp_recv_buffer.json_root);
        tcp_recv_buffer.json_root = NULL;
        return;
    }

    switch (type) {
        case 0: {
            /* 控制类指令 — 发给目标模块 */
            msg_dispatch(MODULE_ID_TCP_RECV,
                         (Module_ID_e)module_id,
                         sizeof(tcp_recv_buffer.cmd_msg),
                         MSG_TYPE_COMMAND,
                         &tcp_recv_buffer.cmd_msg);
            break;
        }
        case 1: {
            /* 查询类指令 — 发给目标模块 */
            msg_dispatch(MODULE_ID_TCP_RECV,
                         (Module_ID_e)module_id,
                         sizeof(tcp_recv_buffer.cmd_msg),
                         MSG_TYPE_COMMAND,
                         &tcp_recv_buffer.cmd_msg);
            break;
        }
        case 2: {
            /* 上传类指令 — 预留，暂做丢弃 */
            cJSON_Delete(tcp_recv_buffer.json_root);
            tcp_recv_buffer.json_root = NULL;
            break;
        }
        default: {
            cJSON_Delete(tcp_recv_buffer.json_root);
            tcp_recv_buffer.json_root = NULL;
            break;
        }
    }

    /* 标记指令已发出，等待被处理 */
    tcp_recv_mark_cmd_sent();
}

void* tcp_recv_thread(void* arg)
{
    (void)arg;

    Tcp_Recv_Init();
    msg_register_module(MODULE_ID_TCP_RECV, tcp_recv_msg_handler, tcp_recv_msg_release_handler);
    tcp_recv_buffer.link = Tcp_Shared_Wait_Link();
    int local_sock = -1;

    while (running) {
        struct timespec ts;
        pthread_mutex_lock(&tcp_recv_buffer.link->lock);
        while (!tcp_recv_buffer.link->connected && running) {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 100000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec += 1;
                ts.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&tcp_recv_buffer.link->cond, &tcp_recv_buffer.link->lock, &ts);
        }
        if (!running) {
            pthread_mutex_unlock(&tcp_recv_buffer.link->lock);
            break;
        }
        local_sock = tcp_recv_buffer.link->Sock;
        pthread_mutex_unlock(&tcp_recv_buffer.link->lock);

        struct timeval tv = {0, 500000};
        setsockopt(local_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

        while (running) {
            Frame_Header hdr;

            /* 1. 流控：等待上一个指令被处理完（超时 2s 后强制放行） */
            if (tcp_recv_wait_cmd_ready() != 0) {
                log_make(&tcp_recv_buffer.log_msg, WARN, gettime_us(),
                         MODULE_ID_TCP_RECV, "Command read overtime!");
                msg_dispatch(MODULE_ID_TCP_RECV, MODULE_ID_LOGGER, sizeof(tcp_recv_buffer.log_msg), MSG_TYPE_LOG, &tcp_recv_buffer.log_msg);
            }

            /* 2. 读取帧头 */
            if (tcp_recv_all(local_sock, (uint8_t*)&hdr, sizeof(Frame_Header)) != 0) {
                break;
            }

            /* 3. 校验帧头: magic + CRC */
            uint16_t crc = crc16_ccitt((uint8_t*)&hdr, sizeof(Frame_Header) - 2);
            if (hdr.magic != 0xABCD || crc != hdr.crc) {
                log_make(&tcp_recv_buffer.log_msg, WARN, gettime_us(),
                         MODULE_ID_TCP_RECV, "Bad header, sync search...");
                msg_dispatch(MODULE_ID_TCP_RECV, MODULE_ID_LOGGER, sizeof(tcp_recv_buffer.log_msg), MSG_TYPE_LOG, &tcp_recv_buffer.log_msg);

                if (tcp_search_frame_header(local_sock, &hdr) != 0) {
                    break;
                }
            }

            /* 4. data_len 上限校验 */
            if (hdr.data_len > MAX_FRAME_DATA_LEN) {
                log_make(&tcp_recv_buffer.log_msg, LOG_ERROR, gettime_us(),
                         MODULE_ID_TCP_RECV, "data_len exceed MAX_FRAME_DATA_LEN");
                msg_dispatch(MODULE_ID_TCP_RECV, MODULE_ID_LOGGER, sizeof(tcp_recv_buffer.log_msg), MSG_TYPE_LOG, &tcp_recv_buffer.log_msg);
                break;
            }

            /* 5. 读取数据载荷 */
            if (hdr.data_len == 0) {
                tcp_recv_buffer.last_cmd_ts = gettime_us();
                continue;
            }

            if (hdr.data_len < sizeof(tcp_recv_buffer.recv_buffer)) {
                if (tcp_recv_all(local_sock, tcp_recv_buffer.recv_buffer, hdr.data_len) != 0) {
                    break;
                }
                tcp_recv_buffer.recv_buffer_len = hdr.data_len;
            } else {
                uint8_t trash[128];
                uint32_t remaining = hdr.data_len;
                while (remaining > 0) {
                    uint32_t to_read = (remaining > sizeof(trash)) ? sizeof(trash) : remaining;
                    if (tcp_recv_all(local_sock, trash, to_read) != 0) {
                        break;
                    }
                    remaining -= to_read;
                }
                log_make(&tcp_recv_buffer.log_msg, WARN, gettime_us(),
                         MODULE_ID_TCP_RECV, "Packet too large, trash dropped");
                msg_dispatch(MODULE_ID_TCP_RECV, MODULE_ID_LOGGER, sizeof(tcp_recv_buffer.log_msg), MSG_TYPE_LOG, &tcp_recv_buffer.log_msg);
                continue;
            }

            /* 6. 更新时间戳 + 分发 */
            tcp_recv_buffer.last_cmd_ts = gettime_us();
            cmd_deliver(&hdr, tcp_recv_buffer.recv_buffer);
        }

        pthread_mutex_lock(&tcp_recv_buffer.link->lock);
        if (tcp_recv_buffer.link->Sock == local_sock) {
            if (local_sock >= 0) {
                close(local_sock);
                tcp_recv_buffer.link->Sock = -1;
            }
            tcp_recv_buffer.link->connected = false;
        }
        pthread_mutex_unlock(&tcp_recv_buffer.link->lock);
        local_sock = -1;

        log_make(&tcp_recv_buffer.log_msg, INFO, gettime_us(),
                 MODULE_ID_TCP_RECV, "Connection lost, wait reconnect");
        msg_dispatch(MODULE_ID_TCP_RECV, MODULE_ID_LOGGER, sizeof(tcp_recv_buffer.log_msg), MSG_TYPE_LOG, &tcp_recv_buffer.log_msg);
    }

    Tcp_Recv_Deinit();
    return NULL;
}

void tcp_recv_thread_wakeup(void)
{
    if (tcp_recv_buffer.link != NULL) {
        pthread_mutex_lock(&tcp_recv_buffer.link->lock);
        pthread_cond_signal(&tcp_recv_buffer.link->cond);
        pthread_mutex_unlock(&tcp_recv_buffer.link->lock);
    }
}