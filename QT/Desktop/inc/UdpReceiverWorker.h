#ifndef UDPRECEIVERWORKER_H
#define UDPRECEIVERWORKER_H
#include <QObject>
#include <QUdpSocket>
#include <QNetworkDatagram>
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
    void dataReceived(const QByteArray &data);
    void errorReceived(const QString &errorMsg);
private:
    QUdpSocket *my_udpsocket = nullptr;
    const quint16 my_port = MY_PORT;
};
#endif // UDPRECEIVERWORKER_H
