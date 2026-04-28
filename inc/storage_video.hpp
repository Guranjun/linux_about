#ifndef __STORAGE_VIDEO_HPP
#define __STORAGE_VIDEO_HPP

#ifdef __cplusplus
extern "C" {
#endif
#include <pthread.h>
void* storage_video_thread(void* arg);
void push_frame_to_storage_buffer(Image_Data* data, time_t timestamp);
void init_storage_buffer(void);

#ifdef __cplusplus
}
#endif


#endif // __STORAGE_VIDEO_HPP