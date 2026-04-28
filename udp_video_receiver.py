import socket
import struct
import cv2
import numpy as np
import time

# --- 配置 ---
UDP_IP = "0.0.0.0"
UDP_PORT = 8080
HEADER_FMT = "<H I H H H I"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
MAGIC_NUMBER = 0xABCD

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
                    # 参数：图像, 文本, 位置(左下角坐标), 字体, 大小, 颜色, 厚度
                    # 这里位置设为 (宽-120, 高-20) 左右
                    cv2.putText(img, fps_text, (w - 120, h - 20), 
                                cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)
                    
                    cv2.imshow("Stream", img)
                
                # 处理完当前帧后清理
                frame_buffer.clear()

            if cv2.waitKey(1) & 0xFF == ord('q'): break
        except Exception as e:
            # print(f"Error: {e}") # 调试时可以打开
            continue

    cv2.destroyAllWindows()
    sock.close()

if __name__ == "__main__":
    start_receiver()