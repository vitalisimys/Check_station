#include "mainwindow.h"
#include "debug.h"

#include <QApplication>
#include <cstdio>

bool debug = false;
QString fullLog;

void simpleMessageHandler(QtMsgType, const QMessageLogContext &, const QString &msg)
{
    if (debug) {
        fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
        fflush(stderr);

        fullLog += msg + "\n";
    }
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    qInstallMessageHandler(simpleMessageHandler);

    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.compare("-debug", Qt::CaseInsensitive) == 0) {
            debug = true;
        }
    }

    MainWindow w;
    w.show();
    return a.exec();
}
