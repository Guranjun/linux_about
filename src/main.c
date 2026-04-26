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
int running = 1;
Camera_Udp_SharedBuffer camera_udp_shared_buffer;

// 信号处理函数
void stop_handler(int sig) {
    printf("\n[Main] Stopping threads...\n");
    running = 0;
    
    // 关键：拍醒所有正在 cond_wait 的线程，让它们检查 running 标志并退出
    pthread_mutex_lock(&camera_udp_shared_buffer.lock);
    pthread_cond_broadcast(&camera_udp_shared_buffer.cond);
    pthread_mutex_unlock(&camera_udp_shared_buffer.lock);
}

int main(int argc,char **argv)
{
    if(argc < 3){
        printf("Usage: %s <video_device> <target_ip>\n", argv[0]);
        return -1;
    }
    signal(SIGINT, stop_handler);
    /*初始化共享缓冲区*/
    camera_udp_shared_buffer.camera_data[0] = (unsigned char *)malloc(FRAME_HIGH * FRAME_WIDTH); // 1MB缓冲区
    camera_udp_shared_buffer.camera_data[1] = (unsigned char *)malloc(FRAME_HIGH * FRAME_WIDTH);
    camera_udp_shared_buffer.latest_index = -1;
    camera_udp_shared_buffer.is_sending = 0;
    pthread_mutex_init(&camera_udp_shared_buffer.lock, NULL);
    pthread_cond_init(&camera_udp_shared_buffer.cond, NULL);
    /*线程相关的定义*/
    pthread_t t_camera_capture, t_udp_send;
    pthread_create(&t_camera_capture, NULL, camera_capture_thread, (void *)argv[1]);
    pthread_create(&t_udp_send, NULL, udp_send_thread, (void *)argv[2]);
    pthread_join(t_camera_capture, NULL);
    pthread_join(t_udp_send, NULL);
    /*释放资源*/
    free(camera_udp_shared_buffer.camera_data[0]);
    free(camera_udp_shared_buffer.camera_data[1]);
    pthread_mutex_destroy(&camera_udp_shared_buffer.lock);
    pthread_cond_destroy(&camera_udp_shared_buffer.cond);

}