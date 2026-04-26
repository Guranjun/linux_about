#include "v4l2_dev.h"
#include "common.h"

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

struct V4L2_Device {
	int fd;				//设备文件描述符
	int buffer_count;		//缓冲区数量
	unsigned char *mmpaddr[4];	//映射后的首地址
	unsigned int addr_length[4];	//映射后空间的大小
	int width;			//视频宽度
	int height;			//视频高度
};
static int V4l2_Init(const char *device_path, V4L2_Device *device)
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
	int write_index = 0;
	V4L2_Device cam;
	V4l2_Init(dev_path, &cam);
    while(running){
        /*实现采集逻辑*/
		/*
		*第一步：采集线程不断从V4L2设备获取数据
		*第二步：获取锁，检测is_sending标志位，如果正在发送则不更新latest_index，如果没有正在发送则更新latest_index
		*		释放锁，通知发送线程有新数据可发送
		*第三步：归还缓冲区，继续下一轮采集
		*/
		/*第一步逻辑实现*/
		struct v4l2_buffer buf;
		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		if(ioctl(cam.fd, VIDIOC_DQBUF, &buf) < 0){
			perror("DQBUF failed");
			continue;
		}
		/*将采集到的数据复制到共享缓冲区*/
		memcpy(camera_udp_shared_buffer.camera_data[write_index], cam.mmpaddr[buf.index], buf.bytesused);
		camera_udp_shared_buffer.frame_len[write_index] = buf.bytesused;
		/*第二步逻辑实现*/
		pthread_mutex_lock(&camera_udp_shared_buffer.lock);
		if(camera_udp_shared_buffer.status == -1 || camera_udp_shared_buffer.status == 1){
			// 没有正在发送数据，更新latest_index
			camera_udp_shared_buffer.latest_index = write_index;
			pthread_cond_broadcast(&camera_udp_shared_buffer.cond);
			write_index = (write_index + 1) % 2; // 切换到另一个缓冲区
			camera_udp_shared_buffer.status = 1; // 数据已经准备好，待处理，待发送
		}
		else{
			// 正在发送数据，不更新latest_index
			//printf("buffer is being sent, skipping update\n");
		}
		pthread_mutex_unlock(&camera_udp_shared_buffer.lock);
		/*第三步逻辑实现*/
		if(ioctl(cam.fd, VIDIOC_QBUF, &buf) < 0){
			perror("QBUF failed");
		}
    }
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam.fd, VIDIOC_STREAMOFF, &type);
	// 释放映射的 4 个缓冲区内存
    for (int i = 0; i < cam.buffer_count; i++) {
        if (cam.mmpaddr[i] != NULL) {
            munmap(cam.mmpaddr[i], cam.addr_length[i]);
        }
    }
	close(cam.fd);
	return NULL;
}