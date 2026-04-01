#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include "debug.h"
#include <QNetworkInterface>
#include <QProcess>
#include <QtConcurrent>
#include <QMessageBox>
#include <QRegularExpression>
#include <QMap>
#include <QRandomGenerator>
#include <QSet>
#include <QSignalBlocker>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SettingsDialog)
    , m_finder(new FindManager(this))
{
    ui->setupUi(this);

    connect(ui->pushButtonConnectStation, &QPushButton::clicked,
            this, &SettingsDialog::onConnectStationClicked);
    connect(ui->findStationComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onStationSelectionChanged);

    ui->pushButtonConnectStation->setEnabled(true);
    ui->findStationComboBox->setCurrentIndex(-1);
    ui->findStationComboBox->setPlaceholderText("Ожидание выбора интерфейса...");

    // Настройка networkComboBox: всегда показываем список найденных интерфейсов.
    if (loadNetworkInterfaces()) {
        ui->networkComboBox->setPlaceholderText("Выберите сетевой интерфейс подключения радиостанции");
        if (ui->networkComboBox->count() == 1) {
            // Если интерфейс один — выбираем автоматически.
            ui->networkComboBox->setCurrentIndex(0);
            onNetworkInterfaceChanged(ui->networkComboBox->currentText());
        } else {
            const QSignalBlocker blocker(ui->networkComboBox);
            ui->networkComboBox->setCurrentIndex(-1);
        }
    } else {
        ui->networkComboBox->setPlaceholderText("Сетевые интерфейсы не найдены.");
    }

    connect(ui->networkComboBox, &QComboBox::currentTextChanged,
            this, &SettingsDialog::onNetworkInterfaceChanged);
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

QString SettingsDialog::selectedInterface() const {
    return ui->networkComboBox->currentText();
}

QString SettingsDialog::selectedStationIp() const {
    const QVariant data = ui->findStationComboBox->currentData(Qt::UserRole);
    if (data.isValid()) {
        const QString ip = data.toString().trimmed();
        if (!ip.isEmpty()) {
            return ip;
        }
    }
    return ui->findStationComboBox->currentText();
}

QPair<bool, QString> SettingsDialog::executeCommand(const QString &command) const
{
    QProcess process;
    process.start("/bin/bash", QStringList() << "-c" << command);
    // ВАЖНО: не ждём бесконечно, иначе "sudo" может повесить GUI.
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(2000);
        return {false, "Timeout выполнения команды: " + command};
    }
    const QString out = QString::fromUtf8(process.readAllStandardOutput());
    const QString err = QString::fromUtf8(process.readAllStandardError());
    // nmcli/ip часто пишут часть в stderr, поэтому возвращаем вместе
    const QString combined = (out + err);
    const bool ok = (process.exitStatus() == QProcess::NormalExit) && (process.exitCode() == 0);
    return {ok, combined};
}

bool SettingsDialog::ensureStationIpsConfigured(const QString &interfaceName,
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
    const QString linearSubnet = ipParts[3]; // как в flasher_bku: 193 => /26 и диапазон .194-.255, иначе /25 и .2-.127

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

    //bool ipExists = false;

    command = QString("nmcli -g ipv4.addresses connection show uuid \"%1\"").arg(connectionUuid);
    result = executeCommand(command);
    const QStringList ipList = result.second.split(QRegularExpression("[,/\\s]+"), Qt::SkipEmptyParts);

    const int startRange = (linearSubnet == "193") ? 194 : 2;
    const int endRange = (linearSubnet == "193") ? 255 : 127;

    // Сканируем какие IP уже есть в нужной подсети, чтобы:
    // - понять, что подсеть "известна" (ipExists)
    // - выбрать новый свободный рандомный адрес (не совпадающий с уже добавленными)
    QSet<QString> usedSubnetIps;
    for (int yCheck = startRange; yCheck <= endRange; ++yCheck) {
        const QString testIP = QString("192.168.%1.%2").arg(staNum).arg(yCheck);
        if (ipList.contains(testIP)) {
            usedSubnetIps.insert(testIP);
            //ipExists = true;
        }
    }

    QString addIP;
    QString selfIpAdded;
    bool selfIpAddedOk = false;
    // Выбираем новый рандомный IP в подсети станции, которого еще нет в ipv4.addresses
    // и используем его дальше как selfIp (IP контроллера).
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
    selfIpAdded = addIP;
    selfIpAddedOk = true;

    // Если мы что-то добавили (self ip — всегда добавляем), нужно переподнять интерфейс,
    // чтобы адреса применились.
    if (selfIpAddedOk) {
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
    }

    if (chosenSelfIp) {
        *chosenSelfIp = selfIpAdded;
    }

    return true;
}

bool SettingsDialog::loadNetworkInterfaces() {
    const QStringList ifaces = collectEligibleInterfaces();
    ui->networkComboBox->clear();

    if (ifaces.isEmpty()) {
        return false;
    }

    for (const QString &name : ifaces) {
        ui->networkComboBox->addItem(name);
    }
    return true;
}

