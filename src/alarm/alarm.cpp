#include "alarm.hpp"
#include "image_process.hpp"
#include "common.h"
#include "msg_about.h"
#include "my_time.h"
#include <cstdint>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <string.h>
#include <vector>
using namespace cv;
using namespace std;

/**
 * @brief 图像处理线程私有数据
 */
typedef struct{
    uint8_t frame_buffer[128 * 1024];   /**< 帧数据缓冲区 */
    uint32_t frame_len;                  /**< 帧数据长度 */
    Log_Msg_t log_msg;
    bool is_updated;                     /**< 是否有新帧待处理 */
    pthread_mutex_t lock;
    pthread_cond_t cond;
}Process_Data;

static Process_Data process_data; 

/**
 * @brief 初始化图像处理数据
 */
static void Process_Data_Init(void)
{
    process_data.frame_len = 0;
    process_data.is_updated = false;
    memset(&process_data.log_msg, 0, sizeof(process_data.log_msg));
    pthread_mutex_init(&process_data.lock, NULL);
    pthread_cond_init(&process_data.cond, NULL);
}

/**
 * @brief 释放图像处理数据资源
 */
static void Process_Data_Deinit(void)
{
    msg_unregister_module(MODULE_ID_ALARM);
    pthread_mutex_destroy(&process_data.lock);
    pthread_cond_destroy(&process_data.cond);
}

#ifdef __cplusplus
extern "C" 
{
#endif

/**
 * @brief 图像处理线程入口
 * @param arg 传入参数（不使用）
 * @return NULL
 *
 * 从缓冲区获取图像帧，执行运动检测算法，
 * 将告警状态分发给存储模块并记录日志
 */
void* process_image_thread(void* arg) 
{    
    Process_Data_Init();
    Alarm_Data alarm_data;
    static string log_string;
    while (running) {
        pthread_mutex_lock(&process_data.lock);
        while(!process_data.is_updated && running){
            pthread_cond_wait(&process_data.cond, &process_data.lock);
        }
        if(!running){
            pthread_mutex_unlock(&process_data.lock);
            break;
        }
        process_data.is_updated = false;
        uint8_t* data_to_process = process_data.frame_buffer;
        uint32_t frame_len = process_data.frame_len;
        pthread_mutex_unlock(&process_data.lock);

        Mat raw_data_mat( 1, frame_len, CV_8UC1, data_to_process);
        Mat img = imdecode(raw_data_mat, IMREAD_COLOR);
        if(img.empty()){
            cerr << "Failed to decode image" << endl;
            continue;
        }

        alarm_data = Move_Detect(&img);
        if(alarm_data_diff(alarm_data)){
            log_string = "Status changed to" + to_string(alarm_data.status);
            const char* p = log_string.data();
            log_make(&process_data.log_msg, INFO, gettime_us(), MODULE_ID_ALARM, p);
            msg_dispatch(MODULE_ID_ALARM, MODULE_ID_LOGGER, sizeof(process_data.log_msg), MSG_TYPE_LOG, &process_data.log_msg);
        }
#ifdef MSG_ENABLE_PRIORITY
        msg_dispatch_with_priority(MODULE_ID_ALARM, MODULE_ID_STORAGE, sizeof(alarm_data), MSG_TYPE_ALARM, MSG_PRIORITY_HIGH, &alarm_data);
#else
        msg_dispatch(MODULE_ID_ALARM, MODULE_ID_STORAGE, sizeof(alarm_data), MSG_TYPE_ALARM, &alarm_data);
#endif
        
    }
    Process_Data_Deinit();
    return nullptr;
}

/**
 * @brief 告警模块的消息释放处理函数
 */
void alarm_msg_release_handler(Common_Msg_t* msg)
{
    (void*) msg;
}

/**
 * @brief 告警模块的消息处理函数
 * @param msg 接收到的消息
 *
 * 支持 MSG_TYPE_IMAGE：拷贝图像帧到处理缓冲区并唤醒处理线程
 */
void alarm_msg_handler(Common_Msg_t* msg)
{
     switch(msg->msg_type){
        case MSG_TYPE_IMAGE:{
            Image_Data* img_data = (Image_Data*)msg->data;
            pthread_mutex_lock(&process_data.lock);
            memcpy(process_data.frame_buffer, img_data->data, img_data->len);
            process_data.frame_len = img_data->len;
            process_data.is_updated = true;
            pthread_cond_signal(&process_data.cond);
            pthread_mutex_unlock(&process_data.lock);
            break;
        }
        case MSG_TYPE_ALARM:
        case MSG_TYPE_LOG:
        case MSG_TYPE_COMMAND:
        default:
            break;
    }
}

/**
 * @brief 唤醒图像处理线程（用于程序退出时）
 */
void alarm_thread_wakeup(void)
{
    pthread_mutex_lock(&process_data.lock);
    pthread_cond_signal(&process_data.cond);
    pthread_mutex_unlock(&process_data.lock);
}

#ifdef __cplusplus
}
#endif