#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "debug.h"
#include "styles.h"
#include "sweep_plot.h"
#include "qcustomplot.h"
#include "protocol_consts.h"
#include <QProcess>
#include <QTime>
#include <QScrollBar>
#include <QPixmap>
#include <algorithm>
#include <cmath>
#include <utility>
#include <QtConcurrent>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSet>
#include <QRandomGenerator>
#include <QMap>
#include <QStringList>
#include <QPushButton>
#include <QSlider>
#include <QSignalBlocker>

namespace {
QString formatHzTriplet(quint64 hz)
{
    const quint64 a = hz / 1000000ULL;
    const quint64 b = (hz / 1000ULL) % 1000ULL;
    const quint64 c = hz % 1000ULL;
    return QStringLiteral("%1.%2.%3")
        .arg(a, 3, 10, QLatin1Char('0'))
        .arg(b, 3, 10, QLatin1Char('0'))
        .arg(c, 3, 10, QLatin1Char('0'));
}

QString spectrumBwLabelText(int idx)
{
    switch (qBound(0, idx, 3)) {
    case 0:
        return QStringLiteral("2.5 кГц");
    case 1:
        return QStringLiteral("5 кГц");
    case 2:
        return QStringLiteral("10 кГц");
    default:
        return QStringLiteral("25 кГц");
    }
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_deviceController(new DeviceController(this))
    , m_analyzerController(new AnalyzerController(this))
    , m_finder(new FindManager(this))
{
    ui->setupUi(this);

    syncHandsFreqLineEdits(static_cast<quint64>(ANALYZER_STREAM_START_HZ_DEFAULT),
                           static_cast<quint64>(ANALYZER_STREAM_STOP_HZ_DEFAULT));
    syncSweepBoundsFromHz(static_cast<quint64>(ANALYZER_STREAM_START_HZ_DEFAULT),
                          static_cast<quint64>(ANALYZER_STREAM_STOP_HZ_DEFAULT));
    initSpectrumSpanCombo();
    syncSpectrumCenterSpanFromRangeHz(static_cast<quint64>(ANALYZER_STREAM_START_HZ_DEFAULT),
                                      static_cast<quint64>(ANALYZER_STREAM_STOP_HZ_DEFAULT));
    connect(ui->pushButtonChangeRange, &QPushButton::clicked, this, &MainWindow::onHandsSpectrumApplyClicked);
    connect(ui->pushButtonSpectrumCenterApply, &QPushButton::clicked, this,
            &MainWindow::onSpectrumCenterSpanApplyClicked);
    if (ui->pushButtonSpectrumCenterApply) {
        ui->pushButtonSpectrumCenterApply->setAutoDefault(false);
        ui->pushButtonSpectrumCenterApply->setDefault(false);
    }

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
    connect(m_analyzerController, &AnalyzerController::spectrumDataReceived,
            this, &MainWindow::onSpectrumDataReceived);

    // Подключение к анализатору должно начинаться автоматически при старте приложения.
    m_analyzerController->connectToDefaultPort();

    // Поиск интерфейсов/станций должен запускаться при старте программы.
    startAutoDiscovery();

    // Спектр: автозапуск при входе на вкладку tabHands
    connect(ui->tabWidget, &QTabWidget::currentChanged,
            this, &MainWindow::onTabWidgetCurrentChanged);

    m_tabHandsIndex =
        ui->tabWidget->indexOf(ui->tabWidget->findChild<QWidget *>("tabHands",
                                                                  Qt::FindDirectChildrenOnly));
    onTabWidgetCurrentChanged(ui->tabWidget->currentIndex());

    if (QPushButton *holdBtn = ui->pushButtonSpectrumClearHold) {
        holdBtn->setCheckable(true);
        holdBtn->setAutoDefault(false);
        holdBtn->setDefault(false);
        connect(holdBtn, &QPushButton::toggled, this, &MainWindow::onSpectrumMaxHoldToggled);
    }

    if (ui->horizontalSliderBW) {
        ui->horizontalSliderBW->setStyleSheet(styleSheetSpectrumBwSlider);
        updateSpectrumBwUi(ui->horizontalSliderBW->value());
        connect(ui->horizontalSliderBW, &QSlider::valueChanged,
                this, &MainWindow::onSpectrumBwSliderChanged);
    }

    m_spectrumUiTimer.setInterval(33);
    m_spectrumUiTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_spectrumUiTimer, &QTimer::timeout, this, &MainWindow::onSpectrumUiTimer);
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
        if (name.startsWith("eth") || name.startsWith("en") /*|| name.startsWith("wlan")*/) {
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
    m_analyzerConnected = true;
    setAnalyzerConnectedUi();
    onDeviceLogMessage("Успешное подключение к анализатору.");

    // Если пользователь уже на tabHands — запускаем стрим.
    if (m_startSpectrumOnHands) {
        startSpectrumStream();
    }
}

void MainWindow::onAnalyzerDisconnected(const QString &reason)
{
    m_analyzerConnected = false;
    m_startSpectrumOnHands = false;
    stopSpectrumStream();
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

void MainWindow::onTabWidgetCurrentChanged(int index)
{
    bool isHands = false;
    if (m_tabHandsIndex >= 0) {
        isHands = (index == m_tabHandsIndex);
    } else {
        QWidget *w = ui->tabWidget ? ui->tabWidget->widget(index) : nullptr;
        isHands = (w && w->objectName() == QStringLiteral("tabHands"));
    }

    m_startSpectrumOnHands = isHands;

    if (isHands) {
        if (m_analyzerConnected) {
            startSpectrumStream();
        }
    } else {
        stopSpectrumStream();
    }
}

void MainWindow::onSpectrumDataReceived(const QVector<double> &freqs,
                                         const QVector<double> &amps)
{
    if (!m_spectrumStreaming) {
        return;
    }
    if (freqs.isEmpty()) {
        return;
    }
    if (!m_spectrumPlotInitialized) {
        initSpectrumPlot();
    }

    if (!ui->plotWidget || !m_sweepTraces.liveTrace) {
        return;
    }

    if (isSpectrumMaxHoldOn()) {
        accumulateSpectrumMemory(m_spectrumMemoryAmps, freqs, amps);
    }

    m_spectrumLatestFreqs = freqs;
    m_spectrumLatestAmps = amps;
    m_spectrumDisplayDirty = true;

    if (!m_spectrumUiTimer.isActive()) {
        m_spectrumUiTimer.start();
    }
}

void MainWindow::onSpectrumUiTimer()
{
    if (!m_spectrumStreaming) {
        m_spectrumUiTimer.stop();
        return;
    }
    if (!m_spectrumDisplayDirty || m_spectrumLatestFreqs.isEmpty()) {
        m_spectrumUiTimer.stop();
        return;
    }
    m_spectrumDisplayDirty = false;
    redrawSpectrumDisplay();
}

void MainWindow::redrawSpectrumDisplay()
{
    if (!ui->plotWidget || !m_sweepTraces.liveTrace || m_spectrumLatestFreqs.isEmpty()) {
        updateSpectrumPeakReadout();
        return;
    }

    const bool hold = isSpectrumMaxHoldOn();
    const int w = qMax(1, ui->plotWidget->axisRect()->width());
    const int maxPts = qBound(240, w * 2, 1800);

    updateSweepSpectrumVisual(m_sweepTraces, m_spectrumLatestFreqs, m_spectrumLatestAmps,
                              hold, m_spectrumMemoryAmps, ui->plotWidget,
                              maxPts);
    updateSpectrumPeakReadout();
}

void MainWindow::initSpectrumPlot()
{
    if (!ui->plotWidget || m_spectrumPlotInitialized) {
        return;
    }

    ui->plotWidget->clearItems();
    ui->plotWidget->clearGraphs();
    m_sweepTraces = SweepPlotTraces{};

    quint64 sweepStartHz = static_cast<quint64>(ANALYZER_STREAM_START_HZ_DEFAULT);
    quint64 sweepStopHz = static_cast<quint64>(ANALYZER_STREAM_STOP_HZ_DEFAULT);
    if (!parseAndValidateHandsRangeHz(&sweepStartHz, &sweepStopHz)) {
        sweepStartHz = static_cast<quint64>(ANALYZER_STREAM_START_HZ_DEFAULT);
        sweepStopHz = static_cast<quint64>(ANALYZER_STREAM_STOP_HZ_DEFAULT);
    }
    const double xLoMHz = sweepStartHz / 1e6;
    const double xHiMHz = sweepStopHz / 1e6;
    setupFrequencySweepPlot(ui->plotWidget, xLoMHz, xHiMHz);
    syncSweepBoundsFromHz(sweepStartHz, sweepStopHz);

    m_sweepTraces = createSweepTraces(ui->plotWidget);
    m_spectrumMemoryAmps.clear();

    connect(ui->plotWidget->xAxis,
            static_cast<void (QCPAxis::*)(const QCPRange &, const QCPRange &)>(&QCPAxis::rangeChanged),
            this,
            [this](const QCPRange &, const QCPRange &) {
                clampSpectrumXAxisToSweep();
                scheduleSpectrumRedrawAfterAxisChange();
            });
    connect(ui->plotWidget->yAxis,
            static_cast<void (QCPAxis::*)(const QCPRange &, const QCPRange &)>(&QCPAxis::rangeChanged),
            this,
            [this](const QCPRange &, const QCPRange &) {
                clampSpectrumYAxisToDbmRange();
                scheduleSpectrumRedrawAfterAxisChange();
            });

    ui->plotWidget->replot();
    m_spectrumPlotInitialized = true;
}

void MainWindow::startSpectrumStream()
{
    if (m_spectrumStreaming) {
        return;
    }
    if (!m_analyzerConnected) {
        return;
    }

    if (!m_spectrumPlotInitialized) {
        initSpectrumPlot();
    }

    quint64 sweepStartHz = 0;
    quint64 sweepStopHz = 0;
    if (!parseAndValidateHandsRangeHz(&sweepStartHz, &sweepStopHz)) {
        onDeviceLogMessage(QStringLiteral(
            "Диапазон в полях не распознан; подставлены значения по умолчанию (220–470 МГц)."));
        sweepStartHz = static_cast<quint64>(ANALYZER_STREAM_START_HZ_DEFAULT);
        sweepStopHz = static_cast<quint64>(ANALYZER_STREAM_STOP_HZ_DEFAULT);
        syncHandsFreqLineEdits(sweepStartHz, sweepStopHz);
    }
    syncSpectrumCenterSpanFromRangeHz(sweepStartHz, sweepStopHz);
    m_analyzerController->setSpectrumRange(sweepStartHz, sweepStopHz);
    syncSweepBoundsFromHz(sweepStartHz, sweepStopHz);
    if (ui->plotWidget) {
        QSignalBlocker bx(ui->plotWidget->xAxis);
        QSignalBlocker by(ui->plotWidget->yAxis);
        ui->plotWidget->xAxis->setRange(sweepStartHz / 1e6, sweepStopHz / 1e6);
        ui->plotWidget->yAxis->setRange(-150.0, 20.0);
    }

    m_spectrumUiTimer.stop();
    m_spectrumDisplayDirty = false;
    m_spectrumLatestFreqs.clear();
    m_spectrumLatestAmps.clear();

    m_spectrumMemoryAmps.clear();
    if (m_sweepTraces.liveTrace) {
        m_sweepTraces.liveTrace->data()->clear();
    }
    if (m_sweepTraces.memoryTrace) {
        m_sweepTraces.memoryTrace->data()->clear();
        m_sweepTraces.memoryTrace->setVisible(isSpectrumMaxHoldOn());
    }

    if (ui->plotWidget) {
        ui->plotWidget->replot(QCustomPlot::rpQueuedReplot);
    }

    m_analyzerController->startSpectrumStream();
    m_spectrumStreaming = true;
}

bool MainWindow::parseHandsRangeHz(double *startHz, double *stopHz) const
{
    if (!ui->lineEditFreqStart || !ui->lineEditFreqStop || !startHz || !stopHz) {
        return false;
    }

    auto parseTriplet = [](const QString &text, double *out) -> bool {
        const QStringList p = text.trimmed().split(QLatin1Char('.'), Qt::SkipEmptyParts);
        if (p.size() != 3) {
            return false;
        }
        bool ok = false;
        const double a = p[0].toDouble(&ok);
        if (!ok) {
            return false;
        }
        const double b = p[1].toDouble(&ok);
        if (!ok) {
            return false;
        }
        const double c = p[2].toDouble(&ok);
        if (!ok) {
            return false;
        }
        *out = a * 1e6 + b * 1e3 + c;
        return true;
    };

    double s = 0.0;
    double t = 0.0;
    if (!parseTriplet(ui->lineEditFreqStart->text(), &s)) {
        return false;
    }
    if (!parseTriplet(ui->lineEditFreqStop->text(), &t)) {
        return false;
    }
    *startHz = s;
    *stopHz = t;
    return true;
}

bool MainWindow::parseAndValidateHandsRangeHz(quint64 *startHz, quint64 *stopHz) const
{
    if (!startHz || !stopHz) {
        return false;
    }
    double s = 0.0;
    double t = 0.0;
    if (!parseHandsRangeHz(&s, &t)) {
        return false;
    }
    quint64 su = static_cast<quint64>(s + 0.5);
    quint64 tu = static_cast<quint64>(t + 0.5);
    if (su == 0 || tu == 0) {
        return false;
    }
    if (su > tu) {
        std::swap(su, tu);
    }
    if (su >= tu) {
        return false;
    }
    if (tu > static_cast<quint64>(10000000000ULL)) {
        return false;
    }
    *startHz = su;
    *stopHz = tu;
    return true;
}

void MainWindow::syncHandsFreqLineEdits(quint64 startHz, quint64 stopHz)
{
    if (ui->lineEditFreqStart) {
        ui->lineEditFreqStart->setText(formatHzTriplet(startHz));
    }
    if (ui->lineEditFreqStop) {
        ui->lineEditFreqStop->setText(formatHzTriplet(stopHz));
    }
}

void MainWindow::initSpectrumSpanCombo()
{
    if (!ui->comboBoxSpectrumSpanMHz) {
        return;
    }
    ui->comboBoxSpectrumSpanMHz->clear();
    for (int mhz = 1; mhz <= 50; ++mhz) {
        ui->comboBoxSpectrumSpanMHz->addItem(QStringLiteral("%1 МГц").arg(mhz), mhz);
    }
}

void MainWindow::syncSpectrumCenterSpanFromRangeHz(quint64 startHz, quint64 stopHz)
{
    if (!ui->lineEditSpectrumCenterMHz || !ui->comboBoxSpectrumSpanMHz) {
        return;
    }
    const double centerMHz =
        (static_cast<double>(startHz) + static_cast<double>(stopHz)) * 0.5 / 1e6;
    ui->lineEditSpectrumCenterMHz->setText(QString::number(centerMHz, 'g', 12));

    const double widthMHz = static_cast<double>(stopHz - startHz) / 1e6;
    const int spanSel = qBound(1, static_cast<int>(qRound(widthMHz)), 50);
    const int idx = spanSel - 1;
    if (idx >= 0 && idx < ui->comboBoxSpectrumSpanMHz->count()) {
        ui->comboBoxSpectrumSpanMHz->setCurrentIndex(idx);
    }
}

bool MainWindow::spectrumBandFromCenterSpanMHz(double centerMHz,
                                               int spanMHz,
                                               quint64 *outStartHz,
                                               quint64 *outStopHz,
                                               QString *errorText) const
{
    if (!outStartHz || !outStopHz) {
        return false;
    }
    if (!std::isfinite(centerMHz) || spanMHz < 1 || spanMHz > 50) {
        if (errorText) {
            *errorText = QStringLiteral("Некорректные центр или span (1…50 МГц).");
        }
        return false;
    }
    const quint64 centerHz = static_cast<quint64>(std::llround(centerMHz * 1e6));
    const quint64 halfHz =
        static_cast<quint64>(std::llround(0.5 * static_cast<double>(spanMHz) * 1e6));
    if (centerHz < halfHz) {
        if (errorText) {
            *errorText = QStringLiteral("Для выбранного span центр слишком мал (нижняя граница < 0).");
        }
        return false;
    }
    const quint64 s = centerHz - halfHz;
    const quint64 e = centerHz + halfHz;
    if (e <= s) {
        if (errorText) {
            *errorText = QStringLiteral("Не удалось вычислить диапазон.");
        }
        return false;
    }
    if (e > static_cast<quint64>(10000000000ULL)) {
        if (errorText) {
            *errorText = QStringLiteral("Верхняя граница частоты превышает допустимую.");
        }
        return false;
    }
    *outStartHz = s;
    *outStopHz = e;
    return true;
}

void MainWindow::applySpectrumRangeHz(quint64 startHz, quint64 stopHz)
{
    m_analyzerController->setSpectrumRange(startHz, stopHz);
    syncHandsFreqLineEdits(startHz, stopHz);
    syncSweepBoundsFromHz(startHz, stopHz);
    syncSpectrumCenterSpanFromRangeHz(startHz, stopHz);
    if (ui->plotWidget) {
        QSignalBlocker bx(ui->plotWidget->xAxis);
        QSignalBlocker by(ui->plotWidget->yAxis);
        ui->plotWidget->xAxis->setRange(startHz / 1e6, stopHz / 1e6);
        ui->plotWidget->yAxis->setRange(-150.0, 20.0);
    }
    m_spectrumMemoryAmps.clear();
    if (m_sweepTraces.liveTrace) {
        m_sweepTraces.liveTrace->data()->clear();
    }
    if (m_sweepTraces.memoryTrace) {
        m_sweepTraces.memoryTrace->data()->clear();
        m_sweepTraces.memoryTrace->setVisible(isSpectrumMaxHoldOn());
    }
    m_spectrumDisplayDirty = true;
    if (m_spectrumStreaming && !m_spectrumUiTimer.isActive()) {
        m_spectrumUiTimer.start();
    }
    redrawSpectrumDisplay();
}

void MainWindow::onHandsSpectrumApplyClicked()
{
    quint64 s = 0;
    quint64 e = 0;
    if (!parseAndValidateHandsRangeHz(&s, &e)) {
        onDeviceLogMessage(QStringLiteral(
            "Диапазон: формат NNN.NNN.NNN Гц, начало < конец, разумные значения частоты."));
        return;
    }
    applySpectrumRangeHz(s, e);
    onDeviceLogMessage(QStringLiteral("Диапазон анализатора: %1 – %2 Гц").arg(s).arg(e));
}

void MainWindow::onSpectrumCenterSpanApplyClicked()
{
    if (!ui->lineEditSpectrumCenterMHz || !ui->comboBoxSpectrumSpanMHz) {
        return;
    }
    QString t = ui->lineEditSpectrumCenterMHz->text().trimmed();
    t.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double centerMHz = t.toDouble(&ok);
    const int spanMHz = ui->comboBoxSpectrumSpanMHz->currentData().toInt();
    if (!ok || spanMHz < 1) {
        onDeviceLogMessage(QStringLiteral("Введите корректную центральную частоту в МГц."));
        return;
    }
    QString err;
    quint64 s = 0;
    quint64 e = 0;
    if (!spectrumBandFromCenterSpanMHz(centerMHz, spanMHz, &s, &e, &err)) {
        onDeviceLogMessage(err.isEmpty() ? QStringLiteral("Не удалось вычислить диапазон.") : err);
        return;
    }
    applySpectrumRangeHz(s, e);
    onDeviceLogMessage(QStringLiteral("Диапазон: центр %1 МГц, span %2 МГц → %3 – %4 Гц")
                             .arg(centerMHz, 0, 'g', 12)
                             .arg(spanMHz)
                             .arg(s)
                             .arg(e));
}

bool MainWindow::isSpectrumMaxHoldOn() const
{
    return ui->pushButtonSpectrumClearHold && ui->pushButtonSpectrumClearHold->isChecked();
}

void MainWindow::onSpectrumMaxHoldToggled(bool checked)
{
    if (checked) {
        if (!m_spectrumLatestFreqs.isEmpty()
            && m_spectrumLatestAmps.size() == m_spectrumLatestFreqs.size()) {
            accumulateSpectrumMemory(m_spectrumMemoryAmps, m_spectrumLatestFreqs, m_spectrumLatestAmps);
        }
        m_spectrumDisplayDirty = true;
        if (m_spectrumStreaming && !m_spectrumUiTimer.isActive()) {
            m_spectrumUiTimer.start();
        }
    } else {
        m_spectrumMemoryAmps.clear();
        if (m_sweepTraces.memoryTrace) {
            m_sweepTraces.memoryTrace->data()->clear();
            m_sweepTraces.memoryTrace->setVisible(false);
        }
        m_spectrumDisplayDirty = true;
        if (m_spectrumStreaming && !m_spectrumUiTimer.isActive()) {
            m_spectrumUiTimer.start();
        }
    }
    redrawSpectrumDisplay();
    if (ui->plotWidget && !m_spectrumLatestFreqs.isEmpty()) {
        ui->plotWidget->replot();
    }
}

void MainWindow::updateSpectrumBwUi(int sliderIndex)
{
    if (ui->labelSpectrumBwValue) {
        ui->labelSpectrumBwValue->setText(spectrumBwLabelText(sliderIndex));
    }
}

void MainWindow::onSpectrumBwSliderChanged(int value)
{
    updateSpectrumBwUi(value);
    if (m_analyzerController) {
        m_analyzerController->setSpectrumBandwidth(value);
    }
}

void MainWindow::updateSpectrumPeakReadout()
{
    if (!ui->labelSpectrumPeakFreqValue || !ui->labelSpectrumPeakPowerValue) {
        return;
    }
    if (m_spectrumLatestFreqs.isEmpty()
        || m_spectrumLatestAmps.size() != m_spectrumLatestFreqs.size()) {
        ui->labelSpectrumPeakFreqValue->setText(QStringLiteral("—"));
        ui->labelSpectrumPeakPowerValue->setText(QStringLiteral("—"));
        return;
    }
    int best = 0;
    double bestAmp = m_spectrumLatestAmps[0];
    for (int i = 1; i < m_spectrumLatestAmps.size(); ++i) {
        if (m_spectrumLatestAmps[i] > bestAmp) {
            bestAmp = m_spectrumLatestAmps[i];
            best = i;
        }
    }
    ui->labelSpectrumPeakFreqValue->setText(
        QString::number(m_spectrumLatestFreqs[best], 'f', 6));
    ui->labelSpectrumPeakPowerValue->setText(QString::number(bestAmp, 'f', 1));
}

void MainWindow::syncSweepBoundsFromHz(quint64 startHz, quint64 stopHz)
{
    m_spectrumSweepMinMHz = static_cast<double>(startHz) / 1e6;
    m_spectrumSweepMaxMHz = static_cast<double>(stopHz) / 1e6;
    if (m_spectrumSweepMaxMHz <= m_spectrumSweepMinMHz) {
        m_spectrumSweepMaxMHz = m_spectrumSweepMinMHz + 1e-3;
    }
}

void MainWindow::clampSpectrumXAxisToSweep()
{
    if (!ui->plotWidget) {
        return;
    }
    QCPAxis *ax = ui->plotWidget->xAxis;
    const QCPRange r = ax->range();
    const double xmin = m_spectrumSweepMinMHz;
    const double xmax = m_spectrumSweepMaxMHz;
    double lo = r.lower;
    double hi = r.upper;
    bool changed = false;
    if (lo < xmin) {
        lo = xmin;
        changed = true;
    }
    if (hi > xmax) {
        hi = xmax;
        changed = true;
    }
    if (hi <= lo) {
        const double span = qMax(1e-6, xmax - xmin);
        hi = qMin(xmax, lo + 0.01 * span);
        if (hi <= lo) {
            lo = xmin;
            hi = xmax;
        }
        changed = true;
    }
    if (changed) {
        QSignalBlocker b(ax);
        ax->setRange(lo, hi);
    }
}

void MainWindow::clampSpectrumYAxisToDbmRange()
{
    static constexpr double kLo = -150.0;
    static constexpr double kHi = 20.0;
    if (!ui->plotWidget) {
        return;
    }
    QCPAxis *ax = ui->plotWidget->yAxis;
    const QCPRange r = ax->range();
    double lo = r.lower;
    double hi = r.upper;
    bool changed = false;
    if (lo < kLo) {
        lo = kLo;
        changed = true;
    }
    if (hi > kHi) {
        hi = kHi;
        changed = true;
    }
    if (hi <= lo) {
        hi = qMin(kHi, lo + 1.0);
        if (hi <= lo) {
            lo = kLo;
            hi = kHi;
        }
        changed = true;
    }
    if (changed) {
        QSignalBlocker b(ax);
        ax->setRange(lo, hi);
    }
}

void MainWindow::scheduleSpectrumRedrawAfterAxisChange()
{
    if (m_spectrumStreaming && !m_spectrumLatestFreqs.isEmpty()) {
        m_spectrumDisplayDirty = true;
        if (!m_spectrumUiTimer.isActive()) {
            m_spectrumUiTimer.start();
        }
    }
}

void MainWindow::stopSpectrumStream()
{
    if (!m_spectrumStreaming) {
        return;
    }

    m_spectrumUiTimer.stop();
    m_spectrumDisplayDirty = false;
    m_analyzerController->stopSpectrumStream();
    m_spectrumStreaming = false;
}
