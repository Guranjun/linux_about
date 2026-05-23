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
volatile int running = 1;


// 信号处理函数
void stop_handler(int sig) {
    printf("\n[Main] Stopping threads...\n");
    running = 0;
    msg_thread_wakeup(); // 关键：拍醒所有正在 cond_wait 的线程，让它们检查 running 标志并退出
    udp_thread_wakeup(); // 关键：拍醒所有正在 cond_wait 的线程，让它们检查 running 标志并退出
    tcp_send_thread_wakeup();
    alarm_thread_wakeup();
    storage_thread_wakeup();
    logger_thread_wakeup();
    // 关键：拍醒所有正在 cond_wait 的线程，让它们检查 running 标志并退出
   /* pthread_mutex_lock(&camera_udp_shared_buffer.lock);
    pthread_cond_broadcast(&camera_udp_shared_buffer.cond);
    pthread_mutex_unlock(&camera_udp_shared_buffer.lock);
    */
}

int main(int argc,char **argv)
{
    if(argc < 3){
        printf("Usage: %s <video_device> <target_ip>\n", argv[0]);
        return -1;
    }
    signal(SIGINT, stop_handler);
    /*初始化共享缓冲区*/
    /*线程相关的定义*/
    pthread_t t_camera_capture, t_udp_send, t_tcp_send, t_image_process, t_msg_deliver, t_image_storage, t_log_process;
    pthread_create(&t_camera_capture, NULL, camera_capture_thread, (void *)argv[1]);
    pthread_create(&t_udp_send, NULL, udp_send_thread, (void *)argv[2]);
    pthread_create(&t_tcp_send, NULL, tcp_send_thread, (void *)argv[2]);
    pthread_create(&t_image_process, NULL, process_image_thread, NULL);
    pthread_create(&t_msg_deliver, NULL, msg_deliver_thread, NULL);
    pthread_create(&t_image_storage, NULL, storage_video_thread, NULL);
    pthread_create(&t_log_process, NULL, logger_process_thread, NULL);
    pthread_join(t_camera_capture, NULL);
    pthread_join(t_udp_send, NULL);
    pthread_join(t_tcp_send, NULL);
    pthread_join(t_image_process, NULL);
    pthread_join(t_msg_deliver, NULL);
    pthread_join(t_image_storage, NULL);
    pthread_join(t_log_process, NULL);
    /*释放资源*/
    /*free(camera_udp_shared_buffer.camera_data[0]);
    free(camera_udp_shared_buffer.camera_data[1]);
    pthread_mutex_destroy(&camera_udp_shared_buffer.lock);
    pthread_cond_destroy(&camera_udp_shared_buffer.cond);
    */
}