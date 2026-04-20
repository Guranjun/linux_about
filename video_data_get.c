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
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/uio.h>

typedef struct{
	uint16_t magic;		//帧头标志
	uint32_t frame_id;	//帧ID
	uint16_t pkg_cnt;	//分包总数
	uint16_t pkg_id;	//分包ID
	uint16_t data_len;	//数据长度
	uint32_t timestamp;	//时间戳

} Frame_Header ;

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
int main(int argc, char**argv)
{

	if (ClientSocket < 0)
    {
        perror("socket");
        return -1;
    }
	if(argc != 2)
	{
		printf("%s </dev/video0,1...>\n", argv[0]);
		return -1;
	}
	//1.打开摄像头设备
	int fd = open(argv[1], O_RDWR);
	if(fd < 0)
	{
		perror("打开设备失败");
		return -1;
	}
	//2.设置摄像头采集格式
	struct v4l2_format vfmt;
	vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;	//选择视频抓取
	vfmt.fmt.pix.width = 640;//设置宽，不能随意设置
	vfmt.fmt.pix.height = 480;//设置高
	vfmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;//设置视频采集像素格式
 
	int ret = ioctl(fd, VIDIOC_S_FMT, &vfmt);// VIDIOC_S_FMT:设置捕获格式
	if(ret < 0)
	{
		perror("设置采集格式错误");
	}
	/*udp about*/
	int ClientSocket ;
    struct sockaddr_in ClientAddr ;
	ssize_t len;
	ClientSocket = socket( AF_INET , SOCK_DGRAM, 0);
	if (ClientSocket < 0)
	{
		perror("socket");
		return -1;
	}
	memset(&ClientAddr, 0, sizeof(ClientAddr));
	ClientAddr.sin_family= AF_INET;
	ClientAddr.sin_port= htons(8080);
	ClientAddr.sin_addr.s_addr= inet_addr("192.168.1.110");
	ret = connect(ClientSocket , (struct sockaddr *)&ClientAddr , sizeof(ClientAddr));
    if(ret == -1)
    {
        perror("Client connect : ");
        return -1;
    }


	memset(&vfmt, 0, sizeof(vfmt));
	vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(fd, VIDIOC_G_FMT, &vfmt);	
	if(ret < 0)
	{
		perror("读取采集格式失败");
	}
	printf("width = %d\n", vfmt.fmt.pix.width);
	printf("width = %d\n", vfmt.fmt.pix.height);
	unsigned char *p = (unsigned char*)&vfmt.fmt.pix.pixelformat;
	printf("pixelformat = %c%c%c%c\n", p[0],p[1],p[2],p[3]);	
 
	//4.申请缓冲队列
	struct v4l2_requestbuffers reqbuffer;
	reqbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqbuffer.count = 4;	//申请4个缓冲区
	reqbuffer.memory = V4L2_MEMORY_MMAP;	//采用内存映射的方式
 
	ret = ioctl(fd, VIDIOC_REQBUFS, &reqbuffer);
	if(ret < 0)
	{
		perror("申请缓冲队列失败");
	}
	
	//映射，映射之前需要查询缓存信息->每个缓冲区逐个映射->将缓冲区放入队列
	struct v4l2_buffer mapbuffer;
	unsigned char *mmpaddr[4];//用于存储映射后的首地址
	unsigned int addr_length[4];//存储映射后空间的大小
	mapbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;//初始化type
	for(int i = 0; i < 4; i++)
	{
		mapbuffer.index = i;
		ret = ioctl(fd, VIDIOC_QUERYBUF, &mapbuffer);	//查询缓存信息
		if(ret < 0)
			perror("查询缓存队列失败");
		mmpaddr[i] = (unsigned char *)mmap(NULL, mapbuffer.length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mapbuffer.m.offset);//mapbuffer.m.offset映射文件的偏移量
		addr_length[i] = mapbuffer.length;
		//放入队列
		ret = ioctl(fd, VIDIOC_QBUF, &mapbuffer);
		if(ret < 0)
			perror("放入队列失败");
	}
 
	//打开设备
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(fd, VIDIOC_STREAMON, &type);
	if(ret < 0)
		perror("打开设备失败");
	//从队列中提取一帧数据
	struct v4l2_buffer readbuffer;
	readbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(fd, VIDIOC_DQBUF, &readbuffer);//从缓冲队列获取一帧数据（出队列）
	//出队列后得到缓存的索引index,得到对应缓存映射的地址mmpaddr[readbuffer.index]
	if(ret < 0)
		perror("获取数据失败");
	len = send(ClientSocket , mmpaddr[readbuffer.index] , readbuffer.bytesused , 0 );
	if(len <= 0 )
	{
		close(ClientSocket);
		return -1;
	}
	FILE *file = fopen("1.jpg", "w+");//建立文件用于保存一帧数据
	fwrite(mmpaddr[readbuffer.index], readbuffer.bytesused, 1, file);
	fclose(file);
	//读取数据后将缓冲区放入队列
	ret = ioctl(fd, VIDIOC_QBUF, &readbuffer);
	if(ret < 0)
		perror("放入队列失败");
	//关闭设备
	ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
	if(ret < 0)
		perror("关闭设备失败");
	//取消映射
	for(int i = 0; i < 4; i++)
		munmap(mmpaddr[i], addr_length[i]);
	close(fd);
	close(ClientSocket);
	return 0;
	
}