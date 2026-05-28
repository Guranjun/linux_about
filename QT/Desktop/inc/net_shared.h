#ifndef NET_SHARED_H
#define NET_SHARED_H

#include <stdint.h>

#if defined(_MSC_VER)
#pragma pack(push, 1)
#endif
struct Frame_Header{
    uint16_t magic;		//帧头标志
    uint16_t data_len;	//数据长度
    uint32_t frame_id;	//帧ID
    uint16_t pkg_cnt;	//分包总数
    uint16_t pkg_id;	//分包ID
    uint32_t type;     //数据类型
    uint64_t timestamp;	//时间戳
    uint32_t reserved1; //4字节的保留位
    uint16_t reserved2; //2字节的保留位
    uint16_t crc;       //CRC检测码
}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((packed))
#endif
;
#if defined(_MSC_VER)
#pragma pack(pop)
#endif

#endif // NET_SHARED_H
