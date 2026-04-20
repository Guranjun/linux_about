# 摄像头数据采集和UDP传输程序

## 功能说明

该程序实现了以下功能：

### 1. **持续读写摄像头数据**
   - 使用V4L2 API采集摄像头视频流
   - 分辨率: 640x480
   - 格式: MJPEG
   - 支持4个缓冲区的循环使用

### 2. **UDP网络传输**
   - 通过UDP协议将摄像头数据发送到上位机
   - 目标IP: 192.168.1.110
   - 目标端口: 8080
   - 支持持续式传输（按Ctrl+C停止）

### 3. **数据帧头功能**
   - 为每一帧数据添加帧头信息
   - 帧头结构（共18字节）：
     - **magic** (2字节): 魔数 0xABCD，用于帧识别
     - **version** (2字节): 协议版本号（当前为1）
     - **frame_number** (4字节): 帧计数，从0开始
     - **data_length** (4字节): 后续视频数据的长度
     - **timestamp** (4字节): 时间戳（毫秒）
     - **checksum** (2字节): 校验和，用于错误检测

### 4. **数据包结构**
   ```
   UDP数据包 = 帧头(18字节) + 摄像头MJPEG数据(可变长)
   ```

### 5. **优雅关闭**
   - 支持Ctrl+C信号，能够安全地停止采集和清理资源
   - 自动关闭摄像头设备、取消内存映射

## 编译指令

### 方法1：基本编译
```bash
gcc -o video_data_get video_data_get.c
```

### 方法2：添加详细警告信息
```bash
gcc -Wall -Wextra -o video_data_get video_data_get.c
```

### 方法3：带调试符号（用于GDB调试）
```bash
gcc -g -Wall -Wextra -o video_data_get video_data_get.c
```

## 使用方法

### 基本使用
```bash
./video_data_get /dev/video0
```

其中 `/dev/video0` 是摄像头设备文件。如果有多个摄像头，可以使用：
- `/dev/video0` - 第一个摄像头
- `/dev/video1` - 第二个摄像头
- 依此类推

### 运行示例
```bash
$ ./video_data_get /dev/video0
width = 640
width = 480
pixelformat = MJPG
Starting continuous video capture...
Frame header size: 18 bytes
Sent frame: 0, size: 8534 bytes, total header+data: 8552 bytes
Sent frame: 30, size: 8721 bytes, total header+data: 8739 bytes
Sent frame: 60, size: 8645 bytes, total header+data: 8663 bytes
...
```

### 停止程序
按 `Ctrl+C` 按键停止程序，程序将：
1. 停止摄像头采集
2. 关闭摄像头设备
3. 取消内存映射
4. 关闭UDP连接
5. 显示总共发送的帧数

## 上位机接收端设计建议

### 数据包解析流程
1. 接收18字节的帧头
2. 检查magic字段是否为0xABCD
3. 读取data_length字段获取数据大小
4. 接收相应大小的MJPEG视频数据
5. 可选：根据checksum验证数据完整性

### Python示例代码（伪代码）
```python
import socket
import struct

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('192.168.1.110', 8080))

while True:
    # 接收帧头
    frame_header, _ = sock.recvfrom(18)
    magic, version, frame_num, data_len, timestamp, checksum = \
        struct.unpack('<HHIIIH', frame_header)
    
    if magic != 0xABCD:
        print("Invalid frame header")
        continue
    
    # 接收视频数据
    video_data, _ = sock.recvfrom(data_len)
    
    # 处理数据（保存、显示等）
    print(f"Frame {frame_num}, size: {len(video_data)} bytes")
```

## 关键改进说明

### 与原版本的主要差异

| 特性 | 原版本 | 改进版本 |
|------|--------|---------|
| 采集模式 | 单帧采集 | 持续循环采集 |
| 数据头 | 无 | 有（18字节额外信息） |
| 错误处理 | 单次失败直接退出 | 失败继续采集 |
| 关闭方式 | 自动关闭 | 支持Ctrl+C安全关闭 |
| 日志信息 | 最少 | 定期输出采集状态 |
| 文件保存 | 保存到本地文件 | 纯网络传输 |

## 常见问题

### 1. 如何获取可用的摄像头设备？
```bash
ls -l /dev/video*
```

### 2. 程序运行报错"打开设备失败"？
- 检查摄像头设备是否存在
- 检查用户权限（可能需要sudo运行）
- 检查摄像头是否被其他程序占用

### 3. 如何修改目标IP和端口？
编辑代码中的这两行：
```c
ClientAddr.sin_port = htons(8080);           // 修改端口
ClientAddr.sin_addr.s_addr = inet_addr("192.168.1.110");  // 修改IP
```

### 4. 帧头校验和如何计算？
校验和是除开checksum字段外所有字节的和。接收端可用于验证数据完整性。

### 5. 如何调整视频分辨率或格式？
编辑代码中的这部分：
```c
vfmt.fmt.pix.width = 640;                    // 修改宽度
vfmt.fmt.pix.height = 480;                   // 修改高度
vfmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;  // 修改格式
```

## 性能指标

- **帧头开销**: 18字节/帧
- **最大分辨率**: 640x480 (可调)
- **采集格式**: MJPEG (可调)
- **缓冲区数**: 4个
- **传输方式**: UDP (无丢包重试)

## 注意事项

1. UDP协议无保证传输，对于关键应用可考虑添加接收端确认机制
2. 大的MJPEG数据可能会被UDP分片，接收端需要处理IP分片重组
3. 网络带宽充足的情况下建议使用TCP以保证可靠性
4. 程序未添加数据压缩，建议在带宽有限的场景下考虑压缩

## 许可和版本

- 版本: 1.0
- 修改日期: 2026-04-20
- 平台: Linux
- 依赖: V4L2库, 标准C库
