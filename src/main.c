/*系统库与内核库*/
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
/*自定义头文件*/
#include "common.h"
#include "v4l2_dev.h"
#include "udp_send.h"
#include "tcp_send.h"
#include "msg_deliver.h"
#include "alarm.hpp"
#include "storage_video.hpp"
#include "log.h"
#include "tcp_recv.h"
#include "command_get.h"
volatile int running = 1;

/**
 * @brief 信号处理函数（SIGINT）
 * @param sig 信号编号
 *
 * 设置 running = 0 并唤醒所有线程，触发各线程退出循环
 */
void stop_handler(int sig) {
    printf("\n[Main] Stopping threads...\n");
    running = 0;
    msg_thread_wakeup();
    udp_thread_wakeup();
    tcp_send_thread_wakeup();
    tcp_recv_thread_wakeup();
    alarm_thread_wakeup();
    storage_thread_wakeup();
    logger_thread_wakeup();
    command_thread_wakeup();
}

/**
 * @brief 主函数
 * @param argc 参数个数
 * @param argv 参数列表（argv[1]=video设备, argv[2]=目标IP）
 * @return 0=正常退出, -1=参数错误
 *
 * 创建并启动所有工作线程：
 * - camera_capture: V4L2 摄像头采集
 * - udp_send:       UDP 图像传输
 * - tcp_send:       TCP 数据发送
 * - tcp_recv:       TCP 命令接收
 * - image_process:  图像处理/运动检测
 * - msg_deliver:    消息队列分发
 * - image_storage:  视频存储
 * - log_process:    日志/数据库
 * - command_process:命令处理
 */
int main(int argc,char **argv)
{
    if(argc < 3){
        printf("Usage: %s <video_device> <target_ip>\n", argv[0]);
        return -1;
    }
    signal(SIGINT, stop_handler);
    /*初始化共享缓冲区*/
    /*线程相关的定义*/
    pthread_t t_camera_capture, t_udp_send, t_tcp_send, t_image_process, t_msg_deliver, t_image_storage, t_log_process, t_tcp_recv, t_command_process;
    pthread_create(&t_camera_capture, NULL, camera_capture_thread, (void *)argv[1]);
    pthread_create(&t_udp_send, NULL, udp_send_thread, (void *)argv[2]);
    pthread_create(&t_tcp_send, NULL, tcp_send_thread, (void *)argv[2]);
    pthread_create(&t_tcp_recv, NULL, tcp_recv_thread, NULL);
    pthread_create(&t_image_process, NULL, process_image_thread, NULL);
    pthread_create(&t_msg_deliver, NULL, msg_deliver_thread, NULL);
    pthread_create(&t_image_storage, NULL, storage_video_thread, NULL);
    pthread_create(&t_log_process, NULL, logger_process_thread, NULL);
    pthread_create(&t_command_process, NULL, command_process_thread, NULL);
    pthread_join(t_camera_capture, NULL);
    pthread_join(t_udp_send, NULL);
    pthread_join(t_tcp_send, NULL);
    pthread_join(t_tcp_recv, NULL);
    pthread_join(t_image_process, NULL);
    pthread_join(t_msg_deliver, NULL);
    pthread_join(t_image_storage, NULL);
    pthread_join(t_log_process, NULL);
    pthread_join(t_command_process, NULL);
    return 0;
}