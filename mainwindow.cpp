#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "debug.h"
#include <QProcess>
#include <QTime>
#include <QScrollBar>
#include <QPixmap>
#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_deviceController(new DeviceController(this))
{
    ui->setupUi(this);

    connect(m_deviceController, &DeviceController::connected,
            this, &MainWindow::onDeviceConnected);
    connect(m_deviceController, &DeviceController::disconnected,
            this, &MainWindow::onDeviceDisconnected);
    connect(m_deviceController, &DeviceController::logMessage,
            this, &MainWindow::onDeviceLogMessage);
    connect(m_deviceController, &DeviceController::errorOccurred,
            this, &MainWindow::onDeviceError);

    setStationDisconnectedUi();
    ui->frameStation->setVisible(false);
    onDeviceLogMessage("Приложение запущено. Откройте настройки и выберите станцию.");
}

MainWindow::~MainWindow()
{
    cleanupAddedSelfIp();
    delete ui;
}

QPair<bool, QString> MainWindow::executeCommand(const QString &command) const
{
    QProcess process;
    process.start("/bin/bash", QStringList() << "-c" << command);
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(2000);
        return {false, "Timeout выполнения команды: " + command};
    }
    const QString out = QString::fromUtf8(process.readAllStandardOutput());
    const QString err = QString::fromUtf8(process.readAllStandardError());
    const QString combined = out + err;
    const bool ok = (process.exitStatus() == QProcess::NormalExit) && (process.exitCode() == 0);
    return {ok, combined};
}

