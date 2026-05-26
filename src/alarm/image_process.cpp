#include "image_process.hpp"
#include "msg_about.h"

using namespace std;
using namespace cv;

static Mat prev_frame_gray;

/**
 * @brief 帧差法运动检测
 * @param input_frame 输入彩色图像帧
 * @return 告警数据结构体（SAFE 或 MOVED）
 *
 * 算法步骤：
 * 1. 将当前帧转为灰度图
 * 2. 与上一帧灰度图做帧差（absdiff）
 * 3. 二值化（阈值 25），计算运动像素占比
 * 4. 超过 10% 判定为 MOVED
 */
Alarm_Data Move_Detect(Mat* input_frame)
{
    static bool is_first_frame = true;
    static Alarm_Data alarm_data;
    Mat current_frame_gray, frame_diff, thresh;

    cvtColor(*input_frame, current_frame_gray, COLOR_BGR2GRAY);

    if (is_first_frame) {
        prev_frame_gray = current_frame_gray.clone();
        is_first_frame = false;
        alarm_data.status = SAFE;
        return alarm_data;
    }

    absdiff(current_frame_gray, prev_frame_gray, frame_diff);
    threshold(frame_diff, thresh, 25, 255, THRESH_BINARY);

    double movement_percentage = (double)countNonZero(thresh) / (thresh.rows * thresh.cols);

    if(movement_percentage > 0.1f){
        alarm_data.status = MOVED;
    }
    else{
        alarm_data.status = SAFE;
    }

    current_frame_gray.copyTo(prev_frame_gray);
    return alarm_data;
}

/**
 * @brief 比较告警状态是否发生变化
 * @param data 当前告警状态
 * @return true=状态已变化, false=状态未变
 *
 * 仅当告警状态切换时才返回 true，避免重复发送相同状态的日志
 */
bool alarm_data_diff(Alarm_Data data)
{
    static Alarm_Data alarm_data;

    if(alarm_data.status != data.status){
        alarm_data.status = data.status;
        return true;
    }

    return false;
}