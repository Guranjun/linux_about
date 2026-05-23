import socket
import struct
import time
import cv2
import numpy as np

# ================= 配置参数 =================
TCP_IP = "0.0.0.0"
TCP_PORT = 8080

# < H H I H H I Q I H H ➔ 32 字节
HEADER_FMT = "< H H I H H I Q I H H" 
HEADER_SIZE = struct.calcsize(HEADER_FMT) # 严格 32 字节
MAGIC_BYTES = b'\xcd\xab'                  # 0xABCD 小端字节序

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

def handle_client(client_sock, client_addr):
    print(f"连接成功: {client_addr[0]}:{client_addr[1]}")
    
    stream_buffer = bytearray()
    frame_buffer = {}
    max_f_id = -1
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

            # 2. 提取 32 字节帧头
            header_data = bytes(stream_buffer[:HEADER_SIZE])
            
            # 3. ★★★ 终极防御：在解包之前/后，立刻执行帧头第一道物理层 CRC 校验 ★★★
            # 帧头一共 32 字节，最后 2 字节是 CRC，因此我们拿前 30 字节做校验
            local_crc = crc16_ccitt_xmodem(header_data[:30])
            
            # 解包出所有字段
            magic, d_len, f_id, p_cnt, p_id, f_type, ts, res1, res2, received_crc = struct.unpack(HEADER_FMT, header_data)

            # 如果算出来的 local_crc 跟 C 语言发过来的 received_crc 不相等
            if local_crc != received_crc:
                # print("⚠️ 警报：踩到 Payload 内部伪造的假魔数，已被 CRC16 成功拦截！")
                del stream_buffer[2:] # 判定为假魔数，弹开前两个字节继续往后搜索
                continue

            # --- 走到这里，说明魔数和 CRC 全部对上，100% 是真帧头，进入业务强校验 ---
            if d_len == 0 or d_len > 65535 or p_cnt == 0 or p_cnt > 1000 or p_id >= p_cnt:
                del stream_buffer[2:] 
                continue

            # 4. 检查后续载荷数据是否已经接收完整
            if len(stream_buffer) < HEADER_SIZE + d_len:
                break 

            # 5. 真包冷启动对齐检测
            if not has_found_first_packet:
                if p_id != 0:
                    del stream_buffer[: HEADER_SIZE + d_len]
                    continue
                else:
                    print(f"成功捕捉到全新帧的首包 (Frame: {f_id})")
                    has_found_first_packet = True

            # 6. 精准切下这一片有效载荷并从流中清除
            payload = bytes(stream_buffer[HEADER_SIZE : HEADER_SIZE + d_len])
            del stream_buffer[: HEADER_SIZE + d_len]

            # 7. 常规过滤历史过期帧
            if f_id < max_f_id: continue

            if f_id > max_f_id:
                max_f_id = f_id
                frame_buffer[f_id] = {'type': f_type, 'pkgs': [None] * p_cnt}

            if p_id < p_cnt and f_id in frame_buffer:
                frame_buffer[f_id]['pkgs'][p_id] = payload

            # 8. 组包与图像渲染
            current_frame = frame_buffer[max_f_id]
            if all(p is not None for p in current_frame['pkgs']):
                full_jpg = b"".join(current_frame['pkgs'])
                img = cv2.imdecode(np.frombuffer(full_jpg, np.uint8), cv2.IMREAD_COLOR)
                if img is not None:
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

        if cv2.waitKey(1) & 0xFF == ord("q"): break

    client_sock.close()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    handle_client()