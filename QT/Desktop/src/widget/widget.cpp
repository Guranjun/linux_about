#include "widget.h"
#include "ui_widget.h"
#include <QPushButton>
Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);
    QPushButton * quitBtn = new QPushButton("关闭窗口",this);
    connect(quitBtn,&QPushButton::clicked,this,&QWidget::close);

}
void Widget::on_btnOpen_Clicked()
{
    
}
void Widget::on_btnClose_Clicked()
{

}
void Widget::on_UdpDataReceived(const QByteArray &data)
{

}
void Widget::on_UdpErrorOccurred(const QString &msg)
{
    
}
Widget::~Widget()
{
    if(m_UdpReceiveThread && m_UdpReceiveThread->isRunning()){
        m_UdpReceiveThread->quit();
        m_UdpReceiveThread->wait();
    }
    delete ui;
}
