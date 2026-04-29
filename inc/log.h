#ifndef __LOG_H
#define __LOG_H

typedef enum{
    DEBUG = 0,//调试信息
    INFO ,//程序运行信息
    WARN ,//警告，有异常出现
    ERROR //出现错误
     
}LOG_LEVEL;
typedef struct{
    uint32_t timestamp;
    LOG_LEVEL level;
    char module_name[16];//模块名字
    char content[64];//日志内容
}Log_Msg_t;

#endif // __LOG_H