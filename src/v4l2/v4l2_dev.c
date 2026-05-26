#include "v4l2_dev.h"
#include "common.h"
#include "msg_about.h"
#include "my_time.h"
#include <complex.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>

/**
 * @brief V4L2 设备结构体
 */
struct V4L2_Device {
	int fd;               /**< 设备文件描述符 */
	int buffer_count;     /**< 缓冲区数量 */
	unsigned char *mmpaddr[4];  /**< 映射后的首地址 */
	unsigned int addr_length[4];/**< 映射后空间的大小 */
	int width;            /**< 视频宽度 */
	int height;           /**< 视频高度 */
};

/**
 * @brief 图像占用标志位
 */
typedef struct{
	int Image_Taken_flag; /**< 0:未被占用, 1:正在发送数据 */ 
	int counter;          /**< 占用计数器 */
}V4L2_Image_Taken_Flag;

/**
 * @brief V4L2 采集线程私有数据结构体
 */
typedef struct{
	Image_Data camera_data[V4L2_BUF_COUNT];   /**< 双缓冲区图像数据 */
	int latest_index;                          /**< 最新数据所在的索引 */
	V4L2_Image_Taken_Flag taken_flag[V4L2_BUF_COUNT]; /**< 图像占用标志位和计数器 */
	pthread_mutex_t lock;                      /**< 互斥锁 */
	pthread_cond_t cond;                       /**< 条件变量 */
}V4L2_Data_buffer;

static V4L2_Data_buffer v4l2_data_buffer;

/**
 * @brief 更新图像占用标志位（计数 +1）
 * @param target_index 目标缓冲区索引
 */
void Change_Image_Taken_Flag(int target_index)
{
	v4l2_data_buffer.taken_flag[target_index].Image_Taken_flag = 1;
	v4l2_data_buffer.taken_flag[target_index].counter++;
}

/**
 * @brief 重置图像占用标志位（计数 -1，减到 0 时标志清零）
 * @param target_index 目标缓冲区索引
 */
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

/**
 * @brief 初始化 V4L2 数据缓冲区
 */
static void v4l2_data_buffer_init(void)
{
	for(int i = 0; i < V4L2_BUF_COUNT; i++){
		v4l2_data_buffer.camera_data[i].data = malloc(FRAME_WIDTH * FRAME_HIGH * 2);
		v4l2_data_buffer.camera_data[i].len = 0;
		v4l2_data_buffer.camera_data[i].timestamps = 0;
		v4l2_data_buffer.camera_data[i].index = i;
		v4l2_data_buffer.taken_flag[i].Image_Taken_flag = 0;
		v4l2_data_buffer.taken_flag[i].counter = 0;
	}	
	v4l2_data_buffer.latest_index = -1;
	pthread_mutex_init(&v4l2_data_buffer.lock, NULL);
	pthread_cond_init(&v4l2_data_buffer.cond, NULL);
}

/**
 * @brief 释放 V4L2 数据缓冲区资源
 */
static void v4l2_data_buffer_destroy(void)
{
	for(int i = 0; i < V4L2_BUF_COUNT; i++){
		free(v4l2_data_buffer.camera_data[i].data);
	}
	pthread_mutex_destroy(&v4l2_data_buffer.lock);
	pthread_cond_destroy(&v4l2_data_buffer.cond);
}

/**
 * @brief 初始化 V4L2 摄像头设备
 * @param device_path 设备路径
 * @param device      V4L2 设备结构体指针
 * @return 0=成功
 */
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

/**
 * @brief 摄像头数据采集线程入口
 * @param arg 设备路径字符串
 * @return NULL
 *
 * 从 V4L2 设备循环采集 MJPEG 图像帧，
 * 通过消息系统分发给 UDP 发送、存储和告警模块
 */