void MainWindow::cleanupAddedSelfIp()
{
    if (m_cleanupDone) {
        return;
    }
    m_cleanupDone = true;

    if (m_addedIps.isEmpty()) {
        return;
    }

    // Удаляем в обратном порядке добавления — так удобнее для логов/отладки.
    for (int i = m_addedIps.size() - 1; i >= 0; --i) {
        const AddedIpEntry &e = m_addedIps[i];
        const QString iface = e.iface.trimmed();
        const QString selfIp = e.ip.trimmed();
        const int cidr = e.cidr;

        if (iface.isEmpty() || selfIp.isEmpty() || cidr <= 0) {
            continue;
        }

        QString connectionUuid = e.connectionUuid.trimmed();
        if (connectionUuid.isEmpty()) {
            // Самый надёжный способ — спросить у nmcli активное соединение для DEVICE.
            const QString command = QString("nmcli -t -f UUID,DEVICE connection show --active | grep -F \":%1\" | cut -d':' -f1")
                                        .arg(iface);
            const QPair<bool, QString> result = executeCommand(command);
            connectionUuid = result.second.trimmed().split('\n', Qt::SkipEmptyParts).value(0).trimmed();
            if (!result.first || connectionUuid.isEmpty()) {
                onDeviceLogMessage(QString("Не удалось определить активное соединение для %1, очистка self-IP %2/%3 пропущена.")
                                       .arg(iface, selfIp).arg(cidr));
                continue;
            }
        }

        const QString ipWithMask = QString("%1/%2").arg(selfIp).arg(cidr);
        QString command = QString("nmcli connection modify uuid \"%1\" -ipv4.addresses %2").arg(connectionUuid, ipWithMask);
        QPair<bool, QString> result = executeCommand(command);

        // Если не получилось без sudo — пробуем с sudo (часто профили требуют прав).
        if (!result.first) {
            command = QString("sudo nmcli connection modify uuid \"%1\" -ipv4.addresses %2").arg(connectionUuid, ipWithMask);
            result = executeCommand(command);
        }

        if (!result.first) {
            onDeviceLogMessage(QString("Не удалось удалить self-IP %1 из UUID \"%2\": %3")
                                   .arg(ipWithMask, connectionUuid, result.second.trimmed()));
            continue;
        }

        // Применяем изменения (переподнимаем интерфейс).
        executeCommand(QString("sudo nmcli device disconnect %1").arg(iface));
        executeCommand(QString("sudo nmcli device connect %1").arg(iface));
        onDeviceLogMessage(QString("Удалён добавленный self-IP %1 (интерфейс %2)").arg(ipWithMask, iface));
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    cleanupAddedSelfIp();
    QMainWindow::closeEvent(event);
}

void MainWindow::on_actionSettings_triggered()
{
    SettingsDialog dialog(this);
    connect(&dialog, &SettingsDialog::stationConnectRequested,
            this, &MainWindow::onStationConnectRequested);
    dialog.exec();
}

void MainWindow::onStationConnectRequested(const QString &stationIp, const QString &selfIp, const QString &interfaceName) {
    if (m_deviceController->isConnected()) {
        m_deviceController->disconnectFromDevice();
    }

    ui->frameStation->setVisible(false);
    if (!selfIp.trimmed().isEmpty()) {
        m_deviceController->setSelfIp(selfIp);
        onDeviceLogMessage(QString("Выбран self IP контроллера: %1").arg(selfIp));
    }
    m_deviceController->setStationIp(stationIp);
    onDeviceLogMessage(QString("Запрос подключения к станции %1").arg(stationIp));

    // Запоминаем для очистки при выходе (может быть несколько станций/несколько добавлений).
    const QStringList parts = stationIp.trimmed().split('.');
    const int cidr = (parts.size() == 4 && parts[3] == "193") ? 26 : 25;
    AddedIpEntry entry;
    entry.ip = selfIp.trimmed();
    entry.cidr = cidr;
    entry.iface = interfaceName.trimmed();
    if (!entry.iface.isEmpty()) {
        const QString cmd = QString("nmcli -t -f UUID,DEVICE connection show --active | grep -F \":%1\" | cut -d':' -f1")
                                .arg(entry.iface);
        const QPair<bool, QString> res = executeCommand(cmd);
        entry.connectionUuid = res.second.trimmed().split('\n', Qt::SkipEmptyParts).value(0).trimmed();
    }

    // Не дублируем одинаковые записи.
    const bool exists = std::any_of(m_addedIps.cbegin(), m_addedIps.cend(), [&entry](const AddedIpEntry &e) {
        return e.iface == entry.iface && e.ip == entry.ip && e.cidr == entry.cidr;
    });
    if (!exists && !entry.ip.isEmpty() && entry.cidr > 0 && !entry.iface.isEmpty()) {
        m_addedIps.push_back(entry);
    }

    m_deviceController->connectToDevice();
}

void MainWindow::onDeviceConnected(const QString &ip) {
    setStationConnectedUi();
    ui->frameStation->setVisible(true);
    onDeviceLogMessage(QString("Успешное подключение к р/станции: %1").arg(ip));
}

void MainWindow::onDeviceDisconnected() {
    setStationDisconnectedUi();
    ui->frameStation->setVisible(true);
    onDeviceLogMessage("Соединение со станцией разорвано.");
}

void MainWindow::onDeviceLogMessage(const QString &msg) {
    const QString timeStr = QTime::currentTime().toString("HH:mm:ss");
    ui->logTextEdit->append(QString("[%1] %2").arg(timeStr, msg));

    QScrollBar *sb = ui->logTextEdit->verticalScrollBar();
    if (sb) {
        sb->setValue(sb->maximum());
    }
}

void MainWindow::onDeviceError(const QString &err) {
    onDeviceLogMessage(QString("ОШИБКА: %1").arg(err));
}

void MainWindow::setStationConnectedUi() {
    ui->frameStation->setStyleSheet(
        "#frameStation {"
        " color: #10b981;"
        " border-radius: 8px;"
        " border: 2px solid #10b981;"
        " font-family: \"Consolas\";"
        "}"
    );
    ui->labelPixStation->setPixmap(QPixmap(":/led_green.png"));
    ui->labelStateStation->setText("Подключена");
}

void MainWindow::setStationDisconnectedUi() {
    ui->frameStation->setStyleSheet(
        "#frameStation {"
        " color: #10b981;"
        " border-radius: 8px;"
        " border: 2px solid #ef4444;"
        " font-family: \"Consolas\";"
        "}"
    );
    ui->labelPixStation->setPixmap(QPixmap(":/led_red.png"));
    ui->labelStateStation->setText("Отключена");
}
