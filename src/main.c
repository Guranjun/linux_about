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
char *ip_adrress;
int main(int argc,char **argv)
{
    pthread_t t_camera_capture, t_udp_send;
}