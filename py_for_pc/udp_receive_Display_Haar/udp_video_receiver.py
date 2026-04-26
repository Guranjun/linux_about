import socket
import struct
import cv2
import numpy as np
import threading
from queue import Queue
import os
# --- 配置 ---
UDP_IP = "0.0.0.0"
UDP_PORT = 8080
HEADER_FMT = "<H I H H H I"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
MAGIC_NUMBER = 0xABCD
base_path = os.path.dirname(__file__)
frame_queue = Queue(maxsize=1)
#--- 人脸识别线程 ---
# 该线程从队列中获取完整帧数据，进行人脸检测并显示结果
# 1. 加载 Haar Cascade 模型
# 2. 循环等待队列中的数据，进行人脸检测并显示结果
def face_recognition_worker():
    """ 
    人脸识别子线程：使用 OpenCV 自带的轻量级检测器
    """
    print("[Thread] 识别线程启动 (使用 OpenCV DNN)...")
    
    # 也可以用更简单的 Haar Cascade
    model_path = os.path.join(base_path, 'haarcascade_frontalface_default.xml')
    print(f"正在尝试加载: {model_path}")
    face_cascade = cv2.CascadeClassifier(model_path)
    print(f"模型加载状态: {not face_cascade.empty()}")
    while True:
        try:
            full_jpg = frame_queue.get()
            img = cv2.imdecode(np.frombuffer(full_jpg, np.uint8), cv2.IMREAD_COLOR)
            if img is None: continue

            gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
            
            # 检测人脸
            faces = face_cascade.detectMultiScale(gray, 1.1, 5, minSize=(30, 30))

            
            for (x, y, w, h) in faces:
                cv2.rectangle(img, (x, y), ((x+w), (y+h)), (0, 255, 0), 2)
                cv2.putText(img, "Face", (x, y-10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 2)
            
            cv2.imshow("UDP Stream - Face Detection", img)
            if cv2.waitKey(1) & 0xFF == ord('q'): break
        except Exception as e:
            print(f"Worker Error: {e}")
    cv2.destroyAllWindows()
# 主线程：接收 UDP 数据并组装成完整帧
# 1. 创建 UDP Socket
# 2. 循环接收数据包，解析头部信息，按帧 ID 组装数据
# 3. 当完整帧组装完成后，放入队列供识别线程处理
def start_receiver():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024 * 1024) 

    frame_buffer = {}
    max_f_id = -1

    t = threading.Thread(target=face_recognition_worker, daemon=True)
    t.start()

    print(f"[Main] 正在接收数据...")

    while True:
        try:
            sock.setblocking(False)
            while True:
                try:
                    data, addr = sock.recvfrom(4096)
                    header = struct.unpack(HEADER_FMT, data[:HEADER_SIZE])
                    magic, f_id, p_cnt, p_id, d_len, ts = header
                    if magic != MAGIC_NUMBER or f_id < max_f_id: continue
                    
                    if f_id > max_f_id:
                        max_f_id = f_id
                        frame_buffer.clear() 
                        frame_buffer[f_id] = [None] * p_cnt
                    
                    frame_buffer[f_id][p_id] = data[HEADER_SIZE:]
                except BlockingIOError:
                    break 

            if max_f_id in frame_buffer and all(p is not None for p in frame_buffer[max_f_id]):
                full_jpg = b"".join(frame_buffer[max_f_id])
                if not frame_queue.full():
                    frame_queue.put(full_jpg)
                else:
                    try:
                        frame_queue.get_nowait()
                        frame_queue.put(full_jpg)
                    except: pass
                frame_buffer.clear()

        except Exception:
            continue

if __name__ == "__main__":
    start_receiver()