#ifndef __COMMON_H
#define __COMMON_H


#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include "cJSON.h"
#define FRAME_HIGH 240
#define FRAME_WIDTH 320
#define V4L2_BUF_COUNT 2

#ifdef __cplusplus
extern "C" {
#endif
//模块ID枚举
typedef enum {
    MODULE_ID_V4L2 = 0,
    MODULE_ID_UDP,
    MODULE_ID_ALARM,
    MODULE_ID_LOGGER,
    MODULE_ID_STORAGE,
    MODULE_ID_TCP_SEND,
    MODULE_ID_TCP_RECV,
    MODULE_ID_COMMAND,
    MODULE_ID_MAX
} Module_ID_e;
//用于标识bigdata数据传输的状态
typedef enum {
    IDLE = 0,
    SEND,
    RESEND,
    DONE,
    FILE_DELIVER_ERROR
} STATUS;
//文件类型的定义
typedef enum {
    NORMAL = 0,
    IMAGE,
    DB,
    VIDEO,
    COMMAND
} FILE_TYPE;
//消息类型的定义
typedef enum {
    MSG_TYPE_IMAGE = 0,
    MSG_TYPE_ALARM,
    MSG_TYPE_LOG,
    MSG_TYPE_COMMAND,
    MSG_TYPE_BIGDATA
} Msg_Type_e;
//告警等级的定义
typedef enum {
    SAFE = 0,
    MOVED
} Alarm_Level;
typedef enum {
    CMD_NOR = 0x0000,
    
    CMD_V4L2_ = 0x0100,

    CMD_STORAGE_ = 0x0200,

    CMD_LOG_ = 0x0300,
    CMD_LOG_UPLOAD_DB = 0x0301,

    CMD_UDP = 0x0400,

    CMD_TCP = 0x0500

}CMD_ID;
#ifdef MSG_ENABLE_PRIORITY
//消息优先级的定义
typedef enum {
    MSG_PRIORITY_LOW = 0,
    MSG_PRIORITY_NORMAL,
    MSG_PRIORITY_HIGH,
    MSG_PRIORITY_URGENT
} Msg_Priority_e;
#endif
//通用消息结构体定义
typedef struct {
    Module_ID_e src_module;
    Module_ID_e dst_module;
    uint32_t data_len;
    Msg_Type_e msg_type;
    uint8_t count;
#ifdef MSG_ENABLE_PRIORITY
    Msg_Priority_e priority;
#endif
    void *data;
} Common_Msg_t;
//大数据消息结构体定义
typedef struct {
    void *data_ptr;
    uint32_t total_len;
    FILE_TYPE type;
    STATUS status;
    int fd;
} BigData_Msg_t;
//消息接受释放函数指针类型定义
typedef void (*MsgHandler_t)(Common_Msg_t *msg);
typedef void (*MsgReleaseHandler_t)(Common_Msg_t *msg);
//日志等级定义
typedef enum {
    DEBUG = 0,
    INFO,
    WARN,
    LOG_ERROR
} LOG_LEVEL;
//日志消息结构体定义
typedef struct {
    LOG_LEVEL level;
    uint64_t timestamp;
    Module_ID_e module;
    char content[64];
} Log_Msg_t;
//命令消息结构体定义
typedef struct {
    CMD_ID cmd;           // 命令ID，例如 CMD_V4L2_ + 1
    Module_ID_e src;      // 来源模块（固定 MODULE_ID_TCP_RECV）
    int type;             // JSON 中的 type 字段（0=control,1=query,2=file,3=config）
    cJSON *param;         // JSON 中的 param 子节点（里面可以嵌套任意内容）
} Command_Msg_t;
//图像数据结构体定义
typedef struct {
    uint64_t timestamps;
    uint32_t len;
    uint8_t *data;
    uint8_t index;
} Image_Data;

typedef struct {
} Log_Data;
//告警数据结构体定义
typedef struct {
    Alarm_Level status;
} Alarm_Data;

//网络传输帧头定义，总共32个字节
typedef struct {
    uint16_t magic;		//帧头标志
    uint16_t data_len;	//数据长度
	uint32_t frame_id;	//帧ID
	uint16_t pkg_cnt;	//分包总数
	uint16_t pkg_id;	//分包ID
    FILE_TYPE type;     //数据类型
	uint64_t timestamp;	//时间戳
    uint32_t reserved1; //4字节的保留位
    uint16_t reserved2; //2字节的保留位
    uint16_t crc;       //CRC检测码
} __attribute__((packed)) Frame_Header;

extern volatile int running;


void msg_init(void);
Common_Msg_t msg_make(Module_ID_e src, Module_ID_e dst, uint32_t len, Msg_Type_e type, void *data);
#ifdef MSG_ENABLE_PRIORITY
Common_Msg_t msg_make_with_priority(Module_ID_e src, Module_ID_e dst, uint32_t len, Msg_Type_e type, Msg_Priority_e priority, void *data);
void msg_set_priority(Common_Msg_t *msg, Msg_Priority_e priority);
#endif
int msg_send(Common_Msg_t *msg);


void V4L2_msg_release_handler(Common_Msg_t *msg);
void udp_msg_handler(Common_Msg_t *msg);
void storage_msg_handler(Common_Msg_t *msg);
void alarm_msg_release_handler(Common_Msg_t *msg);
void alarm_msg_handler(Common_Msg_t *msg);
void logger_msg_handler(Common_Msg_t *msg);
void log_make(Log_Msg_t *log_msg, LOG_LEVEL level, uint64_t timestamp, Module_ID_e module, const char *content);

#ifdef __cplusplus
}
#endif

#endif
