#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <opencv2/opencv.hpp>
#include "common.h"
#include "msg_about.h"
#include "storage_video.hpp"
#include "ffmpeg_muxer.hpp"
#include "my_time.h"
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace std;
using namespace cv;
#define MAXSIZE 50
#define STORE_IN_DAT 0
#define FPS 10

static bool move_detected;

/**
 * @brief 视频存储双缓冲数据结构体
 */
typedef struct {
    vector<uint8_t> buffer_A[MAXSIZE]; /**< A 块缓冲区 */
    uint32_t lens_A[MAXSIZE];
    uint64_t ts_A[MAXSIZE];

    vector<uint8_t> buffer_B[MAXSIZE]; /**< B 块缓冲区 */
    uint32_t lens_B[MAXSIZE];
    uint64_t ts_B[MAXSIZE];

    vector<uint8_t> (*write_ptr)[MAXSIZE]; /**< 当前正在写入的缓冲区指针 */
    uint32_t *write_lens_ptr;
    uint64_t *write_ts_ptr;

    vector<uint8_t> (*read_ptr)[MAXSIZE];  /**< 当前待读取的缓冲区指针 */
    uint32_t *read_lens_ptr;
    uint64_t *read_ts_ptr;
    Log_Msg_t log_msg;
    int write_idx;          /**< 当前写入位置 */
    bool data_ready;        /**< 读块是否已装满待处理 */

    pthread_mutex_t lock;
    pthread_cond_t cond;
} Storage_Data;

static Storage_Data storage_data;
static bool is_recording = false;
static uint8_t post_record_count = 0;

