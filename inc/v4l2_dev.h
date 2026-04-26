#ifndef __V4L2_DEV_H
#define __V4L2_DEV_H



typedef struct V4L2_Device V4L2_Device;
int V4l2_Init(const char *device_path, V4L2_Device *device);
void *camera_capture_thread(void *arg);



#endif