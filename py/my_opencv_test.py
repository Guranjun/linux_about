import cv2
import numpy as np
import os
# 1. 自动获取当前脚本所在的文件夹路径
base_path = os.path.dirname(__file__)

# 2. 拼接模型的绝对路径
prototxt_path = os.path.join(base_path, "deploy.prototxt.txt")
model_path = os.path.join(base_path, "res10_300x300_ssd_iter_140000.caffemodel")

# 3. 加载模型（使用拼接好的绝对路径）
net = cv2.dnn.readNetFromCaffe(prototxt_path, model_path)

url = "http://192.168.1.103:8080/?action=stream"
cap = cv2.VideoCapture(url)

while True:
    ret, frame = cap.read()
    if not ret: break

    h, w = frame.shape[:2]
    # 图像预处理
    blob = cv2.dnn.blobFromImage(cv2.resize(frame, (300, 300)), 1.0, (300, 300), (104.0, 177.0, 123.0))
    net.setInput(blob)
    detections = net.forward()

    for i in range(0, detections.shape[2]):
        confidence = detections[0, 0, i, 2]
        if confidence > 0.5:  # 置信度阈值
            box = detections[0, 0, i, 3:7] * np.array([w, h, w, h])
            (startX, startY, endX, endY) = box.astype("int")

            # 画框
            cv2.rectangle(frame, (startX, startY), (endX, endY), (0, 255, 0), 2)

            # 计算人脸中心点用于后续跟踪
            center_x = (startX + endX) // 2
            print(f"人脸中心: {center_x}")

    cv2.imshow("DNN Face Detection", frame)
    if cv2.waitKey(1) == 27: break

cap.release()
cv2.destroyAllWindows()