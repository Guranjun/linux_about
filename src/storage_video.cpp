#include <cstdint>
#include <ctime>
#include <iostream>
#include <opencv2/opencv.hpp>
#include "common.h"
#include "storage_video.hpp"
//#include "ffmpeg_muxer.h"

#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace std;
using namespace cv;
/*存储图像缓冲区*/

typedef struct{
    vector<uint8_t> frames[50]; //存储50帧图像数据的指针数组
    uint32_t frame_lens[50]; //每帧图像数据的长度
    time_t frame_timestamps[50];//时间戳
    int head; //队列头索引
    int tail; //队列尾索引
    int count; //当前队列中的帧数
    int MaxSize; //队列的最大容量
    pthread_mutex_t lock;   // 必须有锁
    pthread_cond_t cond;    // 建议有条件变量，避免线程空转浪费 CPU
}Storage_Buffer;
static Storage_Buffer storage_buffer = {
    .head = 0,
    .tail = 0,
    .count = 0,
    .MaxSize = 50
};
static bool is_recording = false; //视频录制状态
static vector<uint8_t> local_frame_buffer; //本地帧数据缓冲区，避免频繁分配内存

/*视频存储线程*/
#ifdef __cplusplus
extern "C" 
{
#endif
void* storage_video_thread(void* arg)
{
    /*存储线程逻辑
    *第一步：初始化存储缓冲区
    *第二步：检测当前的危险状态，如果处于危险状态同时视频录制状态为false，开始存储视频，初始化视频头
    *第三步：检测当前危险状态，如果处于危险状态且视频录制状态为true，将缓冲区里的旧数据全都存到文件中
    *第四步：检测当前危险状态，如果不处于危险状态且视频录制状态为true，开始延迟存储100帧数据，存储完毕后关闭视频文件，同时置位视频录制状态为false
    */
    while(running){
        pthread_mutex_lock(&storage_buffer.lock);
        // 如果队列没数据，就等通知，不消耗 CPU
        while(storage_buffer.count == 0 && running) {
            pthread_cond_wait(&storage_buffer.cond, &storage_buffer.lock);
        }
        if(!running){
            pthread_mutex_unlock(&storage_buffer.lock);
            break;
        }
        storage_buffer.frames[storage_buffer.head].swap(local_frame_buffer);
        uint32_t len = storage_buffer.frame_lens[storage_buffer.head];
        time_t ts = storage_buffer.frame_timestamps[storage_buffer.head];
        storage_buffer.count--;
        pthread_mutex_unlock(&storage_buffer.lock);
        if(!is_recording == false/*&& danger_state == danger */){
            //开始存储视频，初始化视频头
            is_recording = true;
        }
        else if(is_recording == true/*&& danger_state == danger*/){
            //将缓冲区里的旧数据全都存到文件中
        }
        else if(is_recording == true/*&& danger_state != danger*/){
            //开始延迟存储100帧数据，存储完毕后关闭视频文件，同时置位视频录制状态为false
            is_recording = false;
        }
    }

}
/**
 * @brief 图像数据压入存储缓冲区队列
 * @param data 需传入的图像数据
 * @param timestamp 图像数据的时间戳
 */
void push_frame_to_storage_buffer(Image_Data* data, time_t timestamp)
{
    //将新帧数据推入存储缓冲区
    //1.获取锁，检查缓冲区状态
    //2.如果缓冲区已满，直接覆盖
    //3.将新帧数据复制到缓冲区中，更新索引和计数
    pthread_mutex_lock(&storage_buffer.lock);
    storage_buffer.frames[storage_buffer.tail].assign(data->data, data->data + data->len);
    storage_buffer.frame_lens[storage_buffer.tail] = data->len;
    storage_buffer.frame_timestamps[storage_buffer.tail] = timestamp;
    storage_buffer.tail = (storage_buffer.tail + 1) % storage_buffer.MaxSize;
    if(storage_buffer.count < storage_buffer.MaxSize){
        storage_buffer.count++;
    }
    else{
        //如果缓冲区已满，覆盖最旧的数据
        storage_buffer.head = (storage_buffer.head + 1) % storage_buffer.MaxSize;
    }
    pthread_mutex_unlock(&storage_buffer.lock);
}
/*初始化存储数据的队列*/
void init_storage_buffer(void)
{
    pthread_mutex_init(&storage_buffer.lock, NULL);
    pthread_cond_init(&storage_buffer.cond, NULL);
    local_frame_buffer.reserve(256*1024); // 同样预留空间
    for(int i = 0; i < 50; i++) {
        // 预留 256KB 空间。只要单帧 JPEG 不超过这个值，就不会产生任何碎片
        storage_buffer.frames[i].reserve(256 * 1024); 
    }
}
#ifdef __cplusplus
}
#endif