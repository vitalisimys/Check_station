#include "flasher.h"

Flasher::Flasher(QString *bootcmdPtr, QObject *parent)
    : QObject(parent), bootcmdPtr(bootcmdPtr) {
    tftpServer = new TftpServer(this);
    connect(tftpServer, &TftpServer::logMessage, this, &Flasher::forwardLogMessage);
    connect(tftpServer, &TftpServer::progressChanged, this, &Flasher::forwardProgessChanged);
    connect(tftpServer, &TftpServer::startTransmitFile, this, &Flasher::forwardStartTransmitFile);
    connect(tftpServer, &TftpServer::transmitFinish, this, &Flasher::forwardTransmitFinish);
}

Flasher::~Flasher() {}

void Flasher::startCheckConnect(const QString &ip) {
    // Создаем новый поток для проверки подключения
    QThread *thread = new QThread;

    // Флаг для отслеживания первой попытки подключения
    static bool firstAttempt = true;
    static QTime startTime;

    // Перемещаем функцию connectToHost в отдельный поток
    QObject::connect(thread, &QThread::started, [=]() {
        // Логика ожидания: первая попытка через 1 минуту, последующие каждые 5 секунд
        if (firstAttempt) {
            firstAttempt = false;
            startTime = QTime::currentTime();
            // Ожидание 1 минуты перед первой попыткой
            QThread::msleep(90000);
        } else {
            // Ожидание 5 секунд между последующими попытками
            QThread::msleep(5000);
        }

        bool connected = ssher.connectToHost(ip);
        if (connected) {
            emit connectCompleted();
            firstAttempt = true; // Сброс флага после успешного подключения
        } else {
            // Если подключение не удалось, проверяем еще раз через 5с
            QTimer::singleShot(5000, this, [this, ip]() {
                startCheckConnect(ip);
            });
        }
        thread->quit();
    });

    // Удаляем поток после завершения работы
    QObject::connect(thread, &QThread::finished, thread, &QThread::deleteLater);

    // Запускаем поток
    thread->start();
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
    if (!ssher.connectToHost(ip)) {
        emit logMessage("Ошибка: Не удалось подключиться к устройству.", "red");
        return false;
    }
    if (!ssher.authenticate("root", "zxcvbn")) {
        emit logMessage("Ошибка: Аутентификация не удалась.", "red");
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
        // Обновлён путь к fw_setenv
        commandSetPPMs += QString("/usr/sbin/fw_setenv %1 %2").arg(ppmVariable).arg(ppmValue);
    }
    commandSetConfig += commandSetPPMs;
    ssher.executeCommand(commandSetConfig);

    // Сброс конфигурации (/usr/service/reset_to_factory.sh) и перезагрузка
    // Обновлён путь к reboot
    QString commandReset = "echo 1 > /sysconfig/first_run && "
                           "rm /sysconfig/staid && "
                           "sync && /sbin/reboot";
    ssher.executeCommand(commandReset);
    ssher.cleanup();
    return true;
}

