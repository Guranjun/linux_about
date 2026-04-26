#ifndef IMAGE_PROCESSOR_HPP
#define IMAGE_PROCESSOR_HPP

#ifdef __cplusplus
extern "C"{
#endif
#include <pthread.h>    // 必须包含，用于互斥锁和条件变量
#ifdef __cplusplus
}
#endif
extern "C" void* process_image_thread(void* arg) ;



#endif // IMAGE_PROCESSOR_HPP