QStringList SettingsDialog::collectEligibleInterfaces() const {
    QStringList result;
    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        // Фильтр: только up, running, без loopback
        if (!(interface.flags() & QNetworkInterface::IsUp) ||
            !(interface.flags() & QNetworkInterface::IsRunning) ||
            (interface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        if (interface.hardwareAddress().isEmpty()) {
            continue; // Виртуальные интерфейсы без MAC
        }

        // Допускаем ethernet-like имена (как было раньше)
        const QString name = interface.name();
        if (name.startsWith("eth") || name.startsWith("en") || name.startsWith("wlan")) {
            result.push_back(name);
        }
    }
    return result;
}

void SettingsDialog::onNetworkInterfaceChanged(const QString &interfaceName) {
    m_preparedStationIp.clear();
    m_preparedSelfIp.clear();

    const QString trimmedInterface = interfaceName.trimmed();
    if (trimmedInterface.isEmpty()) {
        ui->findStationComboBox->clear();
        ui->findStationComboBox->setPlaceholderText("Ожидание выбора интерфейса...");
        ui->pushButtonConnectStation->setEnabled(true);
        return;
    }

    qInfo() << "Запуск поиска радиостанций на интерфейсе:" << trimmedInterface;

    ui->findStationComboBox->clear();
    ui->findStationComboBox->setPlaceholderText("Сканирование...");
    ui->findStationComboBox->setCurrentIndex(-1);

    QtConcurrent::run([this, trimmedInterface]() {
        QVector<QString> found = m_finder->searchStations(trimmedInterface);
        // Возвращаем результат в главный поток через invokeMethod
        QMetaObject::invokeMethod(this, [this, found]() {
            onScanFinished(found);
        }, Qt::QueuedConnection);
    });
}

void SettingsDialog::onScanFinished(const QVector<QString> &foundIps) {
    m_preparedStationIp.clear();
    m_preparedSelfIp.clear();

    ui->findStationComboBox->clear();

    // Требование: если найдено несколько IP с одинаковой подсетью (192.168.X.*),
    // то добавлять/подключаться нужно к адресу *.193.
    //
    // Реализация: группируем по X (третьему октету) и выбираем приоритетно .193,
    // иначе берём первый найденный.
    QMap<int, QString> chosenBySubnet; // subnet -> chosen ip
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

        // приоритет: *.193
        if (currentHost != 193 && host == 193) {
            it.value() = ip;
        }
    }

    // Наполняем ComboBox строками "Станция №x", а реальный IP кладём в UserRole
    for (auto it = chosenBySubnet.cbegin(); it != chosenBySubnet.cend(); ++it) {
        const int subnet = it.key();
        const QString ip = it.value();
        ui->findStationComboBox->addItem(QString("Станция №%1").arg(subnet), ip);
    }

    ui->findStationComboBox->setPlaceholderText(
        chosenBySubnet.isEmpty() ? "Радиостанции не найдены" : "Выберите найденную радиостанцию");
    if (chosenBySubnet.size() == 1) {
        ui->findStationComboBox->setCurrentIndex(0);
        ui->pushButtonConnectStation->setEnabled(true);
        return;
    }

    if (chosenBySubnet.size() > 1) {
        ui->findStationComboBox->setCurrentIndex(-1);
        ui->pushButtonConnectStation->setEnabled(false);
        return;
    }

    ui->findStationComboBox->setCurrentIndex(-1);
    ui->pushButtonConnectStation->setEnabled(true);
}

void SettingsDialog::onStationSelectionChanged(int index) {
    const bool multipleStations = ui->findStationComboBox->count() > 1;
    const bool hasSelection = index >= 0;
    ui->pushButtonConnectStation->setEnabled(!multipleStations || hasSelection);

    m_preparedStationIp.clear();
    m_preparedSelfIp.clear();
    if (!hasSelection) {
        return;
    }

    const QString stationIp = selectedStationIp().trimmed();
    const QString iface = selectedInterface().trimmed();
    if (stationIp.isEmpty() || iface.isEmpty()) {
        ui->pushButtonConnectStation->setEnabled(false);
        return;
    }

    QString err;
    QString selfIp;
    if (!ensureStationIpsConfigured(iface, stationIp, &selfIp, &err)) {
        ui->pushButtonConnectStation->setEnabled(false);
        QMessageBox::critical(this, "Подготовка подключения", err);
        return;
    }

    m_preparedStationIp = stationIp;
    m_preparedSelfIp = selfIp.trimmed();
}

void SettingsDialog::onConnectStationClicked() {
    const QString stationIp = selectedStationIp().trimmed();
    if (stationIp.isEmpty()) {
        QMessageBox::warning(this, "Подключение", "Выберите IP радиостанции в списке.");
        return;
    }

    const QString iface = selectedInterface().trimmed();
    QString selfIp = m_preparedSelfIp.trimmed();

    if (m_preparedStationIp != stationIp || selfIp.isEmpty()) {
        QString err;
        if (!ensureStationIpsConfigured(iface, stationIp, &selfIp, &err)) {
            QMessageBox::critical(this, "Подключение", err);
            return;
        }
    }

    emit stationConnectRequested(stationIp, selfIp, iface);
    accept();
}
