#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <signal.h>

static int running = 1;
void stop_handler(int sig) { running = 0; }
typedef struct{
	uint16_t magic;		//帧头标志
	uint32_t frame_id;	//帧ID
	uint16_t pkg_cnt;	//分包总数
	uint16_t pkg_id;	//分包ID
	uint16_t data_len;	//数据长度
	uint32_t timestamp;	//时间戳
} __attribute__((packed)) Frame_Header ;
typedef struct{
	int fd;				//设备文件描述符
	int buffer_count;		//缓冲区数量
	unsigned char *mmpaddr[4];	//映射后的首地址
	unsigned int addr_length[4];	//映射后空间的大小
	int width;			//视频宽度
	int height;			//视频高度
} V4L2_Device;
typedef struct{
	int Sock;			//UDP套接字
	struct sockaddr_in dest_addr;	//目的地址
	uint32_t current_frame_id; // 帧ID计数器
} Udp_Config;
void send_packet_optimized(int sock, Frame_Header *header, uint8_t *image_data, struct sockaddr_in *dest_addr) {
    struct iovec iov[2];
    struct msghdr msg;

    // 第一块：包头
    iov[0].iov_base = header;
    iov[0].iov_len = sizeof(Frame_Header); // 确保类型名正确

    // 第二块：图像数据分片
    iov[1].iov_base = image_data;
    iov[1].iov_len = header->data_len;

    // 填充 msghdr 结构
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = dest_addr;
    msg.msg_namelen = sizeof(struct sockaddr_in);
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;

    if (sendmsg(sock, &msg, 0) < 0) {
        perror("sendmsg error");
    }
}
void V4l2_Init(const char *device_path, V4L2_Device *device)
{
	device->fd = open(device_path, O_RDWR);
	if(device->fd < 0){
		perror("打开设备失败");
		exit(-1);
	}
	struct v4l2_format vfmt;
	memset(&vfmt, 0, sizeof(vfmt));
	vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vfmt.fmt.pix.width = 640;
	vfmt.fmt.pix.height = 480;
	vfmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	if( ioctl(device->fd, VIDIOC_S_FMT, &vfmt) < 0){
		perror("Set Format error");
		close(device->fd);
		exit(-1);
	}
	device->width = vfmt.fmt.pix.width;
	device->height = vfmt.fmt.pix.height;
	struct v4l2_requestbuffers reqbuffer;
	memset(&reqbuffer, 0, sizeof(reqbuffer));
	reqbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqbuffer.count = 4;
	reqbuffer.memory = V4L2_MEMORY_MMAP;
	if(ioctl(device->fd, VIDIOC_REQBUFS, &reqbuffer) < 0){
		perror("Request Buffers error");
		close(device->fd);
		exit(-1);
	}
	device->buffer_count = reqbuffer.count;
	struct v4l2_buffer mapbuffer;
	memset(&mapbuffer, 0, sizeof(mapbuffer));
	mapbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	for(int i = 0; i < device->buffer_count; i++){
		mapbuffer.index = i;
		if(ioctl(device->fd, VIDIOC_QUERYBUF, &mapbuffer) < 0){
			perror("Query Buffer error");
			close(device->fd);
			exit(-1);
		}
		device->mmpaddr[i] = (unsigned char *)mmap(NULL, mapbuffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, device->fd, mapbuffer.m.offset);
		device->addr_length[i] = mapbuffer.length;
		if(ioctl(device->fd, VIDIOC_QBUF, &mapbuffer) < 0){
			perror("Queue Buffer error");
			close(device->fd);
			exit(-1);
		}
	}
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if(ioctl(device->fd, VIDIOC_STREAMON, &type) < 0){
		perror("Stream On error");
		close(device->fd);
		exit(-1);
	}
	printf("V4L2 Init Success: %dx%d\n", device->width, device->height);
    return 0;
}
void Udp_Init(Udp_Config *udp_config, const char *ip, uint16_t port)
{
	udp_config->Sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (udp_config->Sock < 0) {
		perror("socket");
		exit(-1);
	}
	memset(&udp_config->dest_addr, 0, sizeof(udp_config->dest_addr));
	udp_config->dest_addr.sin_family = AF_INET;
	udp_config->dest_addr.sin_port = htons(port);
	udp_config->dest_addr.sin_addr.s_addr = inet_addr(ip);
	//初始化帧id计数器
	udp_config->current_frame_id = 0;
	int snd_buf = 1024 * 1024; // 设置 1MB 发送缓存
    setsockopt(udp_config->Sock, SOL_SOCKET, SO_SNDBUF, &snd_buf, sizeof(snd_buf));
	printf("UDP Init Success: Target %s:%d\n", ip, port);
    return 0;

}
void Udp_Send_Frame(Udp_Config *udp, uint8_t *jpg_data, uint32_t jpg_len) 
{
    const int CHUNK_SIZE = 1400; // 避开以太网 MTU 限制
    uint16_t total_pkgs = (jpg_len + CHUNK_SIZE - 1) / CHUNK_SIZE;
    uint32_t ts = (uint32_t)time(NULL); 

    for (uint16_t i = 0; i < total_pkgs; i++) {
        uint16_t current_chunk = (jpg_len - i * CHUNK_SIZE > CHUNK_SIZE) ? 
                                 CHUNK_SIZE : (jpg_len - i * CHUNK_SIZE);

        Frame_Header hdr;
        hdr.magic = 0xABCD;
        hdr.frame_id = udp->current_frame_id;
        hdr.pkg_cnt = total_pkgs;
        hdr.pkg_id = i;
        hdr.data_len = current_chunk;
        hdr.timestamp = ts;

        send_packet_optimized(udp->Sock, &hdr, jpg_data + (i * CHUNK_SIZE), &udp->dest_addr);
    }
    udp->current_frame_id++; 
}
int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s <video_device> <target_ip>\n", argv[0]);
        return -1;
    }

    signal(SIGINT, stop_handler);

    V4L2_Device cam;
    Udp_Config udp;

    // 1. 初始化
    V4l2_Init(argv[1], &cam);
    Udp_Init(&udp, argv[2], 8080);

    printf("Streaming started... Press Ctrl+C to stop.\n");

    struct v4l2_buffer buf;
    while (running) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        // 2. 出队列：获取一帧数据
        if (ioctl(cam.fd, VIDIOC_DQBUF, &buf) < 0) {
            perror("DQBUF failed");
            continue;
        }

        // 3. 发送数据：buf.bytesused 是 V4L2 告知的 JPG 实际有效载荷长度
        Udp_Send_Frame(&udp, cam.mmpaddr[buf.index], buf.bytesused);

        // 4. 入队列：归还缓冲区
        if (ioctl(cam.fd, VIDIOC_QBUF, &buf) < 0) {
            perror("QBUF failed");
        }
        
        // 5. 帧率控制（可选）：降低 CPU 占用
        // usleep(1000 * 10); // 10ms 延迟，约 100fps 峰值
    }

    // 资源释放（略）
    printf("\nExiting and cleaning up...\n");
    close(cam.fd);
    close(udp.Sock);
    return 0;
}