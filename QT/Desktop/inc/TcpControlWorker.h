#ifndef TCPCONTROLWORKER_H
#define TCPCONTROLWORKER_H

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

}
#endif // TCPCONTROLWORKER_H
