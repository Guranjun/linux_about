import socket
import struct
import time
import cv2
import numpy as np
import threading  # 💡 引入多线程模块

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

# ================= 🎯 新增：打包并发送自定义命令的函数 =================
def send_command(client_sock, mod_id: int, cmd_id: int):
    """
    按照 C 语言要求的 32字节帧头 + JSON载荷 格式发送控制命令
    """
    try:
        # 1. 构造 JSON 字符串载荷
        json_str = f'{{"mod":{mod_id},"cmd":{cmd_id}}}'
        payload = json_str.encode('utf-8')
        d_len = len(payload)
        
        # 2. 预打包前 30 字节的帧头字段（留出最后的 CRC 空间）
        # 字段顺序对应你的：magic, d_len, f_id, p_cnt, p_id, f_type, ts, res1, res2
        # 我们把 magic 填 0xABCD，d_len 填 json 长度，其他业务字段填 0 即可
        header_temp = struct.pack("< H H I H H I Q I H", 
                                  0xABCD, d_len, 0, 1, 0, 0, int(time.time()*1000), 0, 0)
        
        # 3. 计算前 30 字节的 CRC16
        calc_crc = crc16_ccitt_xmodem(header_temp)
        
        # 4. 把 CRC 拼接到最后 2 字节，组合成完美的 32 字节真帧头
        full_header = header_temp + struct.pack("< H", calc_crc)
        
        # 5. 帧头 + 载荷 一并打包发送
        client_sock.sendall(full_header + payload)
        print(f" -> 【发送成功】已下发控制命令: {json_str} (Payload长: {d_len} 字节)")
    except Exception as e:
        print(f" ❌ 【发送失败】下发命令异常: {e}")

# ================= 🎯 新增：后台键盘输入监听线程 =================
def keyboard_input_thread(client_sock, stop_event):
    """
    在后台运行，等待用户在终端输入命令
    """
    print("\n⌨️  [命令模式已开启]：可在下方终端随时输入控制命令！")
    print("👉 格式示例: 1 10 (表示 mod=1, cmd=10)，输入 'q' 退出发送")
    
    while not stop_event.is_set():
        try:
            user_input = input().strip()
            if user_input.lower() == 'q':
                break
                
            # 解析输入的模块ID和命令ID
            parts = user_input.split()
            if len(parts) == 2:
                mod_id = int(parts[0])
                cmd_id = int(parts[1])
                # 调用发送函数
                send_command(client_sock, mod_id, cmd_id)
            else:
                print("⚠️ 输入格式错误！请输入两个数字并用空格隔开，例如: 1 10")
        except ValueError:
            print("⚠️ 输入错误！ID必须是整数。")
        except Exception:
            break


def handle_client(client_sock, client_addr):
    print(f"连接成功: {client_addr[0]}:{client_addr[1]}")
    
    # 🎯 【核心注入】：创建并启动发送端线程
    stop_send_event = threading.Event()
    send_thr = threading.Thread(target=keyboard_input_thread, args=(client_sock, stop_send_event), daemon=True)
    send_thr.start()

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
            
            # 3. 第一道物理层 CRC 校验
            local_crc = crc16_ccitt_xmodem(header_data[:30])
            
            # 解包所有字段
            magic, d_len, f_id, p_cnt, p_id, f_type, ts, res1, res2, received_crc = struct.unpack(HEADER_FMT, header_data)

            if local_crc != received_crc:
                del stream_buffer[2:] 
                continue

            # --- 走进业务强校验 ---
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

    # 退出清理
    stop_send_event.set() 
    client_sock.close()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_sock.bind((TCP_IP, TCP_PORT))
    server_sock.listen(5)
    print(f"📡 TCP 上位机服务器已启动，正在监听端口 {TCP_PORT}...")

    try:
        while True:
            client_sock, client_addr = server_sock.accept()
            handle_client(client_sock, client_addr)
    except KeyboardInterrupt:
        print("\n服务器安全退出。")
    finally:
        server_sock.close()