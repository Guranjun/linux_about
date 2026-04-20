import socket
import struct
import cv2
import numpy as np

# --- 配置 ---
UDP_IP = "0.0.0.0"
UDP_PORT = 8080
HEADER_FMT = "<H I H H H I"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
MAGIC_NUMBER = 0xABCD

import socket
import struct
import cv2
import numpy as np

# ... (HEADER 定义同前) ...

def start_receiver():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", 8080))
    # 限制系统缓冲区，不让老数据堆积
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 512 * 1024) 

    frame_buffer = {}
    max_f_id = -1

    while True:
        try:
            # 关键优化：非阻塞读取，一次性清空缓冲区里积压的所有老包
            sock.setblocking(False)
            while True:
                try:
                    data, addr = sock.recvfrom(2048)
                    header = struct.unpack(HEADER_FMT, data[:HEADER_SIZE])
                    magic, f_id, p_cnt, p_id, d_len, ts = header
                    
                    if magic != MAGIC_NUMBER: continue
                    
                    # 丢弃比当前看到的最新帧还要老的包
                    if f_id < max_f_id: continue
                    
                    if f_id > max_f_id:
                        max_f_id = f_id
                        frame_buffer.clear() # 发现新帧，旧的没拼完也直接扔了
                        frame_buffer[f_id] = [None] * p_cnt
                    
                    frame_buffer[f_id][p_id] = data[HEADER_SIZE:]
                except BlockingIOError:
                    break # 缓冲区读完了，去处理拼好的图
            
            # 检查当前最大帧 ID 是否收齐
            if max_f_id in frame_buffer and all(p is not None for p in frame_buffer[max_f_id]):
                full_jpg = b"".join(frame_buffer[max_f_id])
                img = cv2.imdecode(np.frombuffer(full_jpg, np.uint8), cv2.IMREAD_COLOR)
                if img is not None:
                    cv2.imshow("Stream", img)
                frame_buffer.clear()

            if cv2.waitKey(1) & 0xFF == ord('q'): break
        except Exception:
            continue

    cv2.destroyAllWindows()

if __name__ == "__main__":
    start_receiver()