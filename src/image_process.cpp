#include "image_process.hpp"
#include "common.h"
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>    // 专门用于 cvtColor, threshold, putText 等图像处理
#include <opencv2/imgcodecs.hpp>  // 专门用于 imdecode 和 imencode (JPG 编解码)
#include <iostream>
#include <vector>
using namespace cv;
using namespace std;
//进行运动检测所需的全局变量
//static Mat prev_frame_gray; // 上一帧的灰度图像
//static bool is_first_frame = true; // 是否是第一帧的标志
/*图像处理功能*/
void Move_Detectiom(Mat* input_frame)
{
    static Mat prev_frame_gray; // 上一帧的灰度图像
    static bool is_first_frame = true; // 是否是第一帧的标志
    Mat current_frame_gray, frame_diff, thresh;
    // 将当前帧转换为灰度图像
    cvtColor(*input_frame, current_frame_gray, COLOR_BGR2GRAY);
    if (is_first_frame) {
        prev_frame_gray = current_frame_gray.clone();
        is_first_frame = false;
        return ;// 第一帧没有前一帧可比较，直接返回
    }
    // 计算当前帧与上一帧的差异
    absdiff(current_frame_gray, prev_frame_gray, frame_diff);
    // 对差异图像进行二值化处理，得到运动区域
    threshold(frame_diff, thresh, 25, 255, THRESH_BINARY);
    double movement_percentage = (double)countNonZero(thresh) / (thresh.rows * thresh.cols);
    if (movement_percentage > 0.02) { // 如果运动区域占比超过2%，认为有运动
        putText(*input_frame, "Motion Detected", Point(10, 30), FONT_HERSHEY_SIMPLEX, 1, Scalar(0, 0, 255), 2);
    }
    current_frame_gray.copyTo(prev_frame_gray); // 更新上一帧的灰度图像
    vector<uchar>encode_buf;
    imencode(".jpg", *input_frame, encode_buf);
    // 将encode_buf中的数据复制回共享缓冲区
    memcpy(camera_udp_shared_buffer.camera_data[camera_udp_shared_buffer.latest_index], encode_buf.data(), encode_buf.size());
    camera_udp_shared_buffer.frame_len[camera_udp_shared_buffer.latest_index] = encode_buf.size();
    
}
extern "C" void* process_image_thread(void* arg) 
{    
    while (running) {
        /*实现图像处理逻辑，注意细节，必须在检测到status为1时才进行图像处理并更新status，处理完成后需要通知发送线程有新数据可发送
        *第一步：获取锁，检测status标志位，如果正在发送则等待条件变量，直到发送线程完成发送并重置status为false
        *第二步：进行图像处理逻辑
        *第三步：获取锁，更新status，释放锁，通知发送线程有新数据可发送
        */
        /*第一步：获取锁，检测status标志位与latest_index，如果正在发送或最新帧还没有更新则等待条件变量
        *注意这里需要使用while循环来检查条件，以防止虚假唤醒导致的错误处理逻辑执行
        *同时需要更改status为2
        */
        pthread_mutex_lock(&camera_udp_shared_buffer.lock);
        while((camera_udp_shared_buffer.latest_index == -1 || camera_udp_shared_buffer.status != 1) && running){
            pthread_cond_wait(&camera_udp_shared_buffer.cond, &camera_udp_shared_buffer.lock);
        }
        if(!running){
            pthread_mutex_unlock(&camera_udp_shared_buffer.lock);
            break; // 如果应用正在停止，提前退出循环
        }
        camera_udp_shared_buffer.status = 2; // 数据正在处理
        int index_to_process = camera_udp_shared_buffer.latest_index;
        uint32_t frame_len = camera_udp_shared_buffer.frame_len[index_to_process];
        uint8_t* data_to_process = camera_udp_shared_buffer.camera_data[index_to_process];
        pthread_mutex_unlock(&camera_udp_shared_buffer.lock);
        /*第二步：进行图像处理逻辑*/
        /*先将jpg数据转成cv2能读懂的图像数据形式*/
        Mat raw_data_mat( 1, frame_len, CV_8UC1, data_to_process);
        Mat img = imdecode(raw_data_mat, IMREAD_COLOR);
        if(img.empty()){
            cerr << "Failed to decode image" << endl;
            camera_udp_shared_buffer.status = -1; // 处理失败，重置状态为-1，等待下一帧数据
            continue;
        }

        // 这里可以添加任何图像处理算法
        Move_Detectiom(&img);
        /*第三步：获取锁，更改status标志为3，释放锁，通知发送线程有新数据可发送*/
        pthread_mutex_lock(&camera_udp_shared_buffer.lock);
        camera_udp_shared_buffer.status = 3; // 数据处理完毕，待发送
        pthread_cond_signal(&camera_udp_shared_buffer.cond);
        pthread_mutex_unlock(&camera_udp_shared_buffer.lock);
    }
    return nullptr;
}