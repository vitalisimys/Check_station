#include "flasher.h"

#include <QPointer>
#include <QtConcurrent/QtConcurrent>

Flasher::Flasher(QObject *parent)
    : QObject(parent) {
    // Прошивка БКУ ожидает SSH-сервер с устаревшим набором HostKey/KEX —
    // тот же режим включён везде в MainWindow при работе со станцией.
    ssher.setAllowLegacyAlgorithms(true);

    tftpServer = new TftpServer(this);
    connect(tftpServer, &TftpServer::logMessage, this, &Flasher::forwardLogMessage);
    connect(tftpServer, &TftpServer::progressChanged, this, &Flasher::forwardProgessChanged);
    connect(tftpServer, &TftpServer::transmitFinish, this, &Flasher::forwardTransmitFinish);
}

Flasher::~Flasher() {}

void Flasher::stopTftpServer() {
    if (tftpServer) {
        tftpServer->stopServer();
    }
}

void Flasher::emitUpdateFailed(const QString &message) {
    emit logMessage(message, QStringLiteral("red"));
    emit updateFailed(message);
}

void Flasher::setQuietConnectionErrors(bool quiet)
{
    m_quietConnectionErrors = quiet;
}

void Flasher::stopCheckConnect()
{
    m_checkConnectGeneration.fetchAndAddOrdered(1);
    m_firstCheckConnect.storeRelease(1);
}

void Flasher::startCheckConnect(const QString &ip, bool longInitialDelay)
{
    if (longInitialDelay) {
        m_firstCheckConnect.storeRelease(1);
    } else {
        m_firstCheckConnect.storeRelease(0);
    }

    const int generation = m_checkConnectGeneration.loadAcquire();
    // Первая попытка после прошивки — длинное ожидание (60-90с до завершения reboot/инициализации),
    // последующие — короткое; флаг сбрасывается на успехе.
    const bool firstAttempt = (m_firstCheckConnect.fetchAndStoreOrdered(0) == 1);
    const int delayMs = firstAttempt ? 90000 : 5000;

    QPointer<Flasher> self(this);
    QTimer::singleShot(delayMs, this, [self, ip, generation]() {
        if (!self || self->m_checkConnectGeneration.loadAcquire() != generation) {
            return;
        }
        QPointer<Flasher> selfInWorker(self);
        QtConcurrent::run([selfInWorker, ip, generation]() {
            if (!selfInWorker || selfInWorker->m_checkConnectGeneration.loadAcquire() != generation) {
                return;
            }
            // Используем независимый SSHer, чтобы не делить libssh2-сессию с UI-операциями.
            SSHer probe;
            probe.setAllowLegacyAlgorithms(true);
            const bool ok = probe.connectToHost(ip);
            probe.cleanup();

            QMetaObject::invokeMethod(qApp, [selfInWorker, ip, ok, generation]() {
                if (!selfInWorker || selfInWorker->m_checkConnectGeneration.loadAcquire() != generation) {
                    return;
                }
                if (ok) {
                    selfInWorker->m_firstCheckConnect.storeRelease(1);
                    emit selfInWorker->connectCompleted();
                } else {
                    selfInWorker->startCheckConnect(ip, false);
                }
            }, Qt::QueuedConnection);
        });
    });
}

