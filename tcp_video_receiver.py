import socket
import struct
import time
import cv2
import numpy as np

# --- 配置 ---
TCP_IP = "0.0.0.0"
TCP_PORT = 8080

# --- 根据新结构体更新格式字符串 ---
# H=uint16_t(2B), I=uint32_t(4B), Q=uint64_t(8B)
# 对应: magic, frame_id, pkg_cnt, pkg_id, data_len, type, timestamp
HEADER_FMT = "<H I H H H I Q"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
MAGIC_NUMBER = 0xABCD

# 定义与C语言一致的枚举映射
FILE_TYPE = {
    0: "NORMAL",
    1: "IMAGE",
    2: "DB",
    3: "VIDEO",
    4: "COMMOND"
}


def recv_exact(sock, size):
    """从 TCP 流中精确读取 size 字节，连接关闭时返回 None。"""
    chunks = bytearray()
    while len(chunks) < size:
        packet = sock.recv(size - len(chunks))
        if not packet:
            return None
        chunks.extend(packet)
    return bytes(chunks)


def draw_fps(img, fps):
    fps_text = f"FPS: {int(fps)}"
    h, w = img.shape[:2]
    cv2.putText(
        img,
        fps_text,
        (w - 120, h - 20),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.8,
        (0, 255, 0),
        2,
    )


def handle_client(client_sock, client_addr):
    frame_buffer = {}
    max_f_id = -1
    prev_time = 0.0
    fps = 0.0

    print(f"Client connected: {client_addr[0]}:{client_addr[1]}")
    print(f"Header format: {HEADER_FMT}, Size: {HEADER_SIZE} bytes")

    while True:
        # 1. 接收固定大小的帧头
        header_data = recv_exact(client_sock, HEADER_SIZE)
        if header_data is None:
            print("Client disconnected")
            break

        # 2. 解包新结构体
        magic, f_id, p_cnt, p_id, d_len, f_type, ts = struct.unpack(HEADER_FMT, header_data)
        if magic != MAGIC_NUMBER:
            print(f"Invalid magic: 0x{magic:04X}")
            continue

        # 3. 接收有效载荷
        payload = recv_exact(client_sock, d_len)
        if payload is None:
            print("Client disconnected while receiving payload")
            break

        # 丢弃历史过期帧
        if f_id < max_f_id:
            continue

        # 发现新帧，初始化缓冲区
        if f_id > max_f_id:
            max_f_id = f_id
            frame_buffer.clear()
            frame_buffer[f_id] = {
                'type': f_type,   # 存储当前帧的业务类型
                'pkgs': [None] * p_cnt
            }

        if p_id >= p_cnt:
            continue

        # 塞入当前分包
        frame_buffer[f_id]['pkgs'][p_id] = payload

        # 4. 检查当前帧是否全部收齐
        if max_f_id in frame_buffer:
            current_frame = frame_buffer[max_f_id]
            
            if all(p is not None for p in current_frame['pkgs']):
                # 核心过滤：只有类型是 IMAGE (1) 时，才去拼图并用 OpenCV 显示
                if current_frame['type'] == 1:
                    full_jpg = b"".join(current_frame['pkgs'])
                    img = cv2.imdecode(np.frombuffer(full_jpg, np.uint8), cv2.IMREAD_COLOR)

                    if img is not None:
                        curr_time = time.time()
                        time_diff = curr_time - prev_time
                        if time_diff > 0:
                            real_fps = 1.0 / time_diff
                            fps = (fps * 0.9) + (real_fps * 0.1)
                        prev_time = curr_time

                        draw_fps(img, fps)
                        cv2.imshow("TCP Stream", img)
                else:
                    # 如果是其他数据类型（例如：DB、COMMOND等），在此扩展相关业务逻辑
                    # type_name = FILE_TYPE.get(current_frame['type'], "UNKNOWN")
                    # print(f"Received non-image frame [{max_f_id}], type: {type_name}")
                    pass

                # 处理完当前帧后，清理一帧的缓冲区
                frame_buffer.clear()

                if cv2.waitKey(1) & 0xFF == ord("q"):
                    break

    client_sock.close()
    cv2.destroyAllWindows()


def start_receiver():
    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_sock.bind((TCP_IP, TCP_PORT))
    server_sock.listen(1)

    print(f"Listening on {TCP_IP}:{TCP_PORT}...")

    try:
        while True:
            client_sock, client_addr = server_sock.accept()
            handle_client(client_sock, client_addr)
    except KeyboardInterrupt:
        print("\nReceiver stopped")
    finally:
        server_sock.close()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    start_receiver()