#ifdef __cplusplus
extern "C" 
{
#endif

/**
 * @brief 初始化存储数据缓冲区
 */
static void Storage_Data_Init(void)
{
    storage_data.write_ptr = &storage_data.buffer_A;
    storage_data.write_lens_ptr = storage_data.lens_A;
    storage_data.write_ts_ptr = storage_data.ts_A;
    storage_data.read_ptr = &storage_data.buffer_B;
    storage_data.read_lens_ptr = storage_data.lens_B;
    storage_data.read_ts_ptr = storage_data.ts_B;
    storage_data.write_idx = 0;
    storage_data.data_ready = false;
    memset(&storage_data.log_msg, 0, sizeof(storage_data.log_msg));
    for(int i = 0;i < MAXSIZE; i++){
        storage_data.buffer_A[i].reserve(128 * 1024);
        storage_data.buffer_B[i].reserve(128 * 1024);
    }
    pthread_mutex_init(&storage_data.lock, NULL);
    pthread_cond_init(&storage_data.cond, NULL);
}

/**
 * @brief 视频存储线程入口
 * @param arg 传入参数（不使用）
 * @return NULL
 *
 * 根据运动检测结果录制视频：
 * - 检测到移动时开始录制 .avi 文件
 * - 移动停止后延迟关闭
 * - 支持 STORE_IN_DAT 宏切换到 .dat 模式
 */
void* storage_video_thread(void* arg)
{
    FILE* fp = nullptr;
    Storage_Data_Init();
    while(running){
        pthread_mutex_lock(&storage_data.lock);
        while(!storage_data.data_ready && running){
            pthread_cond_wait(&storage_data.cond, &storage_data.lock);
        }
        if(!running){
            pthread_mutex_unlock(&storage_data.lock);
            break;
        }
        bool should_save = move_detected || (post_record_count > 0);
        storage_data.data_ready = false;
        pthread_mutex_unlock(&storage_data.lock);
        if(should_save){
#if STORE_IN_DAT == 0
            if(!is_recording){
                char filename[64];
                uint64_t raw_time = storage_data.read_ts_ptr[0];
                time_t file_name = (time_t)raw_time/1000000;
                struct tm* timeinfo = localtime(&file_name);
                strftime(filename, sizeof(filename), "/mnt/sdcard/rec_%Y%m%d_%H%M%S.avi", timeinfo);
                if(ffmpeg_muxer_init(filename, FRAME_WIDTH, FRAME_HIGH, FPS) >= 0){
                    is_recording = true;
                    printf("%s is inited!\n", filename);
                    log_make(&storage_data.log_msg, INFO, gettime_us(), MODULE_ID_STORAGE, "Start to store");
                    msg_dispatch(MODULE_ID_STORAGE, MODULE_ID_LOGGER, sizeof(storage_data.log_msg), MSG_TYPE_LOG, &storage_data.log_msg);
                }
                else{
                    log_make(&storage_data.log_msg, INFO, gettime_us(), MODULE_ID_STORAGE, "Create file failed!");
                    msg_dispatch(MODULE_ID_STORAGE, MODULE_ID_LOGGER, sizeof(storage_data.log_msg), MSG_TYPE_LOG, &storage_data.log_msg);
                }
            }
            if(is_recording){
                for(int i = 0; i < MAXSIZE; i++){
                    ffmpeg_muxer_write((*storage_data.read_ptr)[i].data(), storage_data.read_lens_ptr[i], storage_data.read_ts_ptr[i]);
                }
            }
            if(!move_detected && post_record_count > 0){
                post_record_count--;
                if(post_record_count == 0){
                    ffmpeg_muxer_close();
                    is_recording = false;
                    log_make(&storage_data.log_msg, INFO, gettime_us(), MODULE_ID_STORAGE, "Stored");
                    msg_dispatch(MODULE_ID_STORAGE, MODULE_ID_LOGGER, sizeof(storage_data.log_msg), MSG_TYPE_LOG, &storage_data.log_msg);
                }
            } 
#else
            if(!is_recording){
                char filename[64];
                uint64_t raw_time = storage_data.read_ts_ptr[0];
                time_t file_name = (time_t)raw_time/1000000;
                struct tm *timeinfo = localtime(&file_name);
                strftime(filename, sizeof(filename), "/mnt/sdcard/rec_%Y%m%d_%H%M%S.dat", timeinfo);
                fp = fopen(filename, "wb");
                if(fp){
                    is_recording = true;
                    log_make(&storage_data.log_msg, INFO, gettime_us(), MODULE_ID_STORAGE, "Start to store");
                    msg_dispatch(MODULE_ID_STORAGE, MODULE_ID_LOGGER, sizeof(storage_data.log_msg), MSG_TYPE_LOG, &storage_data.log_msg);
                }
                else{
                    perror("Failed To Open File:");
                }
            }
            if(fp){
                for(int i = 0; i < MAXSIZE; i++){
                    uint32_t len = storage_data.read_lens_ptr[i];
                    fwrite((*storage_data.read_ptr)[i].data(), 1, len, fp);
                }
            }
            if(!move_detected && post_record_count > 0){
                post_record_count--;
                if(post_record_count == 0){
                    if(fp){
                        fclose(fp);
                        fp = nullptr;
                    }
                    is_recording = false;
                    log_make(&storage_data.log_msg, INFO, gettime_us(), MODULE_ID_STORAGE, "Stored");
                    msg_dispatch(MODULE_ID_STORAGE, MODULE_ID_LOGGER, sizeof(storage_data.log_msg), MSG_TYPE_LOG, &storage_data.log_msg);
                }
            } 
#endif
        }
        else{
            if(is_recording && fp){
                fclose(fp);
                fp = nullptr;
                is_recording = false;
                log_make(&storage_data.log_msg, INFO, gettime_us(), MODULE_ID_STORAGE, "File closed");
                msg_dispatch(MODULE_ID_STORAGE, MODULE_ID_LOGGER, sizeof(storage_data.log_msg), MSG_TYPE_LOG, &storage_data.log_msg);
            }
        }
    }
    if(fp)
        fclose(fp);
    msg_unregister_module(MODULE_ID_STORAGE);
}

/**
 * @brief 存储模块的消息处理函数
 * @param msg 接收到的消息
 *
 * 支持:
 * - MSG_TYPE_IMAGE: 缓存图像帧
 * - MSG_TYPE_ALARM: 更新移动检测标志
 */
void storage_msg_handler(Common_Msg_t* msg)
{
    switch(msg->msg_type){
        case MSG_TYPE_IMAGE:{
            Image_Data* img_data = (Image_Data*)msg->data;
            pthread_mutex_lock(&storage_data.lock);
            int idx = storage_data.write_idx;
            auto& target_vec = (*storage_data.write_ptr)[idx];
            target_vec.assign((uint8_t*)img_data->data, (uint8_t*)img_data->data + img_data->len);
            storage_data.write_lens_ptr[idx] = img_data->len;
            storage_data.write_ts_ptr[idx] = img_data->timestamps;
            storage_data.write_idx++;
            
            if(storage_data.write_idx >= MAXSIZE){
                if(storage_data.data_ready){
                    storage_data.write_idx = 0;
                    log_make(&storage_data.log_msg, LOG_ERROR, gettime_us(), MODULE_ID_STORAGE, "Failed");
                    msg_dispatch(MODULE_ID_STORAGE, MODULE_ID_LOGGER, sizeof(storage_data.log_msg), MSG_TYPE_LOG, &storage_data.log_msg);
                }
                else{
                    swap(storage_data.write_ptr, storage_data.read_ptr);
                    swap(storage_data.write_lens_ptr, storage_data.read_lens_ptr);
                    swap(storage_data.write_ts_ptr, storage_data.read_ts_ptr);
                    storage_data.data_ready = true;
                    storage_data.write_idx = 0;
                    pthread_cond_signal(&storage_data.cond);
                }
            }
            pthread_mutex_unlock(&storage_data.lock);
            break;
        }
        case MSG_TYPE_ALARM:{
            Alarm_Data* alarm_data = (Alarm_Data*)msg->data;
            pthread_mutex_lock(&storage_data.lock);
            if(alarm_data->status == MOVED){
                move_detected = true;
                post_record_count = 1;
            }
            else{
                move_detected = false;
            }
            pthread_mutex_unlock(&storage_data.lock);
            break;
        }
        case MSG_TYPE_LOG:
        case MSG_TYPE_COMMAND:
        case MSG_TYPE_BIGDATA:
        default:
            break;
    }
}

/**
 * @brief 唤醒存储线程（用于程序退出时）
 */
void storage_thread_wakeup(void)
{
    pthread_mutex_lock(&storage_data.lock);
    pthread_cond_signal(&storage_data.cond);
    pthread_mutex_unlock(&storage_data.lock);
}

#ifdef __cplusplus
}
#endif