#include "TcpControlWorker.h"
#include "net_shared.h"
#include <QtEndian>
#include <QDebug>
TcpControlWorker::TcpControlWorker(QObject *parent) : QObject(parent) {}

void TcpControlWorker::startWithSocketDescriptor(qintptr socketDesriptor)
{
    m_tcpSocket = new QTcpSocket(this);
    if(m_tcpSocket->setSocketDescriptor(socketDesriptor)){
        qDebug() << "tcp connect success!";
        connect(m_tcpSocket, &QTcpSocket::readyRead, this, &TcpControlWorker::onReadyRead);
        connect(m_tcpSocket, &QTcpSocket::disconnected, this, &TcpControlWorker::onDisconnect);
    }
    else{
        qDebug() << "tcp connect failed!";
    }
}

void TcpControlWorker::sendControlData(const QByteArray &data)
{

}

void TcpControlWorker::stopServer()
{
    if(m_tcpSocket){
        m_tcpSocket->disconnectFromHost();
    }
}

void TcpControlWorker::onReadyRead()
{
    m_tcpBuffer.append(m_tcpSocket->readAll());
    while(m_tcpBuffer.size() >= sizeof(struct Frame_Header)){
        const struct Frame_Header *hdr = reinterpret_cast<const struct Frame_Header*>(m_tcpBuffer.constData());
        if(qFromBigEndian(hdr->magic) != 0xABCD){
            m_tcpBuffer.remove(0,1);
            continue;
        }
        uint16_t data_len = hdr->data_len;
        uint32_t frame_id = hdr->frame_id;
        uint16_t pkg_cnt = hdr->pkg_cnt;
        uint16_t pkg_id = hdr->pkg_id;
        uint32_t file_type = hdr->type;
        uint32_t total_frame_len = sizeof(struct Frame_Header) + data_len;
        if(m_tcpBuffer.size() < total_frame_len){
            break;
        }
        quint16 crc = qChecksum(QByteArrayView(reinterpret_cast<const char*>(hdr), sizeof(struct Frame_Header) - 2));
        if(crc != qFromBigEndian(hdr->crc)){
            qDebug() << "CRC detect failed!";
            m_tcpBuffer.remove(0, sizeof(struct Frame_Header)); // 错位容错，弹掉包头继续滑行
            continue;
        }

        QByteArray frame_data = (m_tcpBuffer.left(total_frame_len)).mid(sizeof(struct Frame_Header), data_len);
        if(m_lastCompleteFrameId > 0 && frame_id <m_lastCompleteFrameId){
            m_tcpBuffer.remove(0, total_frame_len);
            continue;
        }
        if(pkg_cnt > 1){
            cleanExpiredFrames(frame_id);
            m_frameBuffer[frame_id][pkg_id] = frame_data;

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
            dispatchBusiness(file_type, frame_data);
            m_lastCompleteFrameId = frame_id;
        }

        m_tcpBuffer.remove(0, total_frame_len);
    }
}

void TcpControlWorker::onDisconnect()
{
    qDebug() << "tcp disconnected!";
    emit clientDisconnect();
    if(m_tcpSocket){
        m_tcpSocket->deleteLater();
        m_tcpSocket = nullptr;
    }
    m_tcpBuffer.clear();
}

void TcpControlWorker::cleanExpiredFrames(uint32_t currentFrameId)
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
void TcpControlWorker::dispatchBusiness(uint32_t type, const QByteArray &data)
{
    switch(type){
        case 0:{
            break;
        }
        case 1:{

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