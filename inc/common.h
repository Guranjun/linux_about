#ifndef __COMMON_H
#define __COMMON_H

#include <pthread.h>
#include <stdint.h>
/*udp发送线程与v4l2采集线程之间的共享数据区*/
typedef struct{
    uint8_t *camera_data[2];
    uint32_t frame_len[2];
    int write;
    int read;
    int New_Frame_flag;
    pthread_mutex_t lock;
    pthread_cond_t  cond;
}SharedBuffer;
/*udp数据包的数据帧头定义*/
typedef struct{
	uint16_t magic;		//帧头标志
	uint32_t frame_id;	//帧ID
	uint16_t pkg_cnt;	//分包总数
	uint16_t pkg_id;	//分包ID
	uint16_t data_len;	//数据长度
	uint32_t timestamp;	//时间戳
} __attribute__((packed)) Frame_Header;
/*应用运行标志 running在main.c中定义  表示正在运行这个应用*/
extern int running;
extern char* ip_address;
#endif