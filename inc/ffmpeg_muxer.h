#ifndef FFMPEG_MUXER_H
#define FFMPEG_MUXER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化封装器
 * @param filename 保存的文件路径 (.avi)
 * @param width    视频宽
 * @param height   视频高
 * @param fps      基准帧率（影响播放器初始识别，实际由时间戳决定）
 */
int ffmpeg_muxer_init(const char* filename, int width, int height, int fps);

/**
 * @brief 写入一帧图像
 * @param data      JPEG数据指针
 * @param len       数据长度
 * @param timestamp_us  V4L2 传出来的系统微秒时间戳 (usec)
 */
int ffmpeg_muxer_write(uint8_t* data, size_t len, int64_t timestamp_us);

/**
 * @brief 关闭文件，释放资源
 */
void ffmpeg_muxer_close(void);

#ifdef __cplusplus
}
#endif

#endif