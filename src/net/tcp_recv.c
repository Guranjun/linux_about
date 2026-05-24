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
#include <stdint.h>
#include <stdbool.h>

#define RECV_BUFFER_SIZE 512
#define MAX_FRAME_DATA_LEN (1 * 512U)

typedef struct{
    Tcp_Shared_Link_t* link;
    cJSON* json_root;
    Command_Msg_t cmd_msg;
    bool cmd_msg_received;
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
    //tcp_recv_buffer.cmd_msg = 0;
    tcp_recv_buffer.cmd_msg_received = false;
    tcp_recv_buffer.recv_buffer_len = 0;
    tcp_recv_buffer.last_cmd_ts = gettime_us();
}

static void Tcp_Recv_Deinit(void)
{
    msg_unregister_module(MODULE_ID_TCP_RECV);
    tcp_recv_buffer.link = NULL;
}

void tcp_recv_msg_handler(Common_Msg_t* msg)
{
    (void)msg;
}

void tcp_recv_msg_release_handler(Common_Msg_t* msg)
{
    if (msg != NULL && msg->data != NULL) {
        free(msg->data);
        msg->data = NULL;
    }
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
            /* EAGAIN / EWOULDBLOCK: SO_RCVTIMEO 触发了超时，检查 running 后重试 */
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
 *
 *        匹配成功后重组完整帧头并进行 CRC 校验。
 *        CRC 失败时 buf 被重组为 [0xAB, 0xCD, ...] 但返回 0，
 *        调用者继续滑动窗口即可推进，不会死循环。
 *
 * @return  1 找到有效帧头（CRC通过）
 *          0 未匹配或 CRC 失败
 *         -1 连接断开
 */
static int try_match_header_at(int sock, uint8_t* buf, int idx, int hdr_size)
{
    if (buf[idx] != 0xAB) {
        return 0;
    }

    /* 检查 0xAB 之后是否为 0xCD */
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

    /* ===== 找到 0xABCD，重组帧头到 buf[0..31] ===== */
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

    /* CRC 失败：buf 已被重组为 [0xAB, 0xCD, ...]，
       调用者滑动窗口左移即可丢弃这个无效 0xAB，不会反复匹配 */
    return 0;
}

/**
 * @brief 滑动窗口搜索帧头
 *
 *        buf 始终保持 32 字节的滑动窗口。
 *        每轮扫描 buf[1..31] 找 0xAB + 0xCD 匹配，
 *        若未找到则左移 1 字节 + 从 socket 读 1 字节填末尾，重复。
 *
 *        CRC 失败不会导致反复扫描同一个窗口：
 *        - CRC 失败后 buf = [0xAB, 0xCD, ...]，buf[1]=0xCD 不可能是 0xAB 起点
 *        - 本轮扫描结束必然触发滑动，0xAB(buf[0]) 被丢弃
 *
 * @return 0 成功, -1 连接断开
 */
static int tcp_search_frame_header(int sock, Frame_Header* out_hdr)
{
    uint8_t* buf = (uint8_t*)out_hdr;
    const int HDR_SIZE = (int)sizeof(Frame_Header);

    while (1) {
        /* 扫描 buf[1..HDR_SIZE-1] 找 0xAB */
        for (int i = 1; i < HDR_SIZE; i++) {
            int ret = try_match_header_at(sock, buf, i, HDR_SIZE);
            if (ret == 1) {
                return 0;
            }
            if (ret == -1) {
                return -1;
            }
            /* ret == 0:
               未匹配或 CRC 失败。若 CRC 失败 buf 已重组为 [0xAB, 0xCD, ...]，
               buf[1]=0xCD 不可能作为 0xAB 起点，不会重复匹配。
               继续扫描 buf[2..] 中剩余位置。 */
        }

        /* 整轮扫描未命中 → 滑动窗口 */
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
 * @param hdr  帧头信息
 * @param data 帧数据载荷
 */
static void cmd_deliver(Frame_Header* hdr, uint8_t* data)
{
    if (data == NULL || hdr->data_len == 0) {
        return;
    }

    data[hdr->data_len] = '\0';
    tcp_recv_buffer.json_root = cJSON_Parse((const char*)data);
    printf("json parse %s\n", tcp_recv_buffer.json_root ? "OK" : "FAIL");
    if (!tcp_recv_buffer.json_root) printf("error: %s\n", cJSON_GetErrorPtr());
    if (tcp_recv_buffer.json_root != NULL) {
        //进行消息创建分发逻辑
        cJSON *module_node = cJSON_GetObjectItemCaseSensitive(tcp_recv_buffer.json_root, "mod");
        cJSON *command_node = cJSON_GetObjectItemCaseSensitive(tcp_recv_buffer.json_root, "cmd");
        if (cJSON_IsNumber(module_node) && cJSON_IsNumber(command_node)) {
        
            int module_id = module_node->valueint;
            int cmd_id = command_node->valueint;                // 数字读 valueint
            //取锁
            //if(!tcp_recv_buffer.cmd_msg_received){
                tcp_recv_buffer.cmd_msg.cmd = (CMD_ID)cmd_id;
                //log_make(&tcp_recv_buffer.log_msg, LOG_INFO, gettime_us(),
                //    MODULE_ID_TCP_RECV, "JSON parse success!");
                //msg_dispatch(MODULE_ID_TCP_RECV, MODULE_ID_LOGGER, sizeof(tcp_recv_buffer.log_msg), MSG_TYPE_LOG, &tcp_recv_buffer.log_msg);
                //msg_dispatch(MODULE_ID_TCP_RECV, (Module_ID_e)module_id, sizeof(tcp_recv_buffer.cmd_msg), MSG_TYPE_COMMAND, &tcp_recv_buffer.cmd_msg);
            //}
            

            // 调试打印或者通过你的通用消息接口发出去
            // printf("读键成功！模块: %s, 命令: %d\n", module_name, cmd_id);

            // --- 这里衔接你上一轮的消息分发 msg_dispatch ---
            // if (cmd_id == 10) { ... }

        } 
        else {
        
        log_make(&tcp_recv_buffer.log_msg, WARN, gettime_us(),
                 MODULE_ID_TCP_RECV, "JSON keys missing or type mismatch");
        msg_dispatch(MODULE_ID_TCP_RECV, MODULE_ID_LOGGER, sizeof(tcp_recv_buffer.log_msg), MSG_TYPE_LOG, &tcp_recv_buffer.log_msg);
        }
        cJSON_Delete(tcp_recv_buffer.json_root);
        tcp_recv_buffer.json_root = NULL;
    }
    
}

void* tcp_recv_thread(void* arg)
{
    (void)arg;

    Tcp_Recv_Init();
    msg_register_module(MODULE_ID_TCP_RECV, tcp_recv_msg_handler, tcp_recv_msg_release_handler);
    tcp_recv_buffer.link = Tcp_Shared_Wait_Link();
    int local_sock = -1;

    while (running) {
        /* 等待连接建立 (timedwait 100ms, 避免依赖外部信号才能退出) */
        struct timespec ts;
        pthread_mutex_lock(&tcp_recv_buffer.link->lock);
        while (!tcp_recv_buffer.link->connected && running) {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 100000000; /* +100ms */
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

        /* 设置 SO_RCVTIMEO 500ms，使 recv 阻塞时可定时醒来检查 running */
        struct timeval tv = {0, 500000}; /* 0s 500ms */
        setsockopt(local_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

        while (running) {
            Frame_Header hdr;
            printf("Start to read hdr\n");
            /* 1. 读取帧头 */
            if (tcp_recv_all(local_sock, (uint8_t*)&hdr, sizeof(Frame_Header)) != 0) {
                break;
            }

            /* 2. 校验帧头: magic + CRC */
            uint16_t crc = crc16_ccitt((uint8_t*)&hdr, sizeof(Frame_Header) - 2);
            if (hdr.magic != 0xABCD || crc != hdr.crc) {
                log_make(&tcp_recv_buffer.log_msg, WARN, gettime_us(),
                         MODULE_ID_TCP_RECV, "Bad header, sync search...");
                msg_dispatch(MODULE_ID_TCP_RECV, MODULE_ID_LOGGER, sizeof(tcp_recv_buffer.log_msg), MSG_TYPE_LOG, &tcp_recv_buffer.log_msg);

                if (tcp_search_frame_header(local_sock, &hdr) != 0) {
                    break;
                }
            }

            /* 3. data_len 上限校验：超过上限说明协议异常，直接断连 */
            if (hdr.data_len > MAX_FRAME_DATA_LEN) {
                log_make(&tcp_recv_buffer.log_msg, LOG_ERROR, gettime_us(),
                         MODULE_ID_TCP_RECV, "data_len exceed MAX_FRAME_DATA_LEN");
                msg_dispatch(MODULE_ID_TCP_RECV, MODULE_ID_LOGGER, sizeof(tcp_recv_buffer.log_msg), MSG_TYPE_LOG, &tcp_recv_buffer.log_msg);
                break;
            }

            /* 4. 读取数据载荷 */
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
                /* 超过本地缓冲区但未超 MAX_FRAME_DATA_LEN，强行读空 */
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

            /* 5. 更新时间戳 + 分发 */
            tcp_recv_buffer.last_cmd_ts = gettime_us();
            cmd_deliver(&hdr, tcp_recv_buffer.recv_buffer);
        }

        /* ===== 连接断开 / 协议异常 处理 =====
         * 使用 local_sock 而非 link->Sock 进行 close 和检查，
         * 避免并发重连后 link->Sock 已被替换为新的 fd */
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
