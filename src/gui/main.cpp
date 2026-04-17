#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>

#include "common/AppLocale.h"
#include "gui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setApplicationName("rtmp-camera-pusher-gui");
    AppLocale::apply(AppLocale::Language::English);
    MainWindow w;
    w.show();
    return a.exec();
}
