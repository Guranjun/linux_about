#ifndef __COMMON_H
#define __COMMON_H

#include <pthread.h>
#include <stdint.h>
#include <time.h>
#define FRAME_HIGH 480
#define FRAME_WIDTH 640
/*udp发送线程与v4l2采集线程之间的共享数据区*/
#ifdef __cplusplus
extern "C"{
#endif
typedef struct{
    uint8_t *camera_data[2];
    uint32_t frame_len[2];
    int latest_index;//最新数据所在的索引
    int status;/*状态位，0：数据正在准备
                *-1:数据发送完毕，待更新
                *1：数据已经准备好，待处理，待发送
                *2：数据正在处理
                *3：数据处理完毕，待发送
                *4：数据正在发送
                */
    pthread_mutex_t lock;
    pthread_cond_t  cond;
}Camera_Udp_SharedBuffer;
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
extern Camera_Udp_SharedBuffer camera_udp_shared_buffer;
#ifdef __cplusplus
}
#endif
#endif // __COMMON_H