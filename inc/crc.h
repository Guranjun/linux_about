#ifndef __CRC16_H_
#define __CRC16_H_

#include <stdint.h>
#include <stddef.h>

// 计算包含指定长度数据的 CRC16 值
uint16_t crc16_ccitt(const uint8_t *data, size_t length);

#endif // __CRC16_H_