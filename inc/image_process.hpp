#ifndef IMAGE_PROCESSOR_HPP
#define IMAGE_PROCESSOR_HPP

#ifdef __cplusplus
extern "C"{
#endif
#include <pthread.h>    // 必须包含，用于互斥锁和条件变量
void* process_image_thread(void* arg) ;
#ifdef __cplusplus
}
#endif




#endif // IMAGE_PROCESSOR_HPP