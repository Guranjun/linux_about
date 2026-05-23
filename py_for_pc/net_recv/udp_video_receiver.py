import socket
import struct
import cv2
import numpy as np
import time

UDP_IP = "0.0.0.0"
UDP_PORT = 8080
HEADER_FMT = "< H H I H H I Q I H H"
HEADER_SIZE = struct.calcsize(HEADER_FMT) # 32 字节
MAGIC_NUMBER = 0xABCD

def crc16_ccitt_xmodem(data: bytes) -> int:
    crc = 0x0000
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc

def start_receiver():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 512 * 1024) 

    frame_buffer = {}
    max_f_id = -1
    prev_time = 0
    fps = 0

    print(f"Listening on {UDP_IP}:{UDP_PORT}...")

    while True:
        try:
            sock.setblocking(False)
            while True:
                try:
                    data, addr = sock.recvfrom(2048)
                    if len(data) < HEADER_SIZE: continue

                    # ★★★ 终极防御：提取前 30 字节算出本地 CRC 校验和 ★★★
                    local_crc = crc16_ccitt_xmodem(data[:30])

                    # 解包新结构体
                    magic, d_len, f_id, p_cnt, p_id, f_type, ts, res1, res2, received_crc = struct.unpack(HEADER_FMT, data[:HEADER_SIZE])
                    
                    # 强密码双校验：魔数不对，或者 CRC 不对，直接一票否决
                    if magic != MAGIC_NUMBER or local_crc != received_crc: 
                        continue
                    
                    if f_id < max_f_id: continue
                    
                    if f_id > max_f_id:
                        max_f_id = f_id
                        frame_buffer.clear() 
                        frame_buffer[f_id] = {
                            'type': f_type,   
                            'pkgs': [None] * p_cnt
                        }
                    
                    if p_id < p_cnt and f_id in frame_buffer:
                        frame_buffer[f_id]['pkgs'][p_id] = data[HEADER_SIZE : HEADER_SIZE + d_len]
                        
                except BlockingIOError:
                    break 
            
            # 组包解码逻辑 (与之前一致，省略...)
            if max_f_id in frame_buffer:
                current_frame = frame_buffer[max_f_id]
                if all(p is not None for p in current_frame['pkgs']):
                    if current_frame['type'] == 1: 
                        full_jpg = b"".join(current_frame['pkgs'])
                        img = cv2.imdecode(np.frombuffer(full_jpg, np.uint8), cv2.IMREAD_COLOR)
                        if img is not None:
                            curr_time = time.time()
                            time_diff = curr_time - prev_time
                            if time_diff > 0:
                                real_fps = 1 / time_diff
                                fps = (fps * 0.9) + (real_fps * 0.1)
                            prev_time = curr_time
                            fps_text = f"FPS: {int(fps)}"
                            h, w = img.shape[:2]
                            cv2.putText(img, fps_text, (w - 120, h - 20), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)
                            cv2.imshow("Stream", img)
                    frame_buffer.clear()

            if cv2.waitKey(1) & 0xFF == ord('q'): break
        except Exception as e:
            continue

    cv2.destroyAllWindows()
    sock.close()

if __name__ == "__main__":
    start_receiver()