void Flasher::loadConfig(uint variant) {
    switch (variant) {
    case 0: // conf_0=(3 2-1 3-1 4-1 + - - - + 0)
        config.num_ppms = 3;
        config.bi = true;
        config.f_mv_3k = false;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 0;
        config.ppms.clear();
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{3, 1});
        config.ppms.append(PPM{4, 1});
        break;
    case 1: // conf_1=(4 1-1 2-1 3-1 4-1 + - - - + 1)
        config.num_ppms = 4;
        config.bi = true;
        config.f_mv_3k = false;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 1;
        config.ppms.clear();
        config.ppms.append(PPM{1, 1});
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{3, 1});
        config.ppms.append(PPM{4, 1});
        break;
    case 2: // conf_2=(3 2-1 3-1 4-1 + - - - + 2)
        config.num_ppms = 3;
        config.bi = true;
        config.f_mv_3k = false;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 2;
        config.ppms.clear();
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{3, 1});
        config.ppms.append(PPM{4, 1});
        break;
    case 3: // conf_3=(4 1-1 2-1 3-1 4-1 + - - - + 3)
        config.num_ppms = 4;
        config.bi = true;
        config.f_mv_3k = false;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 3;
        config.ppms.clear();
        config.ppms.append(PPM{1, 1});
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{3, 1});
        config.ppms.append(PPM{4, 1});
        break;
    case 4: // conf_4=(2 2-1 3-1 - - - - + 4)
        config.num_ppms = 2;
        config.bi = false;
        config.f_mv_3k = false;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 4;
        config.ppms.clear();
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{3, 1});
        break;
    case 5: // conf_5=(2 2-1 3-1 - - - - + 5)
        config.num_ppms = 2;
        config.bi = false;
        config.f_mv_3k = false;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 5;
        config.ppms.clear();
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{3, 1});
        break;
    case 6: // conf_6=(3 1-1 2-1 3-1 + - - - + 6)
        config.num_ppms = 3;
        config.bi = true;
        config.f_mv_3k = false;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 6;
        config.ppms.clear();
        config.ppms.append(PPM{1, 1});
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{3, 1});
        break;
    case 7: // conf_7=(3 1-1 2-1 3-1 + - - - + 7)
        config.num_ppms = 3;
        config.bi = true;
        config.f_mv_3k = false;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 7;
        config.ppms.clear();
        config.ppms.append(PPM{1, 1});
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{3, 1});
        break;
    case 8: // conf_8=(2 1-1 2-1 + - - - + 8)
        config.num_ppms = 2;
        config.bi = true;
        config.f_mv_3k = false;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 8;
        config.ppms.clear();
        config.ppms.append(PPM{1, 1});
        config.ppms.append(PPM{2, 1});
        break;
    case 9: // conf_9=(4 1-1 1-2 2-1 3-1 + - - - + 9)
        config.num_ppms = 4;
        config.bi = true;
        config.f_mv_3k = false;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 9;
        config.ppms.clear();
        config.ppms.append(PPM{1, 1});
        config.ppms.append(PPM{1, 2});
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{3, 1});
        break;
    case 10: // conf_10=(3 1-1 2-1 3-1 + - - - + 10)
        config.num_ppms = 3;
        config.bi = true;
        config.f_mv_3k = false;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 10;
        config.ppms.clear();
        config.ppms.append(PPM{1, 1});
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{3, 1});
        break;
    case 16: // conf_16=(2 2-1 3-1 + - - - + 16) БКИ
        config.num_ppms = 2;
        config.bi = true;
        config.f_mv_3k = false;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 16;
        config.ppms.clear();
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{3, 1});
        break;
    case 17: // conf_17=(4 2-1 2-2 3-1 4-1 - + - + + 17)
        config.num_ppms = 4;
        config.bi = false;
        config.f_mv_3k = true;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = true;
        config.gnss = true;
        config.variant = 17;
        config.ppms.clear();
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{2, 2});
        config.ppms.append(PPM{3, 1});
        config.ppms.append(PPM{4, 1});
        break;
    case 18: // conf_18=(3 2-1 2-2 3-1 + - - - + 18) БКИ
        config.num_ppms = 3;
        config.bi = true;
        config.f_mv_3k = false;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 18;
        config.ppms.clear();
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{2, 2});
        config.ppms.append(PPM{3, 1});
        break;
    case 19: // conf_19=(3 2-1 3-1 4-1 + - - - + 19) БКИ
        config.num_ppms = 3;
        config.bi = true;
        config.f_mv_3k = false;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 19;
        config.ppms.clear();
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{3, 1});
        config.ppms.append(PPM{4, 1});
        break;
    case 20: // conf_20=(3 1-1 2-1 2-2 + + - - + 20) БКИ
        config.num_ppms = 3;
        config.bi = true;
        config.f_mv_3k = true;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 20;
        config.ppms.clear();
        config.ppms.append(PPM{1, 1});
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{2, 2});
        break;
    case 21: // conf_21=(3 2-1 3-1 4-1 + - - - + 21) БУ
        config.num_ppms = 3;
        config.bi = true;
        config.f_mv_3k = false;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 21;
        config.ppms.clear();
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{3, 1});
        config.ppms.append(PPM{4, 1});
        break;
    case 22: // conf_22=(3 2-1 3-1 4-1 + - - - + 22) БКИ
        config.num_ppms = 3;
        config.bi = true;
        config.f_mv_3k = false;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 22;
        config.ppms.clear();
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{3, 1});
        config.ppms.append(PPM{4, 1});
        break;
    case 23: // conf_23=(4 2-1 3-1 3-2 3-3 + - - - + 23) БУ
        config.num_ppms = 4;
        config.bi = true;
        config.f_mv_3k = false;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 23;
        config.ppms.clear();
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{3, 1});
        config.ppms.append(PPM{3, 2});
        config.ppms.append(PPM{3, 3});
        break;
    case 25: // conf_25=(3 3-1 2-1 3-2 - - - - + 25) БКИ
        config.num_ppms = 3;
        config.bi = false;
        config.f_mv_3k = false;
        config.f_dmv1_2k = false;
        config.f_dmv2_2k = false;
        config.gnss = true;
        config.variant = 25;
        config.ppms.clear();
        config.ppms.append(PPM{3, 1});
        config.ppms.append(PPM{2, 1});
        config.ppms.append(PPM{3, 2});
        break;
    default:
        emit logMessage(QString("Варианта исполнения №%1 не существует").arg(variant), "red");
        break;
    }
}