void *camera_capture_thread(void *arg)
{
    char *dev_path = (char *)arg;
	int write_index = 0;
	struct timespec ts_now, ts_target;
	clock_gettime(CLOCK_MONOTONIC, &ts_now);
	ts_target = ts_now;
	V4L2_Device cam;
	v4l2_data_buffer_init();
	V4l2_Init(dev_path, &cam);
    while(running){
		struct v4l2_buffer buf;
		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		if(ioctl(cam.fd, VIDIOC_DQBUF, &buf) < 0){
			perror("DQBUF failed");
			continue;
		}
		memcpy(v4l2_data_buffer.camera_data[write_index].data, cam.mmpaddr[buf.index], buf.bytesused);
		v4l2_data_buffer.camera_data[write_index].len = buf.bytesused;
		v4l2_data_buffer.camera_data[write_index].timestamps = gettime_us();

		pthread_mutex_lock(&v4l2_data_buffer.lock);
		if(v4l2_data_buffer.taken_flag[write_index].counter == 0){
			v4l2_data_buffer.latest_index = write_index;
			write_index = (write_index + 1) % V4L2_BUF_COUNT;
			pthread_mutex_unlock(&v4l2_data_buffer.lock);
			Change_Image_Taken_Flag(v4l2_data_buffer.latest_index);
			Change_Image_Taken_Flag(v4l2_data_buffer.latest_index);
			msg_dispatch(MODULE_ID_V4L2, MODULE_ID_UDP, sizeof(Image_Data), MSG_TYPE_IMAGE, &v4l2_data_buffer.camera_data[v4l2_data_buffer.latest_index]);
			msg_dispatch(MODULE_ID_V4L2, MODULE_ID_STORAGE, sizeof(Image_Data), MSG_TYPE_IMAGE, &v4l2_data_buffer.camera_data[v4l2_data_buffer.latest_index]);
#ifdef MSG_ENABLE_PRIORITY
			msg_dispatch_with_priority(MODULE_ID_V4L2, MODULE_ID_ALARM, sizeof(Image_Data), MSG_TYPE_IMAGE, MSG_PRIORITY_HIGH, &v4l2_data_buffer.camera_data[v4l2_data_buffer.latest_index]);
#else
			clock_gettime(CLOCK_MONOTONIC, &ts_now);
			if(ts_now.tv_sec > ts_target.tv_sec ||(ts_now.tv_sec == ts_target.tv_sec && ts_now.tv_nsec >= ts_target.tv_nsec)){
				Change_Image_Taken_Flag(v4l2_data_buffer.latest_index);
				msg_dispatch(MODULE_ID_V4L2, MODULE_ID_ALARM, sizeof(Image_Data), MSG_TYPE_IMAGE, &v4l2_data_buffer.camera_data[v4l2_data_buffer.latest_index]);
				ts_target.tv_sec = ts_now.tv_sec + 1;
				ts_target.tv_nsec = ts_now.tv_nsec;
			}
#endif
		}
		else{
			pthread_mutex_unlock(&v4l2_data_buffer.lock);
		}
		if(ioctl(cam.fd, VIDIOC_QBUF, &buf) < 0){
			perror("QBUF failed");
		}
    }
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam.fd, VIDIOC_STREAMOFF, &type);
    for (int i = 0; i < cam.buffer_count; i++) {
        if (cam.mmpaddr[i] != NULL) {
            munmap(cam.mmpaddr[i], cam.addr_length[i]);
        }
    }
	v4l2_data_buffer_destroy();
	msg_unregister_module(MODULE_ID_V4L2);
	close(cam.fd);
	return NULL;
}

/**
 * @brief V4L2 模块的消息释放处理函数
 * @param msg 待释放的消息
 *
 * 重置对应缓冲区的占用标志位
 */
void V4L2_msg_release_handler(Common_Msg_t *msg)
{
    if (msg == NULL || msg->data == NULL) {
        return;
    }
	Image_Data *data = (Image_Data *)msg->data;
	int target_index = data->index;
	if(target_index >= 0 && target_index < V4L2_BUF_COUNT){
		Reset_Image_Taken_Flag(target_index);
	}
}