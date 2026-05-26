#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QThread>
#include "UdpReceiverWorker.h"
QT_BEGIN_NAMESPACE
namespace Ui {
class Widget;
}
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget() override;
signals:
    void RequestOpenUdp();
    void RequestCloseUdp();
private slots:
    void on_btnOpen_Clicked();
    void on_btnClose_Clicked();

    void on_UdpDataReceived(const QByteArray &data);
    void on_UdpErrorOccurred(const QString &msg);

private:
    Ui::Widget *ui;
    QThread *m_UdpReceiveThread = nullptr;
    UdpReceiverWorker *m_udpWorker = nullptr;

    void initNetWork();
};
#endif // WIDGET_H