bool Flasher::writeConfigToUboot(const QString &ip, const uint &newVariant) {
    QMutexLocker locker(&sshMutex);
    if (!ssher.connectToHost(ip)) {
        if (!m_quietConnectionErrors) {
            emit logMessage("Ошибка: Не удалось подключиться к устройству.", "red");
        }
        return false;
    }
    if (!ssher.authenticate("root", "zxcvbn")) {
        emit logMessage("Ошибка: Аутентификация не удалась.", "red");
        ssher.cleanup();
        return false;
    }

    // Обновлены пути к утилитам fw_setenv и reboot
    QString commandSetConfig = QString ("/usr/sbin/fw_setenv num_ppms %1 && "
                                       "/usr/sbin/fw_setenv bi %2 && "
                                       "/usr/sbin/fw_setenv f-mv-3k %3 && "
                                       "/usr/sbin/fw_setenv f-dmv1-2k %4 && "
                                       "/usr/sbin/fw_setenv f-dmv2-2k %5 && "
                                       "/usr/sbin/fw_setenv gnss %6 && "
                                       "/usr/sbin/fw_setenv variant %7 && ")
                                   .arg(config.num_ppms)
                                   .arg(config.bi ? "+" : "-")
                                   .arg(config.f_mv_3k ? "+" : "-")
                                   .arg(config.f_dmv1_2k ? "+" : "-")
                                   .arg(config.f_dmv2_2k ? "+" : "-")
                                   .arg(config.gnss ? "+" : "-")
                                   .arg(QString::number(newVariant));

    QString commandSetPPMs;
    for (int i = 0; i < config.ppms.size(); ++i) {
        const PPM& ppm = config.ppms[i];
        QString ppmVariable = QString("ppm_%1").arg(i + 1); // ppm_1, ppm_2, ...
        QString ppmValue = QString("%1-%2").arg(ppm.type).arg(ppm.cnt);
        if (!commandSetPPMs.isEmpty()) {
            commandSetPPMs += " && ";
        }
        commandSetPPMs += QString("/usr/sbin/fw_setenv %1 %2").arg(ppmVariable).arg(ppmValue);
    }
    commandSetConfig += commandSetPPMs;
    ssher.executeCommand(commandSetConfig);

    // Сброс конфигурации (/usr/service/reset_to_factory.sh) и перезагрузка
    QString commandReset = "echo 1 > /sysconfig/first_run && "
                           "rm /sysconfig/staid && "
                           "sync && /sbin/reboot";
    ssher.executeCommand(commandReset);
    ssher.cleanup();
    return true;
}

