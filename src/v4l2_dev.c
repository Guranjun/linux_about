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
void V4l2_Init(const char *device_path, V4L2_Device *device)
{
	device->fd = open(device_path, O_RDWR);
	if(device->fd < 0){
		perror("打开设备失败");
		exit(-1);
	}
	struct v4l2_format vfmt;
	memset(&vfmt, 0, sizeof(vfmt));
	vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vfmt.fmt.pix.width = FRAME_WIDTH;
	vfmt.fmt.pix.height = FRAME_HIGH;
	vfmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	if( ioctl(device->fd, VIDIOC_S_FMT, &vfmt) < 0){
		perror("Set Format error");
		close(device->fd);
		exit(-1);
	}
	device->width = vfmt.fmt.pix.width;
	device->height = vfmt.fmt.pix.height;
	struct v4l2_requestbuffers reqbuffer;
	memset(&reqbuffer, 0, sizeof(reqbuffer));
	reqbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqbuffer.count = 4;
	reqbuffer.memory = V4L2_MEMORY_MMAP;
	if(ioctl(device->fd, VIDIOC_REQBUFS, &reqbuffer) < 0){
		perror("Request Buffers error");
		close(device->fd);
		exit(-1);
	}
	device->buffer_count = reqbuffer.count;
	struct v4l2_buffer mapbuffer;
	memset(&mapbuffer, 0, sizeof(mapbuffer));
	mapbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	for(int i = 0; i < device->buffer_count; i++){
		mapbuffer.index = i;
		if(ioctl(device->fd, VIDIOC_QUERYBUF, &mapbuffer) < 0){
			perror("Query Buffer error");
			close(device->fd);
			exit(-1);
		}
		device->mmpaddr[i] = (unsigned char *)mmap(NULL, mapbuffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, device->fd, mapbuffer.m.offset);
		device->addr_length[i] = mapbuffer.length;
		if(ioctl(device->fd, VIDIOC_QBUF, &mapbuffer) < 0){
			perror("Queue Buffer error");
			close(device->fd);
			exit(-1);
		}
	}
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if(ioctl(device->fd, VIDIOC_STREAMON, &type) < 0){
		perror("Stream On error");
		close(device->fd);
		exit(-1);
	}
	printf("V4L2 Init Success: %dx%d\n", device->width, device->height);
    return 0;
}
void *camera_capture_thread(void *arg)
{
    char *dev_path = (char *)arg;
    while(running){
        /*实现采集逻辑*/
    }
}