#include <sys/time.h>
#include <stdio.h>
#include "my_time.h"

/**
 * @brief 获取当前微秒时间戳
 * @return 微秒级时间戳
 */
uint64_t gettime_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
}

/**
 * @brief 获取当前毫秒时间戳
 * @return 毫秒级时间戳
 */
uint64_t gettime_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec;
}