void Flasher::readConfigFromUboot(const QString &ip) {
    QMutexLocker locker(&sshMutex);
    if (!ssher.connectToHost(ip)) {
        if (!m_quietConnectionErrors) {
            emit logMessage("Ошибка: Не удалось подключиться к устройству.", "red");
        }
        return;
    }
    if (!ssher.authenticate("root", "zxcvbn")) {
        emit logMessage("Ошибка: Аутентификация не удалась.", "red");
        ssher.cleanup();
        return;
    }

    QString commandReadConfig = "/usr/sbin/fw_printenv -n num_ppms && "
                                "/usr/sbin/fw_printenv -n bi && "
                                "/usr/sbin/fw_printenv -n f-mv-3k && "
                                "/usr/sbin/fw_printenv -n f-dmv1-2k && "
                                "/usr/sbin/fw_printenv -n f-dmv2-2k && "
                                "/usr/sbin/fw_printenv -n gnss && "
                                "/usr/sbin/fw_printenv -n variant";
    QStringList values = ssher.executeCommand(commandReadConfig).split('\n');
    if (!values.isEmpty()) {
        values.removeLast();
    }

    if (values.size() != 7) {
        emit logMessage("Ошибка: Некорректная конфигурация радиостанции. Обновите вариант исполнения.", "red");
        ssher.cleanup();
        return;
    }

    config.num_ppms = values[0].toInt();
    config.bi = config.stringToBool(values[1]);
    config.f_mv_3k = config.stringToBool(values[2]);
    config.f_dmv1_2k = config.stringToBool(values[3]);
    config.f_dmv2_2k = config.stringToBool(values[4]);
    config.gnss = config.stringToBool(values[5]);
    config.variant = values[6].toInt();

    // Заполнение вектора PPM
    config.ppms.clear();
    if (config.num_ppms <= 0) {
        ssher.cleanup();
        return;
    }

    QString command = "/usr/sbin/fw_printenv -n ppm_1";
    for (int i = 2; i <= config.num_ppms; ++i) {
        command += QString(" && /usr/sbin/fw_printenv -n ppm_%1").arg(i);
    }
    values = ssher.executeCommand(command).split('\n');
    if (!values.isEmpty()) {
        values.removeLast();
    }
    for (const QString &value : values) {
        const QStringList parts = value.split('-');
        if (parts.size() != 2) {
            emit logMessage(QString("Некорректный формат ppm_*: «%1»").arg(value), "red");
            continue;
        }
        PPM ppm;
        ppm.type = parts[0].toInt(nullptr);
        ppm.cnt = parts[1].toInt(nullptr);
        config.ppms.append(ppm);
    }
    ssher.cleanup();
}

void Flasher::getSetConfig(const QString &ip) {
    readConfigFromUboot(ip);
}

bool Flasher::checkingPort() {
    return tftpServer->startServer();
}

namespace {
QString stationOctet(const QString &ip)
{
    const QStringList parts = ip.split('.');
    if (parts.size() != 4) {
        return QString();
    }
    return parts.at(2);
}
} // namespace

