#include "mainwindow.h"
#include "debug.h"
#include "styles.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QPushButton>
#include <QStyleFactory>
#include <cstdio>
#include "debug.h"

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
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    qInstallMessageHandler(simpleMessageHandler);

    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.compare("-debug", Qt::CaseInsensitive) == 0) {
            debug = true;
        }
    }

    const QString socketName = "check_station";
    const QString socketPath = QDir::tempPath() + "/" + socketName;

    // Проверяем, запущено ли уже приложение
    QLocalSocket socket;
    socket.connectToServer(socketPath);

    if (socket.waitForConnected(500)) {
        // Если успешно подключились, значит программа работает
        QMessageBox msgBox;
        msgBox.setWindowTitle("Ошибка");
        msgBox.setText("Программа уже запущена.");
        msgBox.setIcon(QMessageBox::Warning);

        QPushButton *btnOk = msgBox.addButton("ОК", QMessageBox::AcceptRole);
        btnOk->setStyleSheet(stylesheetButtonMessBox);

        msgBox.setStyleSheet(stylesheetMessBox);
        msgBox.exec();

        return 1;
    } else {
        DEBUG << "[main] Нет подключения. Ошибка:" << socket.errorString();
    }

    // Удаляем остаточный сокет, если он есть
    if (QFile::exists(socketPath)) {
        DEBUG << "[main] Удаляем остаточный сокет";
        QFile::remove(socketPath);
    }

    // Создаём сервер
    QLocalServer server;
    if (!server.listen(socketPath)) {
        DEBUG << "[main] Ошибка listen():" << server.errorString();

        QMessageBox msgBox;
        msgBox.setWindowTitle("Ошибка");
        msgBox.setText(QString("Не удалось создать сервер:\n %1").arg(server.errorString()));
        msgBox.setIcon(QMessageBox::Warning);

        QPushButton *btnOk = msgBox.addButton("ОК", QMessageBox::AcceptRole);
        btnOk->setStyleSheet(stylesheetButtonMessBox);

        msgBox.setStyleSheet(stylesheetMessBox);
        msgBox.exec();

        return 1;
    }

    DEBUG << "[main] Сервер запущен на:" << server.fullServerName();

    // Очистка при выходе
    QObject::connect(&a, &QApplication::aboutToQuit, [&]() {
        DEBUG << "[main] aboutToQuit: закрываем сервер и удаляем сокет";
        server.close();
        QFile::remove(socketPath);
    });

    MainWindow w;
    w.show();
    return a.exec();
}
