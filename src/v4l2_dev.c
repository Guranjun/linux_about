#include "v4l2_dev.h"
#include "common.h"
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct{
	int fd;				//设备文件描述符
	int buffer_count;		//缓冲区数量
	unsigned char *mmpaddr[4];	//映射后的首地址
	unsigned int addr_length[4];	//映射后空间的大小
	int width;			//视频宽度
	int height;			//视频高度
} V4L2_Device;

void *camera_capture_thread(void *arg)
{
    char *dev_path = (char *)arg;
    while(running){
        /*实现采集逻辑*/
    }
}