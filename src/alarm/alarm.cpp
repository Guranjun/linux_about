#include "alarm.hpp"
#include "image_process.hpp"
#include "common.h"
#include "msg_about.h"
#include "my_time.h"
#include <cstdint>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>    // 专门用于 cvtColor, threshold, putText 等图像处理
#include <opencv2/imgcodecs.hpp>  // 专门用于 imdecode 和 imencode (JPG 编解码)
#include <iostream>
#include <string.h>
#include <vector>
using namespace cv;
using namespace std;
//进行运动检测所需的全局变量
//static Mat prev_frame_gray; // 上一帧的灰度图像
//static bool is_first_frame = true; // 是否是第一帧的标志
/*图像处理功能*/
typedef struct{
    uint8_t frame_buffer[128 * 1024];
    uint32_t frame_len;
    Log_Msg_t log_msg;
    bool is_updated;
    pthread_mutex_t lock;
    pthread_cond_t cond;
}Process_Data;
static Process_Data process_data; 
static void Process_Data_Init(void)
{
    //process_data.frame_buffer = malloc(128 * 1024);
    process_data.frame_len = 0;
    process_data.is_updated = false;
    memset(&process_data.log_msg, 0, sizeof(process_data.log_msg));
    pthread_mutex_init(&process_data.lock, NULL);
    pthread_cond_init(&process_data.cond, NULL);
}
static void Process_Data_Deinit(void)
{
    //free(process_data.frame_buffer);
    msg_unregister_module(MODULE_ID_ALARM);
    pthread_mutex_destroy(&process_data.lock);
    pthread_cond_destroy(&process_data.cond);
}
#ifdef __cplusplus
extern "C" 
{
#endif
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
            break; // 如果应用正在停止，提前退出循环
        }
        process_data.is_updated = false;
        uint8_t* data_to_process = process_data.frame_buffer;
        uint32_t frame_len = process_data.frame_len;
        pthread_mutex_unlock(&process_data.lock);
        /*第二步：进行图像处理逻辑*/
        /*先将jpg数据转成cv2能读懂的图像数据形式*/
        Mat raw_data_mat( 1, frame_len, CV_8UC1, data_to_process);
        Mat img = imdecode(raw_data_mat, IMREAD_COLOR);
        if(img.empty()){
            cerr << "Failed to decode image" << endl;
            //写个日志
            continue;
        }

        // 这里可以添加任何图像处理算法
        //Move_Detectiom(&img);
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

void alarm_msg_release_handler(Common_Msg_t* msg)
{
    (void*) msg;
}
void alarm_msg_handler(Common_Msg_t* msg)
{
     switch(msg->msg_type){
        case MSG_TYPE_IMAGE:{
            //处理图像数据消息
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
            //处理告警数据消息
            break;
        case MSG_TYPE_LOG:
            //处理日志数据消息
            break;
        case MSG_TYPE_COMMAND:
            //处理命令数据消息
            break;
        default:
            break;
    }
}
void alarm_thread_wakeup(void)
{
    pthread_mutex_lock(&process_data.lock);
    pthread_cond_signal(&process_data.cond);
    pthread_mutex_unlock(&process_data.lock);
}
#ifdef __cplusplus
}
#endif