void Flasher::startUpdating(const QString &ip, bool saveRadioData, const QString &variant, const QString &bootcmd) {
    QMutexLocker locker(&sshMutex);
    if (!ssher.connectToHost(ip)) {
        emitUpdateFailed(QStringLiteral("Ошибка: Не удалось подключиться к устройству."));
        return;
    }
    if (!ssher.authenticate("root", "zxcvbn")) {
        emitUpdateFailed(QStringLiteral("Ошибка: Аутентификация не удалась."));
        ssher.cleanup();
        return;
    }

    // Установка bootcmd и serverip для запуска обновления.
    // bootcmd передаём по значению, чтобы исключить data race на UI-стороне.
    ssher.executeCommand(bootcmd);

    // Сохранение радиоданных
    if (saveRadioData) {
        const QString octet = stationOctet(ip);
        if (octet.isEmpty()) {
            emitUpdateFailed(QStringLiteral("Некорректный IP радиостанции: %1").arg(ip));
            ssher.cleanup();
            return;
        }
        QString remoteBackupPath = "/tmp/profiles_backup.tar.gz";
        QString localBackupDir = QCoreApplication::applicationDirPath() + QString("/backup_%1/").arg(octet);
        QString localBackupPath = localBackupDir + "profiles_backup.tar.gz";

        QDir backupDir(localBackupDir);
        if (!backupDir.exists()) {
            backupDir.mkpath(".");
        }

        ssher.executeCommand("tar -cf /tmp/profiles_backup.tar.gz -C /radio profiles");

        if (!ssher.downloadFile(remoteBackupPath, localBackupPath)) {
            emitUpdateFailed(QStringLiteral("Ошибка: Передача архива с радиоданными не удалась."));
            ssher.cleanup();
            return;
        }
    }

    // Установка переменных загрузки
    QString commandBoot = "/usr/sbin/fw_setenv preboot \"if ping \\$serverip; then tftpboot 1000000 u-boot-spi-spl.bin; "
                          "sf probe 0; sf erase 0 100000; sf write 1000000 0 100000; "
                          "env set version_boot 01_eth_power; save; else run preboot; fi\" && "
                          "/usr/sbin/fw_setenv angstremtftp_kernel \"if ping \\$serverip; then tftpboot kernel.bin; "
                          "nand erase.part kernel; nand write \\$fileaddr kernel \\$filesize;else run angstremtftp_kernel; fi\" && "
                          "/usr/sbin/fw_setenv angstremtftp_rootfs \"if ping \\$serverip; then tftpboot rootfs.bin; nand erase.part rootfs; "
                          "nand write \\$fileaddr rootfs \\$filesize; else run angstremtftp_rootfs; fi\" && "
                          "/usr/sbin/fw_setenv angstremtftp_fdt \"if ping \\$serverip; then tftpboot bku-p2020.dtb; sf probe 0; sf erase 200000 20000; "
                          "sf write \\$fileaddr 200000 20000; else run angstremtftp_fdt; fi\"";
    ssher.executeCommand(commandBoot);

    // Установка переменных для сервера и перезагрузка
    QString ethact = "eTSEC2";
    QString ethprime = "eTSEC2";
    if ((variant == "21") || (variant == "23")) {
        ethact = "eTSEC1";
        ethprime = "eTSEC3";
    }
    QString commandSetVariable = QString ("/usr/sbin/fw_setenv serverip 192.168.0.15 && "
                                         "/usr/sbin/fw_setenv ethact %1 && "
                                         "/usr/sbin/fw_setenv ethprime %2 && "
                                         "/usr/sbin/fw_setenv ethrotate no && "
                                         "/usr/sbin/fw_setenv bootdelay 3 && "
                                         "sync && "
                                         "/sbin/reboot").arg(ethact).arg(ethprime);
    ssher.executeCommand(commandSetVariable);

    ssher.cleanup();
}

QPair<QString, QString> Flasher::getcontent(const QString &ip) {
    QMutexLocker locker(&sshMutex);
    if (!ssher.connectToHost(ip)) {
        if (!m_quietConnectionErrors) {
            emit logMessage("Ошибка: Не удалось подключиться к устройству.", "red");
        }
        return {};
    }
    if (!ssher.authenticate("root", "zxcvbn")) {
        emit logMessage("Ошибка: Аутентификация не удалась.", "red");
        ssher.cleanup();
        return {};
    }

    // Устанавливаем bootcmd для случая аварийного завершения в процессе обновления
    QString bootcmdCommand = QString("/usr/sbin/fw_setenv bootcmd \"run angstremcore1_boot\"");
    ssher.executeCommand(bootcmdCommand);

    // Восстанавливаем радиоданные
    const QString octet = stationOctet(ip);
    if (!octet.isEmpty()) {
        QString localBackupDir = QCoreApplication::applicationDirPath() + QString("/backup_%1/").arg(octet);
        if (QDir(localBackupDir).exists()) {
            QString localBackupPath = localBackupDir + "profiles_backup.tar.gz";
            QString remoteBackupPath = "/tmp/profiles_backup.tar.gz";

            if (!ssher.uploadFile(localBackupPath, remoteBackupPath)) {
                emit logMessage("Ошибка: Не удалось восстановить радиоданные.", "red");
            } else {
                ssher.executeCommand("rm -rf /radio/profiles/ && tar -xf /tmp/profiles_backup.tar.gz -C /radio && rm -f /tmp/profiles_backup.tar.gz");
                QDir(localBackupDir).removeRecursively();
            }
        }
    }

    QString variantCommand = QString("/usr/sbin/fw_printenv -n variant");
    QString variant = ssher.executeCommand(variantCommand);

    QString versionCommand = QString("/root/bku_version");
    QString version = ssher.executeCommand(versionCommand);

    ssher.cleanup();
    return qMakePair(variant, version);
}

