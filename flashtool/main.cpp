#include <QApplication>
#include "ui/mainwindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("ACE Flash Tool");
    app.setApplicationVersion("1.0.3");
    app.setOrganizationName("anycubic-cfw");

    MainWindow w;
    w.show();

    return app.exec();
}
