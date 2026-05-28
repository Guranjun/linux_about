#ifndef TCPCONTROLWORKER_H
#define TCPCONTROLWORKER_H
#include <QTcpSocket>
#include <QObject.h>
class TcpControlWorker : public QObject
{
    Q_OBJECT
public :
    explicit TcpControlWorker(QObject *parent);
public slots:
    void startWithSocketDescriptor(qintptr socketDescriptor);
    void sendControlData(const QByteArray &data);
    void stopServer();
signals:
    void clientDisconnect();
    void dataReceived(uint32_t type, const QByteArray &data);
private slots:
    void onReadyRead();
    void onDisconnect();
private:
    QTcpSocket *m_tcpSocket = nullptr;
    QByteArray m_tcpBuffer;
    typedef QMap<uint16_t, QByteArray> FramePackets;
    QMap<uint32_t, FramePackets> m_frameBuffer;
    uint32_t m_lastCompleteFrameId = 0;
    void cleanExpiredFrames(uint32_t currentFrameId);
    void dispatchBusiness(uint32_t type, const QByteArray &data);
};
#endif // TCPCONTROLWORKER_H
