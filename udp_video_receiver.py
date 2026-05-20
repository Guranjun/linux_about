import socket
import struct
import cv2
import numpy as np
import time

# --- 配置 ---
UDP_IP = "0.0.0.0"
UDP_PORT = 8080

# --- 根据新结构体更新格式字符串 ---
# H=uint16_t(2B), I=uint32_t(4B), Q=uint64_t(8B)
# 对应: magic, frame_id, pkg_cnt, pkg_id, data_len, type, timestamp
HEADER_FMT = "<H I H H H I Q"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
MAGIC_NUMBER = 0xABCD

# 定义与C语言一致的枚举映射，方便调试和过滤数据
FILE_TYPE = {
    0: "NORMAL",
    1: "IMAGE",
    2: "DB",
    3: "VIDEO",
    4: "COMMOND"
}

def start_receiver():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    # 限制系统缓冲区，不让老数据堆积
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 512 * 1024) 

    frame_buffer = {}
    max_f_id = -1
    
    # --- FPS 相关变量 ---
    prev_time = 0
    fps = 0

    print(f"Listening on {UDP_IP}:{UDP_PORT}...")
    print(f"Header format: {HEADER_FMT}, Size: {HEADER_SIZE} bytes")

    while True:
        try:
            # 关键优化：非阻塞读取，一次性清空缓冲区里积压的所有老包
            sock.setblocking(False)
            while True:
                try:
                    data, addr = sock.recvfrom(2048)
                    if len(data) < HEADER_SIZE: continue

                    # 解包新结构体
                    header = struct.unpack(HEADER_FMT, data[:HEADER_SIZE])
                    magic, f_id, p_cnt, p_id, d_len, f_type, ts = header
                    
                    if magic != MAGIC_NUMBER: continue
                    
                    # 丢弃比当前看到的最新帧还要老的包
                    if f_id < max_f_id: continue
                    
                    if f_id > max_f_id:
                        max_f_id = f_id
                        frame_buffer.clear() # 发现新帧，旧的没拼完也直接扔了
                        frame_buffer[f_id] = {
                            'type': f_type,   # 记录这一帧的数据类型
                            'pkgs': [None] * p_cnt
                        }
                    
                    # 把有效载荷（去掉帧头）塞入对应的分包位置
                    # 注意：载荷长度推荐采用底层报文裁剪 data[HEADER_SIZE:HEADER_SIZE + d_len]
                    frame_buffer[f_id]['pkgs'][p_id] = data[HEADER_SIZE:]
                except BlockingIOError:
                    break # 缓冲区读完了，去处理拼好的数据
            
            # 检查当前最大帧 ID 是否有效且收齐
            if max_f_id in frame_buffer:
                current_frame = frame_buffer[max_f_id]
                
                if all(p is not None for p in current_frame['pkgs']):
                    # 只有类型是 IMAGE (1) 的时候才执行图像解码
                    if current_frame['type'] == 1: 
                        full_jpg = b"".join(current_frame['pkgs'])
                        img = cv2.imdecode(np.frombuffer(full_jpg, np.uint8), cv2.IMREAD_COLOR)
                        
                        if img is not None:
                            # --- 计算 FPS ---
                            curr_time = time.time()
                            time_diff = curr_time - prev_time
                            if time_diff > 0:
                                real_fps = 1 / time_diff
                                # 平滑处理：新的帧率 = 0.9 * 旧帧率 + 0.1 * 当前瞬时帧率
                                fps = (fps * 0.9) + (real_fps * 0.1)
                            prev_time = curr_time

                            # --- 绘制 FPS 到右下角 ---
                            fps_text = f"FPS: {int(fps)}"
                            h, w = img.shape[:2]
                            cv2.putText(img, fps_text, (w - 120, h - 20), 
                                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)
                            
                            cv2.imshow("Stream", img)
                    else:
                        # 如果是其他非图像数据类型（比如 DB、COMMOND），在这里做别的处理
                        # t_name = FILE_TYPE.get(current_frame['type'], "UNKNOWN")
                        # print(f"Received non-image data frame [{max_f_id}], type: {t_name}")
                        pass
                    
                    # 处理完当前帧后清理缓冲区
                    frame_buffer.clear()

            if cv2.waitKey(1) & 0xFF == ord('q'): break
        except Exception as e:
            # print(f"Error: {e}") # 调试时可以打开
            continue

    cv2.destroyAllWindows()
    sock.close()

if __name__ == "__main__":
    start_receiver()