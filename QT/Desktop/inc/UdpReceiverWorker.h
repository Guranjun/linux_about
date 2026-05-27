#ifndef UDPRECEIVERWORKER_H
#define UDPRECEIVERWORKER_H
#include <QObject>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QMap>
#include <QByteArray>
#include <QList>
#define MY_PORT 8080
class UdpReceiverWorker : public QObject
{
    Q_OBJECT
public:
    explicit UdpReceiverWorker(QObject *parent = nullptr);
public slots:
    //初始化套接字
    void initSocket();
    //关闭套接字
    void closeSocket();
private slots:

    void onReadyRead();
signals:
    void image_dataReceived(const QByteArray &data);
    void errorReceived(const QString &errorMsg);
private:
    QUdpSocket *my_udpsocket = nullptr;
    const quint16 my_port = MY_PORT;
    typedef QMap<uint16_t, QByteArray> FramePackets;
    QMap<uint32_t, FramePackets> m_frameBuffer;
    uint32_t m_lastCompleteFrameId = 0;
    void cleanExpiredFrames(uint32_t currentFrameId);
};
#endif // UDPRECEIVERWORKER_H
