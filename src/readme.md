# 1.main.c

# 2.消息模块
## 1.消息模块函数组成
### 1.占位函数声明
    占位函数，防止未声明报错
### 2.msg_make
    提供给外部模块生成通用消息格式的消息
### 3.msg_init
    消息模块私有数据初始化
### 4.msg_send
    提供给外部模块发送消息
### 5.msg_receive
    模块内部调用，接收消息
### 6.msg_release
    归还消息内含的外部模块的数据
### 7.msg_cleanup
    回收数据，线程结束时使用
### 8.msg_deliver_thread
    消息线程
### 9.msg_thread_weakup
    当进程结束时调用回收线程
