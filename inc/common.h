#ifndef __COMMON_H
#define __COMMON_H

//#include <cstdint>
//#include <ctime>
#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include "log.h"
#define FRAME_HIGH 480
#define FRAME_WIDTH 640
#define V4L2_BUF_COUNT 2
#ifdef __cplusplus
extern "C"{
#endif
/*udp发送线程与v4l2采集线程之间的共享数据区*/
/*数据类型声明*/

typedef enum{
    MODULE_ID_V4L2 = 0, //v4l2采集模块
    MODULE_ID_UDP,      //udp发送模块
    MODULE_ID_ALARM,    //告警处理模块
    MODULE_ID_LOGGER,   //日志处理模块
    MODULE_ID_STORAGE,  //存储模块
    MODULE_ID_TCP,      //tcp通信模块
    MODULE_ID_COMMAND  //上位机命令获取模块
}Module_ID_e;//模块ID枚举类型定义，根据实际需求设计
typedef enum{
    MSG_TYPE_IMAGE = 0, //图像数据消息
    MSG_TYPE_ALARM,     //告警消息
    MSG_TYPE_LOG,       //日志消息
    MSG_TYPE_COMMAND    //命令消息
}Msg_Type_e;//消息类型枚举类型定义，根据实际需求设计
typedef struct{
    Module_ID_e src_module; //消息来源模块
    Module_ID_e dst_module; //消息目标模块
    uint32_t data_len; //消息数据长度
    Msg_Type_e msg_type; //消息类型
    void* data; //消息数据指针
}Common_Msg_t;//通用的数据交换消息结构体，具体定义根据实际需求设计

typedef void (*MsgHandler_t)(Common_Msg_t* msg); //消息处理函数指针类型定义
typedef void (*MsgReleaseHandler_t)(Common_Msg_t* msg); //消息资源释放函数指针类型定义


typedef struct{
    uint8_t*    data;
    uint32_t    len;
    uint8_t     index;
    time_t      timestamps;
}Image_Data; //图像数据结构体定义，包含数据指针、数据长度和时间戳

typedef struct{

}Log_Data; //日志数据结构体定义

typedef struct{

}Alarm_Data; //告警数据结构体定义
/*声明一个待发送数据缓冲区*/
/*
typedef struct{
    Image_Data *data; //数据内容
    DataType type; //数据类型（如视频帧、告警信息等）
    int status; //数据状态（如正在准备、准备好、正在发送等）
    time_t timestamp; //数据生成的时间戳
    pthread_mutex_t lock; //互斥锁，保护数据访问
    pthread_cond_t cond; //条件变量，通知数据更新
}Transfer_Buffer;
*/

/*图像数据缓冲区*/
/*
typedef struct{
    uint8_t* camera_data[2]; //双缓冲区，存储两帧图像数据
    uint32_t frame_len[2]; //每帧图像数据的长度
    int latest_index; //最新数据所在的索引
    pthread_mutex_t lock; //互斥锁，保护数据访问
    pthread_cond_t cond; //条件变量，通知数据更新
}
*/
typedef struct{
    Image_Data data[2]; //双缓冲区，存储两帧图像数据
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
//需定义一个消息队列结构体，包含消息内容和消息类型等信息
//需定义一个环形缓冲区用来存储视频的告警发生前几帧数据，保证告警发生时可以将之前的几帧数据一起发送出去
//需定义一个日志相关
/*应用运行标志 running在main.c中定义  表示正在运行这个应用*/
extern int running;
extern Camera_Udp_SharedBuffer camera_udp_shared_buffer;
/*通用消息收发模块函数声明*/
//void msg_init(void);
Common_Msg_t msg_make(Module_ID_e src, Module_ID_e dst, uint32_t len, Msg_Type_e type, void* data);
void msg_send(Common_Msg_t* msg);
//void msg_receive(Common_Msg_t* msg);
void* msg_deliver_thread(void* arg);
/*v4l2采集线程相关数据结构和函数声明*/
void V4L2_msg_release_handler(Common_Msg_t* msg);
/*udp发送线程相关数据结构和函数声明*/
void udp_msg_handler(Common_Msg_t* msg);

#ifdef __cplusplus
}
#endif
#endif // __COMMON_H