#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "debug.h"
#include "styles.h"
#include <QProcess>
#include <QTime>
#include <QScrollBar>
#include <QPixmap>
#include <algorithm>
#include <QtConcurrent>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSet>
#include <QRandomGenerator>
#include <QMap>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_deviceController(new DeviceController(this))
    , m_analyzerController(new AnalyzerController(this))
    , m_finder(new FindManager(this))
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
    setAnalyzerDisconnectedUi();
    ui->frameStation->setVisible(true);
    ui->frameR3->setVisible(true);
    onDeviceLogMessage("Приложение запущено. Поиск ethernet-интерфейсов...");

    connect(m_analyzerController, &AnalyzerController::analyzerConnected,
            this, &MainWindow::onAnalyzerConnected);
    connect(m_analyzerController, &AnalyzerController::analyzerDisconnected,
            this, &MainWindow::onAnalyzerDisconnected);
    connect(m_analyzerController, &AnalyzerController::logMessage,
            this, &MainWindow::onAnalyzerLogMessage);

    // Подключение к анализатору должно начинаться автоматически при старте приложения.
    m_analyzerController->connectToDefaultPort();

    // Поиск интерфейсов/станций должен запускаться при старте программы.
    startAutoDiscovery();
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
    // В настройки передаём уже просканированные интерфейсы (и, опционально,
    // найденные IP для единственного интерфейса), чтобы не пересканировать заново.
    const QString preselectedIface = (m_cachedIfaces.size() == 1) ? m_cachedIfaces.value(0) : QString();
    const QVector<QString> cachedIps =
        (!preselectedIface.isEmpty() && m_cachedFoundIpsByIface.contains(preselectedIface))
            ? m_cachedFoundIpsByIface.value(preselectedIface)
            : QVector<QString>();

    SettingsDialog dialog(this, m_cachedIfaces, preselectedIface, cachedIps);
    connect(&dialog, &SettingsDialog::stationConnectRequested,
            this, &MainWindow::onStationConnectRequested);
    dialog.exec();
}

void MainWindow::startAutoDiscovery()
{
    QtConcurrent::run([this]() {
        const QStringList ifaces = collectEligibleInterfaces();
        QMetaObject::invokeMethod(this, [this, ifaces]() {
            handleDiscoveryFinished(ifaces);
        }, Qt::QueuedConnection);
    });
}

