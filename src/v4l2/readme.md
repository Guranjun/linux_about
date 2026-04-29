# 视频采集模块
## 1.模块组成函数说明
### 1.Change_Image_Taken_Flag
    标志着模块私有数据现在被外部模块使用中
### 2.Reset_Image_Taken_Flag
    标志着模块私有数据被外部模块归还
### 3.v4l2_data_buffer_init
    内部私有数据初始化
### 4.v4l2_data_buffer_destroy
    内部私有数据free，在线程被销毁前调用
### 5.V4L2_Init
    V4L2基础配置
### 6.camera_capture_thread
    采集线程
