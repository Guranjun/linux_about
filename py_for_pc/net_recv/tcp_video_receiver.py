import socket
import struct
import time
import cv2
import numpy as np
import threading
import os
from datetime import datetime

# ================= 配置参数 =================
TCP_IP = "0.0.0.0"
TCP_PORT = 8080
SAVE_DIR = "received_files"          # 保存文件的目录

# ================= 文件类型枚举（与 C 端 common.h 一致） =================
FILE_TYPE_NORMAL  = 0
FILE_TYPE_IMAGE   = 1
FILE_TYPE_DB      = 2
FILE_TYPE_VIDEO   = 3
FILE_TYPE_COMMAND = 4

# < H H I H H I Q I H H ➔ 32 字节
HEADER_FMT = "< H H I H H I Q I H H"
HEADER_SIZE = struct.calcsize(HEADER_FMT)  # 严格 32 字节
MAGIC_BYTES = b'\xcd\xab'                  # 0xABCD 小端字节序

file_type_names = {
    FILE_TYPE_NORMAL:  "NORMAL",
    FILE_TYPE_IMAGE:   "IMAGE",
    FILE_TYPE_DB:      "DB",
    FILE_TYPE_VIDEO:   "VIDEO",
    FILE_TYPE_COMMAND: "COMMAND",
}

file_type_extensions = {
    FILE_TYPE_IMAGE:   ".jpg",
    FILE_TYPE_DB:      ".db",
    FILE_TYPE_VIDEO:   ".avi",
}


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


def save_file(data: bytes, f_type: int, timestamp_us: int):
    """根据文件类型保存数据到磁盘"""
    os.makedirs(SAVE_DIR, exist_ok=True)

    ext = file_type_extensions.get(f_type, ".bin")
    ts_str = datetime.fromtimestamp(timestamp_us / 1_000_000).strftime("%Y%m%d_%H%M%S")
    filename = f"{ts_str}_{f_type}{ext}"
    filepath = os.path.join(SAVE_DIR, filename)

    with open(filepath, "wb") as f:
        f.write(data)

    type_name = file_type_names.get(f_type, f"UNKNOWN({f_type})")
    print(f"💾 已保存文件: {filepath}  ({len(data)} 字节, 类型: {type_name})")
    return filepath


# ================= 发送自定义命令的函数 =================
def send_command(client_sock, mod_id: int, cmd_id: int, cmd_type: int = 0):
    """
    按照 typical.json 格式发送控制命令:
    {"ver":0, "mod":mod_id, "cmd":cmd_id, "type":cmd_type, "param":{}}
    """
    try:
        import json
        cmd_obj = {
            "ver": 0,
            "mod": mod_id,
            "cmd": cmd_id,
            "type": cmd_type,
            "param": {}
        }
        payload = json.dumps(cmd_obj).encode('utf-8')
        d_len = len(payload)

        header_temp = struct.pack("< H H I H H I Q I H",
                                  0xABCD, d_len, 0, 1, 0, 0, int(time.time()*1000), 0, 0)

        calc_crc = crc16_ccitt_xmodem(header_temp)
        full_header = header_temp + struct.pack("< H", calc_crc)

        client_sock.sendall(full_header + payload)
        print(f" -> 【发送成功】已下发控制命令: {cmd_obj} (Payload长: {d_len} 字节)")
    except Exception as e:
        print(f" ❌ 【发送失败】下发命令异常: {e}")


# ================= 后台键盘输入监听线程 =================
def keyboard_input_thread(client_sock, stop_event):
    print("\n⌨️  [命令模式已开启]：可在下方终端随时输入控制命令！")
    print("👉 格式: mod cmd [type]")
    print("   示例1: 1 768    (mod=1, cmd=768, type=0)")
    print("   示例2: 1 769 1  (mod=1, cmd=769, type=1)")
    print("   输入 'q' 退出发送")

    while not stop_event.is_set():
        try:
            user_input = input().strip()
            if user_input.lower() == 'q':
                break

            parts = user_input.split()
            mod_id = int(parts[0])
            cmd_id = int(parts[1])
            cmd_type = int(parts[2]) if len(parts) >= 3 else 0
            send_command(client_sock, mod_id, cmd_id, cmd_type)
        except (ValueError, IndexError):
            print("⚠️ 格式错误！请按格式输入: mod cmd [type]，例如: 1 768")
        except Exception:
            break


