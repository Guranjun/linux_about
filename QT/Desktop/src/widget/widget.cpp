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

Widget::~Widget()
{
    delete ui;
}