void Flasher::readConfigFromUboot(const QString &ip) {
    if (!ssher.connectToHost(ip)) {
        emit logMessage("Ошибка: Не удалось подключиться к устройству.", "red");
        return;
    }
    if (!ssher.authenticate("root", "zxcvbn")) {
        emit logMessage("Ошибка: Аутентификация не удалась.", "red");
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
    values.removeAt(values.size() - 1);  

    if (values.size() == 7) {
        config.num_ppms = values[0].toInt();
        config.bi = config.stringToBool(values[1]);
        config.f_mv_3k = config.stringToBool(values[2]);
        config.f_dmv1_2k = config.stringToBool(values[3]);
        config.f_dmv2_2k = config.stringToBool(values[4]);
        config.gnss = config.stringToBool(values[5]);
        config.variant = values[6].toInt();
    } else {
        emit logMessage("Ошибка: Некорректная конфигурация радиостанции. Обновите вариант исполнения.", "red");
    }

    // Заполнение вектора PPM
    config.ppms.clear();
    QString command = "/usr/sbin/fw_printenv -n ppm_1";
    for (int i = 2; i <= config.num_ppms; ++i) {
        command += QString(" && /usr/sbin/fw_printenv -n ppm_%1").arg(i);
    }
    values = ssher.executeCommand(command).split('\n');
    values.removeAt(values.size() - 1);
    for (QString value : values) {
        QStringList parts = value.split('-');
        int type = parts[0].toInt(nullptr);
        int cnt = parts[1].toInt(nullptr);
        PPM ppm;
        ppm.type = type;
        ppm.cnt = cnt;
        config.ppms.append(ppm);
    }
    ssher.cleanup();
    return;
}

void Flasher::printConfig() {
    QString htmlTable = "<table border='1' cellpadding='5' cellspacing='0' style='border-collapse: collapse;'>";
    if ((config.variant == 16) || (config.variant == 18) || (config.variant == 19) || (config.variant == 20) || (config.variant == 22) || (config.variant == 25)){
        htmlTable += "<tr><td colspan='2' style='color: hsla(120, 100%, 60%, 0.7); text-align: center;'>Конфигурация U-Boot в БКИ</td></tr>";
    } else if ((config.variant == 21) || (config.variant == 23)){
        htmlTable += "<tr><td colspan='2' style='color: hsla(120, 100%, 60%, 0.7); text-align: center;'>Конфигурация U-Boot в БУ</td></tr>";
    } else {
        htmlTable += "<tr><td colspan='2' style='color: hsla(120, 100%, 60%, 0.7); text-align: center;'>Конфигурация U-Boot в БКУ</td></tr>";
    }
    auto typeToString = [](int type) -> QString {
        switch (type) {
        case 1: return "ДКМВ";
        case 2: return "МВ";
        case 3: return "ДМВ1";
        case 4: return "ДМВ2";
        default: return "Неизвестный тип";
        }
    };
    QString ppmInfo = QString::number(config.num_ppms);
    QMap<int, int> typeMaxCntMap;
    for (const PPM &ppm : config.ppms) {
        if (typeMaxCntMap.contains(ppm.type)) {
            typeMaxCntMap[ppm.type] = std::max(typeMaxCntMap[ppm.type], ppm.cnt);
        } else {
            typeMaxCntMap[ppm.type] = ppm.cnt;
        }
    }
    for (auto it = typeMaxCntMap.begin(); it != typeMaxCntMap.end(); ++it) {
        int type = it.key();
        int maxCnt = it.value();
        ppmInfo += QString("<br>Блок %1 - %2")
                       .arg(typeToString(type))
                       .arg(maxCnt);
    }
    htmlTable += createTableRow("Количество ППМ", ppmInfo);
    htmlTable += createTableRow("Блок Интерфейсов (БИ)", config.bi ? "в составе" : "отсутствует");
    htmlTable += createTableRow("Фильтр Ф-МВ-3К", config.f_mv_3k ? "в составе" : "отсутствует");
    htmlTable += createTableRow("Фильтр Ф-ДМВ1-2К", config.f_dmv1_2k ? "в составе" : "отсутствует");
    htmlTable += createTableRow("Фильтр Ф-ДМВ2-2К", config.f_dmv2_2k ? "в составе" : "отсутствует");
    htmlTable += createTableRow("Блок ГЛОНАСС", config.gnss ? "в составе" : "отсутствует");
    htmlTable += "</table>";
    emit textConfigFromUboot(htmlTable);
}

QString Flasher::createTableRow(const QString &key, const QString &value) {
    return "<tr>"
           "<td style='font-weight: bold; color: hsla(210, 100%, 70%, 0.8);'>" + key + "</td>"
                   "<td style='color: hsla(120, 100%, 60%, 0.7);'>" + value + "</td>"
                     "</tr>";
}

void Flasher::getSetConfig(const QString &ip) {
    readConfigFromUboot(ip);
    printConfig();
}

bool Flasher::checkingPort() {
    return (tftpServer->startServer() ? true : false);
}

void Flasher::startUpdating(const QString &ip, bool saveRadioData, const QString &variant) {
    // Подключение к устройству
    if (!ssher.connectToHost(ip)) {
        emit logMessage("Ошибка: Не удалось подключиться к устройству.", "red");
        return;
    }
    // Аутентификация
    if (!ssher.authenticate("root", "zxcvbn")) {
        emit logMessage("Ошибка: Аутентификация не удалась.", "red");
        ssher.cleanup();
        return;
    }

    // Установка bootcmd и serverip для запуска обновления
    ssher.executeCommand(*bootcmdPtr);

    // Сохранение радиоданных
    if (saveRadioData) {
        QString remoteBackupPath = "/tmp/profiles_backup.tar.gz";
        QString localBackupDir = QCoreApplication::applicationDirPath() + QString("/backup_%1/").arg(ip.split('.')[2]);
        QString localBackupPath = localBackupDir + "profiles_backup.tar.gz";

        // Создаем локальную папку /backup/
        QDir backupDir(localBackupDir);
        if (!backupDir.exists()) {
            backupDir.mkpath(".");
        }

        // Архивируем папку /radio/profiles/ на устройстве
        ssher.executeCommand("tar -cf /tmp/profiles_backup.tar.gz -C /radio profiles");

        // Передаем архив на компьютер
        if (!ssher.downloadFile(remoteBackupPath, localBackupPath)) {
            emit logMessage("Ошибка: Передача архива с радиоданными не удалась.", "red");
            return;
        }
    }

    // Установка переменных загрузки
    // Обновлены пути к fw_setenv
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
    // Обновлены пути к fw_setenv и reboot
    QString commandSetVariable = QString ("/usr/sbin/fw_setenv serverip 192.168.0.15 && "
                                         "/usr/sbin/fw_setenv ethact %1 && "
                                         "/usr/sbin/fw_setenv ethprime %2 && "
                                         "/usr/sbin/fw_setenv ethrotate no && "
                                         "/usr/sbin/fw_setenv bootdelay 3 && "
                                         "sync && "
                                         "/sbin/reboot").arg(ethact).arg(ethprime);
    ssher.executeCommand(commandSetVariable);

    // Освобождаем ресурсы
    ssher.cleanup();
    return;
}

QPair<QString, QString> Flasher::getcontent(const QString &ip) {
    // Подключение к устройству
    if (!ssher.connectToHost(ip)) {
        emit logMessage("Ошибка: Не удалось подключиться к устройству.", "red");
        return {};
    }
    // Аутентификация
    if (!ssher.authenticate("root", "zxcvbn")) {
        emit logMessage("Ошибка: Аутентификация не удалась.", "red");
        ssher.cleanup();
        return {};
    }

    // Устанавливаем bootcmd для случая аварийного завершения в процессе обновления
    // Обновлён путь к fw_setenv
    QString bootcmdCommand = QString("/usr/sbin/fw_setenv bootcmd \"run angstremcore1_boot\"");
    ssher.executeCommand(bootcmdCommand);

    // Восстанавливаем радиоданные
    QString localBackupDir = QCoreApplication::applicationDirPath() + QString("/backup_%1/").arg(ip.split('.')[2]);
    if (QDir(localBackupDir).exists()) {
        QString localBackupPath = localBackupDir + "profiles_backup.tar.gz";
        QString remoteBackupPath = "/tmp/profiles_backup.tar.gz";

        // Передаем архив profiles_backup.tar.gz на радиостанцию
        if (!ssher.uploadFile(localBackupPath, remoteBackupPath)) {
            emit logMessage("Ошибка: Не удалось восстановить радиоданные.", "red");
        } else {
            // Удаляем содержимое папки /radio/profiles/ на радиостанции
            // Распаковываем архив с радиоданными на устройстве в папку /radio/profiles/
            ssher.executeCommand("rm -rf /radio/profiles/ && tar -xf /tmp/profiles_backup.tar.gz -C /radio && rm -f /tmp/profiles_backup.tar.gz");
            //Удаляем на компьютере локальную папку /backup_/ с радиоданными
            QDir(localBackupDir).removeRecursively();
        }
    }

    // Получаем установленный в станции variant
    // Обновлён путь к fw_printenv
    QString variantCommand = QString("/usr/sbin/fw_printenv -n variant");
    QString variant = ssher.executeCommand(variantCommand);

    // Получаем /root/bku_version
    QString versionCommand = QString("/root/bku_version");
    QString version = ssher.executeCommand(versionCommand);

    // Освобождаем ресурсы
    ssher.cleanup();
    return qMakePair(variant, version);
}

void Flasher::finishUpdating(const QString &ip, bool needChangeLedColor, const QString &variant, QString &blocName) {
    // Подключение к устройству
    if (!ssher.connectToHost(ip)) {
        emit logMessage("Ошибка: Не удалось подключиться к устройству.", "red");
        return;
    }
    // Аутентификация
    if (!ssher.authenticate("root", "zxcvbn")) {
        emit logMessage("Ошибка: Аутентификация не удалась.", "red");
        ssher.cleanup();
        return;
    }

    // Изменяем значение leds_var, если светодиод ГОТОВ горит красным
    if (needChangeLedColor) {
        // Обновлён путь к fw_setenv и fw_printenv
        QString ledsVarCommand = QString("/usr/sbin/fw_setenv leds_var \"$(echo $((3 - $(/usr/sbin/fw_printenv -n leds_var))))\"");
        ssher.executeCommand(ledsVarCommand);
    }

    // Устанавливаем bootcmd для завершения обновления
    // Обновлён путь к fw_setenv
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
    emit logMessage(QString("Обновление программного обеспечения %1 радиостанции №%2 успешно завершено").arg(blocName).arg(ip.split(".")[2]), "green");

    // Освобождаем ресурсы
    ssher.cleanup();
    return;
}

// void Flasher::changeReadyLed(const QString &ip) {
//     // Подключение к устройству
//     if (!ssher.connectToHost(ip)) {
//         emit logMessage("Ошибка: Не удалось подключиться к устройству.", "red");
//         return;
//     }
//     // Аутентификация
//     if (!ssher.authenticate("root", "zxcvbn")) {
//         emit logMessage("Ошибка: Аутентификация не удалась.", "red");
//         ssher.cleanup();
//         return;
//     }

//     // Обновлены пути к fw_printenv и fw_setenv
//     if (ssher.executeCommand("/usr/sbin/fw_printenv -n leds_var").isEmpty()){
//         ssher.executeCommand("/usr/sbin/fw_setenv leds_var 1");
//     } else {
//         QString ledsVarCommand = QString("/usr/sbin/fw_setenv leds_var \"$(echo $((3 - $(/usr/sbin/fw_printenv -n leds_var))))\"");
//         ssher.executeCommand(ledsVarCommand);
//     }
//     return;
// }

bool Flasher::changeNumStation(const QString &ip, const QString &enteredNum) {
    // Подключение к устройству
    if (!ssher.connectToHost(ip)) {
        emit logMessage("Ошибка: Не удалось подключиться к устройству.", "red");
        return false;
    }
    // Аутентификация
    if (!ssher.authenticate("root", "zxcvbn")) {
        emit logMessage("Ошибка: Аутентификация не удалась.", "red");
        ssher.cleanup();
        return false;
    }

    // Замена номера радиостанции в /radio/configs/Configs.xml
    QString commandChangeConfigNum = QString("sed -i "
                                             "-e 's/<RouteId> .* <\\/RouteId>/<RouteId> %1 <\\/RouteId>/g' "
                                             "-e 's/<IpAxion> .* <\\/IpAxion>/<IpAxion> 10.0.0.%1 <\\/IpAxion>/g' "
                                             "-e 's/<IpMesh> .* <\\/IpMesh>/<IpMesh> 172.17.23.%1 <\\/IpMesh>/g' "
                                             "-e 's/<IpRST> .* <\\/IpRST>/<IpRST> 192.168.%1.1 <\\/IpRST>/g' "
                                             "-e 's/<RstNum> .* <\\/RstNum>/<RstNum> %1 <\\/RstNum>/g' "
                                             "/radio/configs/Configs.xml"
                                             ).arg(enteredNum);
    ssher.executeCommand(commandChangeConfigNum);

    // Обновлены пути к fw_setenv и reboot
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
    if (writeConfigToUboot(ip, enteredVar)) {
        return true;
    } else {
        return false;
    }
}
