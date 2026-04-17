#include <QApplication>
#include <QByteArray>

#include "common/AppLocale.h"
#include "gui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    AppLocale::apply(AppLocale::Language::English);
    MainWindow w;
    w.show();
    return a.exec();
}