def handle_client(client_sock, client_addr):
    print(f"连接成功: {client_addr[0]}:{client_addr[1]}")

    # 创建发送端线程
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

            # 3. CRC 校验
            local_crc = crc16_ccitt_xmodem(header_data[:30])

            # 解包所有字段
            magic, d_len, f_id, p_cnt, p_id, f_type, ts, res1, res2, received_crc = struct.unpack(HEADER_FMT, header_data)

            if local_crc != received_crc:
                del stream_buffer[2:]
                continue

            # --- 业务强校验 ---
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
                    type_name = file_type_names.get(f_type, f"UNKNOWN({f_type})")
                    print(f"成功捕捉到全新帧的首包 (Frame: {f_id}, 类型: {type_name})")
                    has_found_first_packet = True

            # 6. 切下有效载荷并从流中清除
            payload = bytes(stream_buffer[HEADER_SIZE : HEADER_SIZE + d_len])
            del stream_buffer[: HEADER_SIZE + d_len]

            # 7. 过滤历史过期帧
            if f_id < max_f_id:
                continue

            if f_id > max_f_id:
                max_f_id = f_id
                frame_buffer[f_id] = {'type': f_type, 'pkgs': [None] * p_cnt}

            if p_id < p_cnt and f_id in frame_buffer:
                frame_buffer[f_id]['pkgs'][p_id] = payload

            # 8. 组包完成 → 根据文件类型处理
            current_frame = frame_buffer.get(max_f_id)
            if current_frame is None:
                continue

            if all(p is not None for p in current_frame['pkgs']):
                full_data = b"".join(current_frame['pkgs'])
                file_type = current_frame['type']

                if file_type == FILE_TYPE_IMAGE:
                    # ---- 图像类型：解码显示 + 保存为 .jpg ----
                    img = cv2.imdecode(np.frombuffer(full_data, np.uint8), cv2.IMREAD_COLOR)
                    if img is not None:
                        if prev_hw_timestamp > 0:
                            time_diff = (ts - prev_hw_timestamp) / 1000000.0
                            if 0 < time_diff < 10.0:
                                real_fps = 1.0 / time_diff
                                fps = (fps * 0.9) + (real_fps * 0.1)

                        prev_hw_timestamp = ts
                        cv2.putText(img, f"FPS: {int(fps)}", (20, 40),
                                    cv2.FONT_HERSHEY_SIMPLEX, 1.0, (0, 255, 0), 2)
                        cv2.imshow("TCP Receiver", img)

                        # 保存为 .jpg
                        save_file(full_data, FILE_TYPE_IMAGE, ts)
                    else:
                        print(f"⚠️ 图像解码失败 (Frame: {f_id})")

                elif file_type == FILE_TYPE_DB:
                    # ---- 数据库类型：保存为 .db ----
                    print(f"📦 收到数据库文件 (Frame: {f_id}, {len(full_data)} 字节)")
                    save_file(full_data, FILE_TYPE_DB, ts)

                elif file_type == FILE_TYPE_VIDEO:
                    # ---- 视频类型：保存为 .avi ----
                    print(f"🎬 收到视频文件 (Frame: {f_id}, {len(full_data)} 字节)")
                    save_file(full_data, FILE_TYPE_VIDEO, ts)

                else:
                    # ---- 未知类型：以 .bin 保存 ----
                    print(f"❓ 收到未知类型数据 (type={file_type}, Frame: {f_id}, {len(full_data)} 字节)")
                    save_file(full_data, file_type, ts)

                del frame_buffer[max_f_id]

        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

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
    print(f"📁 接收到的文件将保存到 '{SAVE_DIR}' 目录")

    try:
        while True:
            client_sock, client_addr = server_sock.accept()
            handle_client(client_sock, client_addr)
    except KeyboardInterrupt:
        print("\n服务器安全退出。")
    finally:
        server_sock.close()