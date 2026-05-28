#include "UdpReceiverWorker.h"
#include <QThread>
#include <QDebug>
#include <net_shared.h>

UdpReceiverWorker::UdpReceiverWorker(QObject *parent)
    : QObject(parent)
{
}
void UdpReceiverWorker::initSocket()
{
    m_udpsocket = new QUdpSocket(this);
    bool ok = m_udpsocket->bind(M_PORT, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    if(ok){
        qDebug() << "UDP init success the port is " << M_PORT;
    }
    else{
        qDebug() << "UDP init failed " << m_udpsocket->errorString();
    }
    connect(m_udpsocket, &QUdpSocket::readyRead,
            this, &UdpReceiverWorker::onReadyRead);
}
void UdpReceiverWorker::closeSocket()
{
    if(m_udpsocket == nullptr){
        return;
    }
    m_udpsocket->disconnect();
    m_udpsocket->close();
    m_udpsocket->deleteLater();
    m_udpsocket = nullptr;
}
void UdpReceiverWorker::onReadyRead()
{
    while(m_udpsocket->hasPendingDatagrams()){
        QNetworkDatagram datagram = m_udpsocket->receiveDatagram();
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
                dispatchBusiness(file_type, completeFrameData);

                m_frameBuffer.remove(frame_id);
                m_lastCompleteFrameId = frame_id;
            }
        }
        else{
            continue;
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

void UdpReceiverWorker::dispatchBusiness(uint32_t type, const QByteArray &data)
{
    switch(type){
    case 0:{
        break;
    }
    case 1:{
        emit image_dataReceived(data);
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
}