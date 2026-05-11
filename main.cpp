#include "mainwindow.h"
#include "debug.h"
#include "styles.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QStyleFactory>
#include <QToolTip>
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

    // Единая slate-палитра приложения: используется Fusion-стилем для системных
    // диалогов и виджетов, которые не покрываются QSS (например, заголовки
    // системного MessageBox, выпадающие меню, palette-зависимые элементы).
    {
        QPalette p;
        p.setColor(QPalette::Window,          AppPalette::surface);
        p.setColor(QPalette::WindowText,      AppPalette::textPrimary);
        p.setColor(QPalette::Base,            AppPalette::canvas);
        p.setColor(QPalette::AlternateBase,   AppPalette::surfaceRaised);
        p.setColor(QPalette::ToolTipBase,     AppPalette::surfaceRaised);
        p.setColor(QPalette::ToolTipText,     AppPalette::textPrimary);
        p.setColor(QPalette::Text,            AppPalette::textPrimary);
        p.setColor(QPalette::Button,          AppPalette::surfaceRaised);
        p.setColor(QPalette::ButtonText,      AppPalette::textPrimary);
        p.setColor(QPalette::BrightText,      AppPalette::danger);
        p.setColor(QPalette::Link,            AppPalette::info);
        p.setColor(QPalette::Highlight,       AppPalette::accent);
        p.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#f8fafc")));
        p.setColor(QPalette::Disabled, QPalette::Text,       AppPalette::textDisabled);
        p.setColor(QPalette::Disabled, QPalette::ButtonText, AppPalette::textDisabled);
        p.setColor(QPalette::Disabled, QPalette::WindowText, AppPalette::textDisabled);
        QApplication::setPalette(p);
    }

    // Глобальный QSS приложения — единый «вкус» (tooltip, scrollbar, menu, dialog,
    // progress, lcd, group-box и т.д.). Локальные QSS в .ui сохраняют приоритет.
    a.setStyleSheet(buildAppStyleSheet());

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
