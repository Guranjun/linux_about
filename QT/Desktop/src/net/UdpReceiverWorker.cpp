#include "UdpReceiverWorker.h"
#include <QThread>
#include <QDebug>

#if defined(_MSC_VER)
#pragma pack(push, 1)
#endif
struct Frame_Header{
    uint16_t magic;		//帧头标志
    uint16_t data_len;	//数据长度
    uint32_t frame_id;	//帧ID
    uint16_t pkg_cnt;	//分包总数
    uint16_t pkg_id;	//分包ID
    uint32_t type;     //数据类型
    uint64_t timestamp;	//时间戳
    uint32_t reserved1; //4字节的保留位
    uint16_t reserved2; //2字节的保留位
    uint16_t crc;       //CRC检测码
}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((packed))
#endif
;
#if defined(_MSC_VER)
#pragma pack(pop)
#endif
UdpReceiverWorker::UdpReceiverWorker(QObject *parent)
    : QObject(parent)
{
}
void UdpReceiverWorker::initSocket()
{
    my_udpsocket = new QUdpSocket(this);
    bool ok = my_udpsocket->bind(MY_PORT, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    if(ok){
        qDebug() << "UDP init success the port is " << MY_PORT;
    }
    else{
        qDebug() << "UDP init failed " << my_udpsocket->errorString();
    }
    connect(my_udpsocket, &QUdpSocket::readyRead,
            this, &UdpReceiverWorker::onReadyRead);
}
void UdpReceiverWorker::closeSocket()
{
    if(my_udpsocket == nullptr){
        return;
    }
    my_udpsocket->disconnect();
    my_udpsocket->close();
    my_udpsocket->deleteLater();
    my_udpsocket = nullptr;
}
void UdpReceiverWorker::onReadyRead()
{
    while(my_udpsocket->hasPendingDatagrams()){
        QNetworkDatagram datagram = my_udpsocket->receiveDatagram();
        QByteArray raw_data = datagram.data();
        if(raw_data.isEmpty() || raw_data.size() < sizeof(struct Frame_Header)){
            continue;
        }
        //进行帧头解析、数据帧拼接
        const struct Frame_Header *hdr = reinterpret_cast<const struct Frame_Header*>(raw_data.constData());
        if(hdr->magic != 0xABCD){
            continue;
        }
        quint16 crc = qChecksum(QByteArrayView(reinterpret_cast<const char*>(hdr), sizeof(struct Frame_Header) - 2));
        if(crc == hdr->crc){
            uint16_t data_len = hdr->data_len;
            uint32_t frame_id = hdr->frame_id;
            uint16_t pkg_cnt = hdr->pkg_cnt;
            uint16_t pkg_id = hdr->pkg_id;
            uint32_t file_type = hdr->type;
            QByteArray data = raw_data.mid(sizeof(struct Frame_Header), data_len);
            if(m_lastCompleteFrameId > 0 && frame_id <m_lastCompleteFrameId){
                continue;
            }
            cleanExpiredFrames(frame_id);
            m_frameBuffer[frame_id][pkg_id] = data;
            if(m_frameBuffer[frame_id].size() == pkg_cnt){
                QByteArray completeFrameData;
                for(uint16_t i = 0; i < pkg_cnt; i++){
                    completeFrameData.append(m_frameBuffer[frame_id][i]);
                }
                switch(file_type){
                    case 0:{
                        break;
                    }
                    case 1:{
                        emit image_dataReceived(completeFrameData);
                        break;
                    }
                    case 2:{
                        break;
                    }
                    case 3:{
                        break;
                    }
                    default:{
                        break;
                    }
                }

                m_frameBuffer.remove(frame_id);
                m_lastCompleteFrameId = frame_id;
            }
        }

    }
}
void UdpReceiverWorker::cleanExpiredFrames(uint32_t currentFrameId)
{
    if(m_frameBuffer.size() > 3){
        QList<uint32_t>frameIds = m_frameBuffer.keys();
        for(uint32_t id : frameIds){
            if(id < currentFrameId - 3){
                m_frameBuffer.remove(id);
                qDebug() << "图像帧积攒过多延迟明显，丢弃缓冲区里的帧";
            }
        }
    }
}