QStringList MainWindow::collectEligibleInterfaces() const
{
    QStringList result;

    // Аналогично SettingsDialog: исключаем отключенные устройства.
    QSet<QString> nmcliAllowedDevices;
    {
        const QPair<bool, QString> nmcliResult =
            executeCommand("nmcli -t -f DEVICE,STATE device status 2>/dev/null");
        if (nmcliResult.first) {
            const QStringList lines = nmcliResult.second.split('\n', Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                const QString trimmed = line.trimmed();
                const QStringList parts = trimmed.split(':');
                if (parts.size() < 2) {
                    continue;
                }
                const QString deviceName = parts.value(0).trimmed();
                if (deviceName.isEmpty()) {
                    continue;
                }
                const QString state = parts.mid(1).join(':').trimmed();
                const QString s = state.toLower();
                const bool blocked =
                    s.contains("disconnected") ||
                    s.contains("unavailable") ||
                    s.contains("unmanaged");
                if (!blocked) {
                    nmcliAllowedDevices.insert(deviceName);
                }
            }
        }
    }

    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        if (!(interface.flags() & QNetworkInterface::IsUp) ||
            !(interface.flags() & QNetworkInterface::IsRunning) ||
            (interface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        if (interface.hardwareAddress().isEmpty()) {
            continue;
        }

        const QString name = interface.name();
        if (name.startsWith("eth") || name.startsWith("en") || name.startsWith("wlan")) {
            if (!nmcliAllowedDevices.isEmpty() && !nmcliAllowedDevices.contains(name)) {
                continue;
            }
            result.push_back(name);
        }
    }
    return result;
}

void MainWindow::handleDiscoveryFinished(const QStringList &ifaces)
{
    m_cachedIfaces = ifaces;

    if (ifaces.isEmpty()) {
        onDeviceLogMessage("Ethernet-интерфейсы не найдены. Откройте настройки и выберите интерфейс вручную.");
        return;
    }

    onDeviceLogMessage(QString("Найдено интерфейсов: %1").arg(ifaces.size()));

    // Если интерфейс один — сразу ищем станции на нём.
    if (ifaces.size() == 1) {
        const QString iface = ifaces.value(0);
        onDeviceLogMessage(QString("Поиск радиостанций на интерфейсе %1...").arg(iface));

        QtConcurrent::run([this, iface]() {
            const QVector<QString> foundIps = m_finder ? m_finder->searchStations(iface) : QVector<QString>();
            QMetaObject::invokeMethod(this, [this, iface, foundIps]() {
                handleStationsFound(iface, foundIps);
            }, Qt::QueuedConnection);
        });
        return;
    }

    // Интерфейсов несколько — дальнейший выбор/поиск делаем через настройки.
    onDeviceLogMessage("Интерфейсов несколько. Откройте настройки и выберите интерфейс для поиска станции.");
}

void MainWindow::handleStationsFound(const QString &iface, const QVector<QString> &foundIps)
{
    m_cachedFoundIpsByIface.insert(iface, foundIps);

    // Повторяем логику выбора *.193 по подсетям (как в SettingsDialog).
    QMap<int, QString> chosenBySubnet;
    const QRegularExpression re(R"(^192\.168\.(\d{1,3})\.(\d{1,3})$)");

    for (const QString &rawIp : foundIps) {
        const QString ip = rawIp.trimmed();
        const auto m = re.match(ip);
        if (!m.hasMatch()) {
            continue;
        }
        const int subnet = m.captured(1).toInt();
        const int host = m.captured(2).toInt();
        if (subnet < 0 || subnet > 255 || host < 0 || host > 255) {
            continue;
        }

        auto it = chosenBySubnet.find(subnet);
        if (it == chosenBySubnet.end()) {
            chosenBySubnet.insert(subnet, ip);
            continue;
        }

        const QString &current = it.value();
        const auto cur = re.match(current);
        const int currentHost = cur.hasMatch() ? cur.captured(2).toInt() : -1;
        if (currentHost != 193 && host == 193) {
            it.value() = ip;
        }
    }

    const int stationCount = chosenBySubnet.size();
    if (stationCount == 0) {
        onDeviceLogMessage(QString("Радиостанции на %1 не найдены. Откройте настройки и выберите станцию/интерфейс.").arg(iface));
        return;
    }

    onDeviceLogMessage(QString("Найдено станций на %1: %2").arg(iface).arg(stationCount));

    // Если по итоговой логике выбора станция ровно одна — подключаемся автоматически.
    if (stationCount == 1) {
        const QString stationIp = chosenBySubnet.cbegin().value();
        QString selfIp;
        QString err;
        if (!ensureStationIpsConfigured(iface, stationIp, &selfIp, &err)) {
            onDeviceLogMessage(QString("Автоподключение не выполнено: %1").arg(err));
            return;
        }
        onDeviceLogMessage(QString("Автоподключение к станции %1 (интерфейс %2)...").arg(stationIp, iface));
        onStationConnectRequested(stationIp, selfIp, iface);
        return;
    }

    // Станций несколько — пользователь выберет в настройках.
    onDeviceLogMessage("Станций найдено несколько. Откройте настройки и выберите станцию для подключения.");
}

bool MainWindow::ensureStationIpsConfigured(const QString &interfaceName,
                                            const QString &stationIp,
                                            QString *chosenSelfIp,
                                            QString *errorText) const
{
    const QString activeNetwork = interfaceName.trimmed();
    if (activeNetwork.isEmpty()) {
        if (errorText) *errorText = "Сетевой интерфейс не выбран.";
        return false;
    }

    const QStringList ipParts = stationIp.trimmed().split('.');
    if (ipParts.size() != 4) {
        if (errorText) *errorText = QString("Некорректный IP станции: %1").arg(stationIp);
        return false;
    }
    const QString staNum = ipParts[2];
    const QString linearSubnet = ipParts[3];

    QString command = QString("nmcli -t -f UUID,DEVICE connection show --active | grep -F \":%1\" | cut -d':' -f1")
                          .arg(activeNetwork);
    QPair<bool, QString> result = executeCommand(command);
    if (!result.first || result.second.trimmed().isEmpty()) {
        if (errorText) {
            *errorText = QString("Ошибка: активное сетевое соединение для интерфейса %1 не найдено.")
                             .arg(activeNetwork);
        }
        return false;
    }

    const QString connectionUuid = result.second.trimmed().split('\n', Qt::SkipEmptyParts).value(0).trimmed();
    if (connectionUuid.isEmpty()) {
        if (errorText) *errorText = "Ошибка: UUID активного сетевого соединения пустой.";
        return false;
    }

    command = QString("nmcli -g ipv4.addresses connection show uuid \"%1\"").arg(connectionUuid);
    result = executeCommand(command);
    const QStringList ipList = result.second.split(QRegularExpression("[,/\\s]+"), Qt::SkipEmptyParts);

    const int startRange = (linearSubnet == "193") ? 194 : 2;
    const int endRange = (linearSubnet == "193") ? 255 : 127;

    QSet<QString> usedSubnetIps;
    for (int yCheck = startRange; yCheck <= endRange; ++yCheck) {
        const QString testIP = QString("192.168.%1.%2").arg(staNum).arg(yCheck);
        if (ipList.contains(testIP)) {
            usedSubnetIps.insert(testIP);
        }
    }

    QString addIP;
    const int maxAttempts = 128;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        const int randomHost = (linearSubnet == "193")
                                   ? QRandomGenerator::global()->bounded(194, 255)
                                   : QRandomGenerator::global()->bounded(2, 127);
        const QString candidate = QString("192.168.%1.%2").arg(staNum).arg(randomHost);
        if (!usedSubnetIps.contains(candidate)) {
            addIP = candidate;
            break;
        }
    }

    if (addIP.isEmpty()) {
        if (errorText) {
            *errorText = QString("Не удалось подобрать свободный IP для подсети 192.168.%1.*").arg(staNum);
        }
        return false;
    }

    const int cidr = (linearSubnet == "193") ? 26 : 25;
    command = QString("nmcli connection modify uuid \"%1\" ipv4.method manual +ipv4.addresses %2/%3")
                  .arg(connectionUuid).arg(addIP).arg(cidr);

    result = executeCommand(command);
    if (!result.first) {
        result = executeCommand(QString("sudo %1").arg(command));
    }
    if (!result.first) {
        if (errorText) *errorText = QString("Ошибка при добавлении IP-адреса %1/%2 для подключения UUID \"%3\": %4")
                                        .arg(addIP).arg(cidr).arg(connectionUuid, result.second.trimmed());
        return false;
    }

    // Переподнимаем интерфейс, чтобы адрес применился.
    command = QString("nmcli device disconnect \"%1\"").arg(activeNetwork);
    result = executeCommand(command);
    if (!result.first) {
        result = executeCommand(QString("sudo %1").arg(command));
    }
    if (!result.first) {
        if (errorText) *errorText = QString("Ошибка при перезагрузке сетевого интерфейса %1 (disconnect): %2")
                                        .arg(activeNetwork, result.second.trimmed());
        return false;
    }

    command = QString("nmcli device connect \"%1\"").arg(activeNetwork);
    result = executeCommand(command);
    if (!result.first) {
        result = executeCommand(QString("sudo %1").arg(command));
    }
    if (!result.first) {
        if (errorText) *errorText = QString("Ошибка при перезагрузке сетевого интерфейса %1 (connect): %2")
                                        .arg(activeNetwork, result.second.trimmed());
        return false;
    }

    if (chosenSelfIp) {
        *chosenSelfIp = addIP;
    }
    return true;
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
    // Номер станции берём из IP (подсеть 192.168.X.Y -> X)
    const QStringList parts = ip.trimmed().split('.');
    if (parts.size() == 4) {
        bool ok = false;
        const int stationNum = parts[2].toInt(&ok);
        if (ok) {
            ui->labelStation->setText(QString("Станция №%1").arg(stationNum));
        }
    }
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

void MainWindow::onAnalyzerConnected()
{
    setAnalyzerConnectedUi();
    onDeviceLogMessage("Успешное подключение к анализатору.");
}

void MainWindow::onAnalyzerDisconnected(const QString &reason)
{
    setAnalyzerDisconnectedUi();
    onDeviceLogMessage(QString("Анализатор отключен: %1").arg(reason));
}

void MainWindow::onAnalyzerLogMessage(const QString &msg)
{
    onDeviceLogMessage(msg);
}

void MainWindow::setStationConnectedUi() {
    ui->frameStation->setStyleSheet(styleSheetConnectStation);
    ui->labelPixStation->setPixmap(QPixmap(":/led_green.png"));
    ui->labelStateStation->setText("Подключена");
    ui->labelStateStation->setStyleSheet("color: #8AE08A;");
}

void MainWindow::setStationDisconnectedUi() {
    ui->frameStation->setStyleSheet(styleSheetDisconnectStation);
    ui->labelPixStation->setPixmap(QPixmap(":/led_red.png"));
    ui->labelStateStation->setText("Отключена");
    ui->labelStateStation->setStyleSheet("color: #ff5252;");
}

void MainWindow::setAnalyzerConnectedUi()
{
    ui->frameR3->setVisible(true);
    ui->frameR3->setStyleSheet(styleSheetConnectAnalyzer);
    ui->labelPixR3->setPixmap(QPixmap(":/led_green.png"));
    ui->labelStateR3->setText("Подключен");
    ui->labelStateR3->setStyleSheet("color: #8AE08A;");
}

void MainWindow::setAnalyzerDisconnectedUi()
{
    ui->frameR3->setStyleSheet(styleSheetDisconnectAnalyzer);
    ui->labelPixR3->setPixmap(QPixmap(":/led_red.png"));
    ui->labelStateR3->setText("Отключен");
    ui->labelStateR3->setStyleSheet("color: #ff5252;");
}
