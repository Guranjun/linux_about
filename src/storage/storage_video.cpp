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
/*存储图像缓冲区*/
static bool move_detected;
typedef struct {
    // A 块和 B 块分别作为写入和读取的交换空间
    vector<uint8_t> buffer_A[MAXSIZE];
    uint32_t lens_A[MAXSIZE];
    uint64_t ts_A[MAXSIZE];

    vector<uint8_t> buffer_B[MAXSIZE];
    uint32_t lens_B[MAXSIZE];
    uint64_t ts_B[MAXSIZE];

    // 指针切换：指向当前正在“写”的块和“待读”的块
    vector<uint8_t> (*write_ptr)[MAXSIZE];
    uint32_t *write_lens_ptr;
    uint64_t *write_ts_ptr;

    vector<uint8_t> (*read_ptr)[MAXSIZE];
    uint32_t *read_lens_ptr;
    uint64_t *read_ts_ptr;
    Log_Msg_t log_msg;
    int write_idx; 
    bool data_ready; // 标志位：读块是否装满了数据待存

    pthread_mutex_t lock; // 保护索引切换和标志位
    pthread_cond_t cond;
} Storage_Data;
static Storage_Data storage_data ;
static bool is_recording = false; //视频录制状态
static uint8_t post_record_count = 0;//延迟录制用

/*视频存储线程*/
#ifdef __cplusplus
extern "C" 
{
#endif
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
void* storage_video_thread(void* arg)
{
    /*存储线程逻辑
    *第一步：初始化存储缓冲区
    *第二步：检测当前的危险状态，如果处于危险状态同时视频录制状态为false，开始存储视频，初始化视频头
    *第三步：检测当前危险状态，如果处于危险状态且视频录制状态为true，将缓冲区里的旧数据全都存到文件中
    *第四步：检测当前危险状态，如果不处于危险状态且视频录制状态为true，开始延迟存储100帧数据，存储完毕后关闭视频文件，同时置位视频录制状态为false
    */
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
        //一些操作
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
                    //写个日志
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
                    //写个日志
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
                //fflush(fp);
            }
            if(!move_detected && post_record_count > 0){
                post_record_count--;
                if(post_record_count == 0){
                    if(fp){
                        fclose(fp);
                        fp = nullptr;
                    }
                    is_recording = false;
                    //写个日志
                    log_make(&storage_data.log_msg, INFO, gettime_us(), MODULE_ID_STORAGE, "Stored");
                    msg_dispatch(MODULE_ID_STORAGE, MODULE_ID_LOGGER, sizeof(storage_data.log_msg), MSG_TYPE_LOG, &storage_data.log_msg);
                }
            } 
#endif
        }
        else{
            //如果还在录且fp没关，把fp关了
            if(is_recording && fp){
                fclose(fp);
                fp = nullptr;
                is_recording = false;
                //写个日志
                log_make(&storage_data.log_msg, INFO, gettime_us(), MODULE_ID_STORAGE, "File closed");
                msg_dispatch(MODULE_ID_STORAGE, MODULE_ID_LOGGER, sizeof(storage_data.log_msg), MSG_TYPE_LOG, &storage_data.log_msg);
            }
        }


    }
    if(fp)
        fclose(fp);
    msg_unregister_module(MODULE_ID_STORAGE);
}


void storage_msg_handler(Common_Msg_t* msg)
{
    switch(msg->msg_type){
        case MSG_TYPE_IMAGE:{
            //处理图像数据消息
            Image_Data* img_data = (Image_Data*)msg->data;
            pthread_mutex_lock(&storage_data.lock);
            int idx = storage_data.write_idx;
            auto& target_vec = (*storage_data.write_ptr)[idx];
            target_vec.assign((uint8_t*)img_data->data, (uint8_t*)img_data->data + img_data->len);
            storage_data.write_lens_ptr[idx] = img_data->len;
            storage_data.write_ts_ptr[idx] = img_data->timestamps;
            storage_data.write_idx++;
            
            //存数据
            if(storage_data.write_idx >= MAXSIZE){
                if(storage_data.data_ready){
                    storage_data.write_idx = 0;
                    //写个日志
                    log_make(&storage_data.log_msg, LOG_ERROR, gettime_us(), MODULE_ID_STORAGE, "Failed");
                    msg_dispatch(MODULE_ID_STORAGE, MODULE_ID_LOGGER, sizeof(storage_data.log_msg), MSG_TYPE_LOG, &storage_data.log_msg);
                }
                else{
                    //交换两个缓冲区指针，并重置writeidx，标记数据已准备好
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
            //处理告警数据消息
            //如果是movedetected就标记movedetect为true  不然标记为false
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
            //处理日志数据消息
            break;
        case MSG_TYPE_COMMAND:
            //处理命令数据消息
            break;
        case MSG_TYPE_BIGDATA:
            //处理大数据消息
            break;
        default:
            break;
    }
}
void storage_thread_wakeup(void)
{
    pthread_mutex_lock(&storage_data.lock);
    pthread_cond_signal(&storage_data.cond);
    pthread_mutex_unlock(&storage_data.lock);
}
#ifdef __cplusplus
}
#endif
