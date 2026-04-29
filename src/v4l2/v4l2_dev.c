#include "v4l2_dev.h"
#include "common.h"

#include <complex.h>
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
typedef struct{
	int Image_Taken_flag;/*图像占用标志位，0：没有正在被占用，1：正在发送数据
						  */ 
	int counter;//计数器，记录被占用的次数
}V4L2_Image_Taken_Flag;//v4l2采集线程私有数据结构体定义，标志位，记录正在发送数据的次数
typedef struct{
	Image_Data camera_data[V4L2_BUF_COUNT]; //双缓冲区图像数据
	int latest_index; //最新数据所在的索引
	V4L2_Image_Taken_Flag taken_flag[V4L2_BUF_COUNT]; //图像占用标志位和计数器
													  // flag标志位0：没有正在被占用，1：正在被使用，
													  // counter记录被占用的次数
	pthread_mutex_t lock; //互斥锁，保护数据访问
	pthread_cond_t cond; //条件变量，通知数据更新
}V4L2_Data_buffer;//v4l2采集线程私有数据结构体定义
static V4L2_Data_buffer v4l2_data_buffer; //v4l2采集线程私有数据实例
void Change_Image_Taken_Flag(int target_index)
{
	//pthread_mutex_lock(&v4l2_data_buffer.lock);
	v4l2_data_buffer.taken_flag[target_index].Image_Taken_flag = 1;
	v4l2_data_buffer.taken_flag[target_index].counter++;
	//pthread_mutex_unlock(&v4l2_data_buffer.lock);
}
void Reset_Image_Taken_Flag(int target_index)
{
	pthread_mutex_lock(&v4l2_data_buffer.lock);
	if(v4l2_data_buffer.taken_flag[target_index].counter > 0){
		v4l2_data_buffer.taken_flag[target_index].counter--;
	}
	if(v4l2_data_buffer.taken_flag[target_index].counter == 0){
		v4l2_data_buffer.taken_flag[target_index].Image_Taken_flag = 0;
	}
	
	pthread_mutex_unlock(&v4l2_data_buffer.lock);
}
static void v4l2_data_buffer_init(void)
{
	for(int i = 0; i < V4L2_BUF_COUNT; i++){
		v4l2_data_buffer.camera_data[i].data = malloc(FRAME_WIDTH * FRAME_HIGH * 2); //分配双缓冲区内存
		v4l2_data_buffer.camera_data[i].len = 0;
		v4l2_data_buffer.camera_data[i].timestamps = 0;
		v4l2_data_buffer.camera_data[i].index = i;
		v4l2_data_buffer.taken_flag[i].Image_Taken_flag = 0;
		v4l2_data_buffer.taken_flag[i].counter = 0;
	}	
	v4l2_data_buffer.latest_index = -1;
	//v4l2_data_buffer.status = -1;//初始状态为待更新
	pthread_mutex_init(&v4l2_data_buffer.lock, NULL);
	pthread_cond_init(&v4l2_data_buffer.cond, NULL);
}
static void v4l2_data_buffer_destroy(void)
{
	for(int i = 0; i < V4L2_BUF_COUNT; i++){
		free(v4l2_data_buffer.camera_data[i].data);
	}
	pthread_mutex_destroy(&v4l2_data_buffer.lock);
	pthread_cond_destroy(&v4l2_data_buffer.cond);
}
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
/*摄像头数据采集线程*/
void *camera_capture_thread(void *arg)
{
    char *dev_path = (char *)arg;
	int write_index = 0;
	V4L2_Device cam;
	v4l2_data_buffer_init();
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
		Common_Msg_t Image_to_send_msg;
		Common_Msg_t Image_to_process_msg;
		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		if(ioctl(cam.fd, VIDIOC_DQBUF, &buf) < 0){
			perror("DQBUF failed");
			continue;
		}
		/*将采集到的数据复制到共享缓冲区*/
		memcpy(v4l2_data_buffer.camera_data[write_index].data, cam.mmpaddr[buf.index], buf.bytesused);
		v4l2_data_buffer.camera_data[write_index].len = buf.bytesused;
		v4l2_data_buffer.camera_data[write_index].timestamps = time(NULL);
		/*第二步逻辑实现*/
		pthread_mutex_lock(&v4l2_data_buffer.lock);
		if(v4l2_data_buffer.taken_flag[write_index].counter == 0){
			// 没有正在发送数据，更新latest_index
			v4l2_data_buffer.latest_index = write_index;
			Change_Image_Taken_Flag(write_index); //更新图像占用标志位和计数器
			Image_to_send_msg = msg_make(MODULE_ID_V4L2, MODULE_ID_UDP, sizeof(Image_Data), MSG_TYPE_IMAGE, &v4l2_data_buffer.camera_data[v4l2_data_buffer.latest_index]);
			msg_send(&Image_to_send_msg);	
			write_index = (write_index + 1) % V4L2_BUF_COUNT; // 切换到另一个缓冲区
			//camera_udp_shared_buffer.status = 1; // 数据已经准备好，待处理，待发送
		}
		else{
			// 正在发送数据，不更新latest_index
			//printf("buffer is being sent, skipping update\n");
		}
		pthread_mutex_unlock(&v4l2_data_buffer.lock);
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
	v4l2_data_buffer_destroy();
	close(cam.fd);
	return NULL;
}
/*消息回收函数*/
void V4L2_msg_release_handler(Common_Msg_t *msg)
{
	// 1. 安全检查：防止空指针访问
    if (msg == NULL || msg->data == NULL) {
        return;
    }
	Image_Data *data = (Image_Data *)msg->data;
	int target_index = data->index;
	if(target_index >= 0 && target_index < V4L2_BUF_COUNT){
		Reset_Image_Taken_Flag(target_index); //重置图像占用标志位
	}
}