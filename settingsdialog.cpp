#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include "debug.h"
#include "styles.h"
#include <QNetworkInterface>
#include <QProcess>
#include <QtConcurrent>
#include <QMessageBox>
#include <QRegularExpression>
#include <QMap>
#include <QRandomGenerator>
#include <QSet>
#include <QSignalBlocker>

SettingsDialog::SettingsDialog(QWidget *parent,
                               const QStringList &initialIfaces,
                               const QString &preselectedIface,
                               const QVector<QString> &cachedFoundIps)
    : QDialog(parent)
    , ui(new Ui::SettingsDialog)
    , m_finder(new FindManager(this))
{
    ui->setupUi(this);

    connect(ui->pushButtonConnectStation, &QPushButton::clicked,
            this, &SettingsDialog::onConnectStationClicked);
    connect(ui->findStationComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onStationSelectionChanged);

    // При первом входе в настройки кнопка должна быть заблокирована,
    // пока пользователь не выберет интерфейс и/или станцию (в зависимости от количества найденных).
    ui->pushButtonConnectStation->setEnabled(false);
    ui->findStationComboBox->setCurrentIndex(-1);
    ui->findStationComboBox->setPlaceholderText("Ожидание выбора интерфейса...");

    // Настройка networkComboBox: в настройки может быть передан уже готовый список интерфейсов.
    const bool haveInitial = !initialIfaces.isEmpty();
    // Если нам передали кэш найденных станций, это означает "просто показать уже готовое" —
    // автоподключение в этот момент не делаем.
    m_allowAutoConnectSingleStation = cachedFoundIps.isEmpty();
    if (haveInitial) {
        ui->networkComboBox->clear();
        for (const QString &name : initialIfaces) {
            ui->networkComboBox->addItem(name);
        }
    }

    // Если initialIfaces не передали — сканируем как раньше.
    if (haveInitial || loadNetworkInterfaces()) {
        ui->networkComboBox->setPlaceholderText("Выберите сетевой интерфейс подключения радиостанции");
        if (!preselectedIface.isEmpty()) {
            const int idx = ui->networkComboBox->findText(preselectedIface);
            if (idx >= 0) {
                const QSignalBlocker blocker(ui->networkComboBox);
                ui->networkComboBox->setCurrentIndex(idx);
                ui->networkComboBox->setPlaceholderText(preselectedIface);
            }
            // Если у нас есть кэш найденных IP для этого интерфейса — используем его,
            // чтобы не запускать повторный поиск при открытии настроек.
            if (!cachedFoundIps.isEmpty()) {
                ui->findStationComboBox->setPlaceholderText("Результат сканирования загружен");
                onScanFinished(cachedFoundIps);
            } else {
                // Кэша нет (например, настройки открыли раньше завершения автопоиска) —
                // запускаем поиск станций как обычно.
                QTimer::singleShot(0, this, [this, preselectedIface]() {
                    onNetworkInterfaceChanged(preselectedIface);
                });
            }
        } else if (ui->networkComboBox->count() == 1) {
            // Если интерфейс один — выбираем автоматически.
            // Также подменяем placeholderText на выбранное значение, чтобы UI
            // не показывал общий placeholder вместо конкретного интерфейса.
            const QString chosenInterface = ui->networkComboBox->itemText(0);
            ui->networkComboBox->setCurrentIndex(0);
            ui->networkComboBox->setPlaceholderText(chosenInterface);
            onNetworkInterfaceChanged(chosenInterface);
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

    // Опциональная фильтрация по состоянию NetworkManager:
    // исключаем "disconnected/unavailable/unmanaged" устройства,
    // чтобы в UI не попадали интерфейсы, которые фактически отключены.
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
            // Если nmcli вернул список устройств — используем его для отсечения отключенных.
            if (!nmcliAllowedDevices.isEmpty() && !nmcliAllowedDevices.contains(name)) {
                continue;
            }
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
        ui->pushButtonConnectStation->setEnabled(false);
        return;
    }

    qInfo() << "Запуск поиска радиостанций на интерфейсе:" << trimmedInterface;

    ui->findStationComboBox->clear();
    ui->findStationComboBox->setPlaceholderText("Сканирование...");
    ui->findStationComboBox->setCurrentIndex(-1);
    ui->pushButtonConnectStation->setEnabled(false);

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
    // По умолчанию кнопку показываем (для случаев 0 или >1 станций).
    // Для случая ровно одной станции ниже спрячем и подключимся автоматически.
    ui->pushButtonConnectStation->setVisible(true);
    ui->pushButtonConnectStation->setEnabled(false);

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

    const int stationCount = chosenBySubnet.size();
    ui->findStationComboBox->setPlaceholderText(
        stationCount == 0 ? "Радиостанции не найдены" : "Выберите найденную радиостанцию");

    if (stationCount == 0) {
        ui->findStationComboBox->setCurrentIndex(-1);
        ui->pushButtonConnectStation->setEnabled(false);
        return;
    }
    if (chosenBySubnet.size() == 1) {
        // Если станция одна — выбираем автоматически, кнопку скрываем и,
        // если это результат "живого" сканирования, подключаемся автоматически.
        ui->findStationComboBox->setCurrentIndex(0);
        ui->pushButtonConnectStation->setVisible(false);
        ui->pushButtonConnectStation->setEnabled(false);
        if (m_allowAutoConnectSingleStation) {
            QTimer::singleShot(0, this, [this]() {
                // Если интерфейсов несколько, а станция для выбранного интерфейса одна,
                // подключаемся и закрываем диалог автоматически.
                const bool shouldCloseDialog = (ui->networkComboBox->count() > 1);
                connectSelectedStation(shouldCloseDialog);
            });
        }
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
    const int count = ui->findStationComboBox->count();
    const bool hasSelection = index >= 0;
    if (count <= 0) {
        ui->pushButtonConnectStation->setEnabled(false);
    } else if (count == 1) {
        // При ровно одной станции кнопка скрыта, ручное включение не нужно.
        ui->pushButtonConnectStation->setEnabled(false);
    } else {
        ui->pushButtonConnectStation->setEnabled(hasSelection);
    }

    m_preparedStationIp.clear();
    m_preparedSelfIp.clear();
    if (!hasSelection) {
        return;
    }

    // ВАЖНО: здесь НЕ добавляем self-IP и не трогаем nmcli.
    // Подготовку/добавление IP выполняем строго в момент подключения
    // (в onConnectStationClicked), иначе IP может "повиснуть" без очистки.
    if (count > 1) {
        // Если для выбранного интерфейса найдено несколько станций —
        // после выбора станции подключаемся и закрываем диалог автоматически.
        connectSelectedStation(true);
    }
}

void SettingsDialog::onConnectStationClicked() {
    connectSelectedStation(false);
}

bool SettingsDialog::connectSelectedStation(bool closeAfterConnect) {
    const QString stationIp = selectedStationIp().trimmed();
    if (stationIp.isEmpty()) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Внимание");
        msgBox.setText("Выберите IP радиостанции в списке.");
        msgBox.setIcon(QMessageBox::Warning);
        QPushButton *okButton = new QPushButton("ОК", &msgBox);
        msgBox.addButton(okButton, QMessageBox::RejectRole);
        okButton->setStyleSheet(stylesheetButtonMessBox);
        msgBox.setStyleSheet(stylesheetMessBox);
        msgBox.exec();
        return false;
    }

    const QString iface = selectedInterface().trimmed();
    QString selfIp = m_preparedSelfIp.trimmed();

    if (m_preparedStationIp != stationIp || selfIp.isEmpty()) {
        QString err;
        if (!ensureStationIpsConfigured(iface, stationIp, &selfIp, &err)) {
            QMessageBox msgBox(this);
            msgBox.setWindowTitle("Внимание");
            msgBox.setText(err);
            msgBox.setIcon(QMessageBox::Warning);
            QPushButton *okButton = new QPushButton("ОК", &msgBox);
            msgBox.addButton(okButton, QMessageBox::RejectRole);
            okButton->setStyleSheet(stylesheetButtonMessBox);
            msgBox.setStyleSheet(stylesheetMessBox);
            msgBox.exec();
            return false;
        }
    }

    emit stationConnectRequested(stationIp, selfIp, iface);
    if (closeAfterConnect) {
        accept();
        return true;
    }

    // После подключения:
    // - если станций несколько, блокируем кнопку до нового выбора станции
    //   (и сбрасываем выбор, чтобы пользователь явно выбрал другую станцию).
    // - если станция одна, кнопка уже скрыта.
    if (ui->findStationComboBox->count() > 1) {
        ui->pushButtonConnectStation->setEnabled(false);
        const QSignalBlocker blocker(ui->findStationComboBox);
        ui->findStationComboBox->setCurrentIndex(-1);
        ui->findStationComboBox->setPlaceholderText("Выберите найденную радиостанцию");
    }
    // Диалог настроек не закрываем — пользователь может продолжить настройку.
    return true;
}