void Flasher::finishUpdating(const QString &ip, bool needChangeLedColor, const QString &variant, QString &blocName) {
    QMutexLocker locker(&sshMutex);
    if (!ssher.connectToHost(ip)) {
        if (!m_quietConnectionErrors) {
            emit logMessage("Ошибка: Не удалось подключиться к устройству.", "red");
        }
        return;
    }
    if (!ssher.authenticate("root", "zxcvbn")) {
        emit logMessage("Ошибка: Аутентификация не удалась.", "red");
        ssher.cleanup();
        return;
    }

    if (needChangeLedColor) {
        QString ledsVarCommand = QString("/usr/sbin/fw_setenv leds_var \"$(echo $((3 - $(/usr/sbin/fw_printenv -n leds_var))))\"");
        ssher.executeCommand(ledsVarCommand);
    }

    QString bootcmdCommand = QString("/usr/sbin/fw_setenv bootcmd \"run angstremcore1_boot\"");
    ssher.executeCommand(bootcmdCommand);

    blocName = "БКУ";
    if ((variant == "21") || (variant == "23")) {
        blocName = "Блока управления";
    } else if ((variant == "16") ||
               (variant == "18") ||
               (variant == "19") ||
               (variant == "20") ||
               (variant == "22") ||
               (variant == "25")) {
        blocName = "БКИ";
    }
    const QString stationNum = stationOctet(ip);
    emit logMessage(QString("Обновление программного обеспечения %1 радиостанции №%2 успешно завершено")
                        .arg(blocName, stationNum.isEmpty() ? QStringLiteral("?") : stationNum),
                    "green");

    ssher.cleanup();
}

bool Flasher::changeNumStation(const QString &ip, const QString &enteredNum) {
    QMutexLocker locker(&sshMutex);
    if (!ssher.connectToHost(ip)) {
        if (!m_quietConnectionErrors) {
            emit logMessage("Ошибка: Не удалось подключиться к устройству.", "red");
        }
        return false;
    }
    if (!ssher.authenticate("root", "zxcvbn")) {
        emit logMessage("Ошибка: Аутентификация не удалась.", "red");
        ssher.cleanup();
        return false;
    }

    QString commandChangeConfigNum = QString("sed -i "
                                             "-e 's/<RouteId> .* <\\/RouteId>/<RouteId> %1 <\\/RouteId>/g' "
                                             "-e 's/<IpAxion> .* <\\/IpAxion>/<IpAxion> 10.0.0.%1 <\\/IpAxion>/g' "
                                             "-e 's/<IpMesh> .* <\\/IpMesh>/<IpMesh> 172.17.23.%1 <\\/IpMesh>/g' "
                                             "-e 's/<IpRST> .* <\\/IpRST>/<IpRST> 192.168.%1.1 <\\/IpRST>/g' "
                                             "-e 's/<RstNum> .* <\\/RstNum>/<RstNum> %1 <\\/RstNum>/g' "
                                             "/radio/configs/Configs.xml"
                                             ).arg(enteredNum);
    ssher.executeCommand(commandChangeConfigNum);

    QString commandChangeNum = QString("/usr/sbin/fw_setenv id4 %1 && echo %1 > /sysconfig/staid && /sbin/reboot").arg(enteredNum);
    ssher.executeCommand(commandChangeNum);
    ssher.cleanup();
    return true;
}

bool Flasher::changeVarStation(const QString &ip, const uint &enteredVar) {
    loadConfig(enteredVar);
#ifdef QDEBUG
    qDebug() << "PPMConfig Details:";
    qDebug() << "  num_ppms:" << config.num_ppms;
    qDebug() << "  bi:" << config.bi;
    qDebug() << "  f_mv_3k:" << config.f_mv_3k;
    qDebug() << "  f_dmv1_2k:" << config.f_dmv1_2k;
    qDebug() << "  f_dmv2_2k:" << config.f_dmv2_2k;
    qDebug() << "  gnss:" << config.gnss;
    qDebug() << "  variant:" << config.variant;
    qDebug() << "  ppms (size):" << config.ppms.size();
    for (int i = 0; i < config.ppms.size(); ++i) {
        const PPM& ppm = config.ppms[i];
        qDebug() << "    PPM[" << i << "]: type =" << ppm.type << ", cnt =" << ppm.cnt;
    }
#endif
    return writeConfigToUboot(ip, enteredVar);
}
