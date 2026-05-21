import socket
import struct
import time
import cv2
import numpy as np

# ================= 配置参数 =================
TCP_IP = "0.0.0.0"
TCP_PORT = 8080

HEADER_FMT = "<H I H H H I Q" 
HEADER_SIZE = struct.calcsize(HEADER_FMT) # 严格 24 字节
MAGIC_BYTES = b'\xcd\xab'                  # 0xABCD 小端字节序

def handle_client(client_sock, client_addr):
    print(f"连接成功: {client_addr[0]}:{client_addr[1]}")
    
    stream_buffer = bytearray()
    frame_buffer = {}
    max_f_id = -1
    
    # 核心控制变量：是否已经抓到了首帧头
    has_found_first_packet = False
    
    prev_hw_timestamp = 0.0
    fps = 0.0

    while True:
        try:
            data = client_sock.recv(65536)
            if not data:
                print("连接断开。")
                break
            stream_buffer.extend(data)
        except Exception as e:
            print(f"网络异常: {e}")
            break

        while len(stream_buffer) >= HEADER_SIZE:
            # 1. 滑动寻找魔数 0xABCD
            if stream_buffer[0:2] != MAGIC_BYTES:
                idx = stream_buffer.find(MAGIC_BYTES)
                if idx == -1:
                    stream_buffer = stream_buffer[-1:]
                    break
                else:
                    del stream_buffer[:idx]
                    if len(stream_buffer) < HEADER_SIZE:
                        break

            # 2. 提取 24 字节帧头进行业务合法性校验
            header_data = bytes(stream_buffer[:HEADER_SIZE])
            magic, f_id, p_cnt, p_id, d_len, f_type, ts = struct.unpack(HEADER_FMT, header_data)

            # 严格过滤由于中途切入把图片内容误认作 Magic 的情况
            if d_len == 0 or d_len > 65535 or p_cnt == 0 or p_cnt > 1000 or p_id >= p_cnt:
                del stream_buffer[2:] # 假魔数，弹开前两个字节继续找
                continue

            # 3. 检查后续载荷数据是否已经接收完整
            if len(stream_buffer) < HEADER_SIZE + d_len:
                break # 数据不够，等待下一次 recv

            # 4. 【核心实现你的想法】：真包冷启动对齐检测
            if not has_found_first_packet:
                # 如果还没建立首帧同步，我们只认当前大帧的第 0 个切片 (pkg_id == 0)
                if p_id != 0:
                    # 按照你的思路：不是首包，说明是中途切入的残缺帧残渣，直接释放/弹出
                    # print(f"中途切入，抛弃残缺帧 {f_id} 的第 {p_id} 包")
                    del stream_buffer[: HEADER_SIZE + d_len]
                    continue
                else:
                    # 抓到首包了！正式激活存储和解析状态
                    print(f"成功捕捉到全新帧的首包 (Frame: {f_id})")
                    has_found_first_packet = True

            # 5. 精准切下这一片有效载荷并从流中清除
            payload = bytes(stream_buffer[HEADER_SIZE : HEADER_SIZE + d_len])
            del stream_buffer[: HEADER_SIZE + d_len]

            # 6. 常规过滤历史过期帧（比如重连时引入的旧数据）
            if f_id < max_f_id:
                continue

            if f_id > max_f_id:
                max_f_id = f_id
                frame_buffer[f_id] = {'type': f_type, 'pkgs': [None] * p_cnt}

            if p_id < p_cnt and f_id in frame_buffer:
                frame_buffer[f_id]['pkgs'][p_id] = payload

            # 7. 组包与图像渲染
            current_frame = frame_buffer[max_f_id]
            if all(p is not None for p in current_frame['pkgs']):
                full_jpg = b"".join(current_frame['pkgs'])
                
                img = cv2.imdecode(np.frombuffer(full_jpg, np.uint8), cv2.IMREAD_COLOR)
                if img is not None:
                    # 硬件时间戳校准防抖
                    if prev_hw_timestamp > 0:
                        time_diff = (ts - prev_hw_timestamp) / 1000000.0
                        if 0 < time_diff < 10.0:
                            real_fps = 1.0 / time_diff
                            fps = (fps * 0.9) + (real_fps * 0.1)
                    
                    prev_hw_timestamp = ts

                    cv2.putText(img, f"FPS: {int(fps)}", (20, 40), 
                                cv2.FONT_HERSHEY_SIMPLEX, 1.0, (0, 255, 0), 2)
                    cv2.imshow("TCP Pure HW-FPS Stream", img)

                del frame_buffer[max_f_id]

        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    client_sock.close()
    cv2.destroyAllWindows()

def start_receiver():
    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_sock.bind((TCP_IP, TCP_PORT))
    server_sock.listen(5)
    print(f"正在监听端口: {TCP_PORT} ...")
    try:
        while True:
            client_sock, client_addr = server_sock.accept()
            handle_client(client_sock, client_addr)
    finally:
        server_sock.close()

if __name__ == "__main__":
    start_receiver()