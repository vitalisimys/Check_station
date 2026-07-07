#include "mainwindow.h"
#include "debug.h"
#include "styles.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QMutex>
#include <QMutexLocker>
#include <QPalette>
#include <QPushButton>
#include <QStringList>
#include <QStyleFactory>
#include <QTimer>
#include <functional>

bool debug = false;
QString fullLog;

namespace {
QMutex g_appLogMutex;
QStringList g_appLogPending;
std::function<void(const QString &)> g_appLogSink;

void simpleMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    // Технический вывод: только при ключе debug — в logTextEdit, не в stderr.
    const bool technical = (type == QtDebugMsg || type == QtInfoMsg);
    const bool openglTrace = msg.startsWith(QStringLiteral("[OpenGL]"));
    const bool waylandActivateNoise =
        msg.contains(QStringLiteral("Wayland"), Qt::CaseInsensitive)
        && msg.contains(QStringLiteral("requestActivate"), Qt::CaseInsensitive);
    if ((technical || openglTrace || waylandActivateNoise) && !debug) {
        return;
    }

    fullLog += msg + QLatin1Char('\n');

    std::function<void(const QString &)> sinkCopy;
    {
        QMutexLocker locker(&g_appLogMutex);
        if (!g_appLogSink) {
            g_appLogPending.append(msg);
            return;
        }
        sinkCopy = g_appLogSink;
    }

    QTimer::singleShot(0, qApp, [sinkCopy, msg]() {
        if (sinkCopy) {
            sinkCopy(msg);
        }
    });
}
} // namespace

void setApplicationLogTextSink(std::function<void(const QString &)> sink)
{
    QStringList pending;
    std::function<void(const QString &)> fn;
    {
        QMutexLocker locker(&g_appLogMutex);
        fn = std::move(sink);
        g_appLogSink = fn;
        pending = std::move(g_appLogPending);
        g_appLogPending.clear();
    }
    if (!fn) {
        return;
    }
    for (const QString &line : pending) {
        QTimer::singleShot(0, qApp, [fn, line]() {
            if (fn) {
                fn(line);
            }
        });
    }
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.compare(QStringLiteral("debug"), Qt::CaseInsensitive) == 0
            || arg.compare(QStringLiteral("-debug"), Qt::CaseInsensitive) == 0) {
            debug = true;
        }
    }

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
