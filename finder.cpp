#include "finder.h"

#include <QProcess>

#include <cerrno>
#include <cstring>

namespace {

struct TempAliasIp {
    QString ip;
    int mask = 0;
};

QPair<bool, QString> runShellCommand(const QString &command)
{
    QProcess process;
    process.start(QStringLiteral("/bin/bash"), QStringList() << QStringLiteral("-c") << command);
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(2000);
        return {false, QStringLiteral("Timeout: ") + command};
    }
    const QString out = QString::fromUtf8(process.readAllStandardOutput());
    const QString err = QString::fromUtf8(process.readAllStandardError());
    const bool ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    return {ok, out + err};
}

// Как в flasher_bku::checkStationAvailability: без alias в подсети станции ping до 192.168.X.*
// с другой подсети на интерфейсе не проходит, хотя ARP-ответ уже получен.
QVector<TempAliasIp> ensureAliasIpsForReachability(const QString &interfaceName,
                                                   const QVector<QString> &foundIps)
{
    QVector<TempAliasIp> added;
    added.reserve(foundIps.size());

    for (const QString &ip : foundIps) {
        const QStringList parts = ip.split('.');
        if (parts.size() != 4) {
            continue;
        }

        const QString subNet = parts[0] + QLatin1Char('.') + parts[1] + QLatin1Char('.') + parts[2];
        QString aliasIp;
        int mask = 0;
        if (parts[3].toInt() <= 127) {
            aliasIp = subNet + QStringLiteral(".15");
            mask = 25;
        } else {
            aliasIp = subNet + QStringLiteral(".211");
            mask = 26;
        }

        const QString checkCmd =
            QStringLiteral("ip a show %1 | grep '%2'").arg(interfaceName, aliasIp);
        const QPair<bool, QString> checkResult = runShellCommand(checkCmd);
        if (checkResult.first && !checkResult.second.trimmed().isEmpty()) {
            continue;
        }

        const QString addCmd =
            QStringLiteral("ip a a %1/%2 dev %3").arg(aliasIp).arg(mask).arg(interfaceName);
        QPair<bool, QString> addResult = runShellCommand(addCmd);
        if (!addResult.first) {
            addResult = runShellCommand(QStringLiteral("sudo %1").arg(addCmd));
        }
        if (addResult.first) {
            added.append({aliasIp, mask});
        }
    }

    return added;
}

void removeAliasIps(const QString &interfaceName, const QVector<TempAliasIp> &aliases)
{
    for (const TempAliasIp &alias : aliases) {
        const QString removeCmd =
            QStringLiteral("ip a d %1/%2 dev %3").arg(alias.ip).arg(alias.mask).arg(interfaceName);
        if (!runShellCommand(removeCmd).first) {
            runShellCommand(QStringLiteral("sudo %1").arg(removeCmd));
        }
    }
}

// Проверка, что хост реально отвечает на L3 (ICMP), а не только в ARP-кэше маршрутизатора.
bool pingHostReachable(const QString &ip)
{
    QProcess ping;
    ping.start(QStringLiteral("ping"),
               QStringList() << QStringLiteral("-n") << QStringLiteral("-c") << QStringLiteral("1")
                             << QStringLiteral("-W") << QStringLiteral("2") << ip);
    if (!ping.waitForStarted(3000)) {
        qWarning() << "ping: не удалось запустить для" << ip;
        return false;
    }
    if (!ping.waitForFinished(5000)) {
        ping.kill();
        ping.waitForFinished(2000);
        return false;
    }
    return ping.exitStatus() == QProcess::NormalExit && ping.exitCode() == 0;
}

} // namespace

ArpSenderThread::ArpSenderThread(int sock, const QString &src_ip, const QString &dst_ip, const uint8_t *src_mac, const QString &interface,
                                 QAtomicInt *errorCount, QAtomicInt *lastErrno)
    : m_sock(sock), m_src_ip(src_ip), m_dst_ip(dst_ip), m_src_mac(src_mac), m_interface(interface),
      m_errorCount(errorCount), m_lastErrno(lastErrno) {}

void ArpSenderThread::run() {
    struct arp_packet arp;
    if (inet_pton(AF_INET, m_src_ip.toUtf8(), arp.spa) <= 0 || inet_pton(AF_INET, m_dst_ip.toUtf8(), arp.tpa) <= 0) {
        qCritical() << "Ошибка: Невалидный IP-адрес.";
        return;
    }
    // Заполнение ARP-запроса
    arp.htype = htons(ARPHRD_ETHER);      // Ethernet
    arp.ptype = htons(ETH_P_IP);          // IPv4
    arp.hlen = ETH_ALEN;                  // MAC-адрес (6 байт)
    arp.plen = sizeof(uint32_t);          // IPv4-адрес (4 байта)
    arp.oper = htons(ARPOP_REQUEST);      // ARP-запрос
    memcpy(arp.sha, m_src_mac, ETH_ALEN); // MAC-адрес отправителя
    memset(arp.tha, 0, ETH_ALEN);         // MAC-адрес получателя
    // Заполнение Ethernet-заголовка
    constexpr size_t FRAME_SIZE = ETH_HLEN + sizeof(struct arp_packet);
    uint8_t frame[FRAME_SIZE];
    uint8_t broadcast_mac[ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(frame, broadcast_mac, ETH_ALEN); // MAC-адрес назначения
    memcpy(frame + ETH_ALEN, m_src_mac, ETH_ALEN); // MAC-адрес отправителя
    frame[12] = ETH_P_ARP / 256;           // EtherType (ARP)
    frame[13] = ETH_P_ARP % 256;
    // Копирование ARP-пакета в фрейм
    memcpy(frame + ETH_HLEN, &arp, sizeof(struct arp_packet));
    // Настройка адреса для отправки
    struct sockaddr_ll socket_address;
    memset(&socket_address, 0, sizeof(socket_address));
    socket_address.sll_family = AF_PACKET;
    socket_address.sll_protocol = htons(ETH_P_ARP);
    int ifindex = if_nametoindex(m_interface.toUtf8());
    if (ifindex == 0) {
        qCritical() << "Ошибка: Интерфейс" << m_interface << "не найден.";
        return;
    }
    socket_address.sll_ifindex = ifindex;
    socket_address.sll_halen = ETH_ALEN;
    memcpy(socket_address.sll_addr, broadcast_mac, ETH_ALEN);
    // Отправка пакета. Ошибки агрегируем в счётчике, чтобы не засорять
    // вывод одинаковыми строками "sendto: ..." на каждый из 510 пакетов.
    ssize_t sent_bytes = sendto(m_sock, frame, sizeof(frame), 0, (struct sockaddr *)&socket_address, sizeof(socket_address));
    if (sent_bytes < 0) {
        const int err = errno;
        if (m_errorCount) {
            m_errorCount->fetchAndAddRelaxed(1);
        }
        if (m_lastErrno) {
            m_lastErrno->storeRelaxed(err);
        }
    }
}

// Реализация ArpReceiverThread
ArpReceiverThread::ArpReceiverThread(int sock, QMutex &mtx, QVector<QString> &found_ips)
    : m_sock(sock), m_mtx(mtx), m_found_ips(found_ips) {}

void ArpReceiverThread::run() {
    uint8_t buffer[ETH_HLEN + sizeof(struct arp_packet)];
    struct sockaddr_ll socket_address;
    socklen_t addr_len = sizeof(socket_address);
    while (!isInterruptionRequested()) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(m_sock, &readfds);
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        int ret = select(m_sock + 1, &readfds, nullptr, nullptr, &timeout);
        if (ret == -1) {
            perror("select");
            break;
        } else if (ret == 0) {
            continue; // Таймаут истек, продолжаем цикл
        }
        ssize_t len = recvfrom(m_sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&socket_address, &addr_len);
        if (len < 0) {
            perror("recvfrom");
            continue;
        }
        // Проверка размера пакета
        if (len < static_cast<ssize_t>(ETH_HLEN + sizeof(struct arp_packet))) {
            qCritical() << "Ошибка: Некорректный размер ARP-пакета.";
            continue;
        }
        // Проверка типа пакета (EtherType)
        uint16_t ether_type = ntohs(*reinterpret_cast<uint16_t *>(buffer + 12));
        if (ether_type != ETH_P_ARP) {
            continue;
        }
        // Извлечение ARP-пакета
        struct arp_packet *arp = reinterpret_cast<struct arp_packet *>(buffer + ETH_HLEN);
        // Проверка, что это ARP-ответ
        if (ntohs(arp->oper) != ARPOP_REPLY) {
            continue;
        }
        // Извлечение IP-адреса отправителя
        uint32_t sender_ip = *(uint32_t *)arp->spa;
        uint8_t *ip_bytes = reinterpret_cast<uint8_t *>(&sender_ip);
        // Формирование полного IP-адреса
        QString sender_ip_str = QString("%1.%2.%3.%4")
                                    .arg(static_cast<int>(ip_bytes[0]))
                                    .arg(static_cast<int>(ip_bytes[1]))
                                    .arg(static_cast<int>(ip_bytes[2]))
                                    .arg(static_cast<int>(ip_bytes[3]));
        // Проверка шаблонов IP-адресов (192.168.x.1 и 192.168.x.193)
        if ((ip_bytes[0] == 192 && ip_bytes[1] == 168 && ip_bytes[3] == 1) ||
            (ip_bytes[0] == 192 && ip_bytes[1] == 168 && ip_bytes[3] == 193)) {
            QMutexLocker locker(&m_mtx);
            if (!m_found_ips.contains(sender_ip_str)) {
                m_found_ips.append(sender_ip_str); // Сохранение уникального IP
            }
        }
    }
}

FindManager::FindManager(QObject *parent)
    : QObject(parent) {}

FindManager::~FindManager() {}

// Создание raw socket
int FindManager::createRawSocket() {
    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock < 0) {
        perror("socket");
    }
    return sock;
}

// Получение MAC-адреса интерфейса
std::array<uint8_t, 6> FindManager::getMacAddress(const QString &interfaceName, bool *ok) {
    std::array<uint8_t, 6> src_mac{};
    if (ok) {
        *ok = false;
    }

    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        if (interface.name() != interfaceName) {
            continue;
        }
        const QStringList macParts = interface.hardwareAddress().split(':');
        if (macParts.size() != 6) {
            break;
        }
        for (int i = 0; i < 6; ++i) {
            src_mac[i] = static_cast<uint8_t>(macParts[i].toInt(nullptr, 16));
        }
        if (ok) {
            *ok = true;
        }
        break;
    }
    return src_mac;
}

QString FindManager::getIpv4Address(const QString &interfaceName) {
    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        if (interface.name() != interfaceName) {
            continue;
        }

        const QList<QNetworkAddressEntry> entries = interface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol &&
                !ip.isNull() &&
                ip != QHostAddress::LocalHost) {
                return ip.toString();
            }
        }
    }
    return QString();
}

// Поиск радиостанций
QVector<QString> FindManager::searchStations(const QString &interfaceName) {
    QVector<QString> found_ips;
    QMutex mtx;

    // Предварительная валидация интерфейса: если он не существует, не активен
    // или не имеет валидного MAC — не пытаемся отправлять ARP-запросы, иначе
    // ядро вернёт EINVAL на каждый из 510 sendto() и засорит вывод.
    QNetworkInterface iface = QNetworkInterface::interfaceFromName(interfaceName);
    if (!iface.isValid()) {
        qWarning() << "Сканирование пропущено: интерфейс" << interfaceName << "не найден.";
        return found_ips;
    }
    const auto flags = iface.flags();
    if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning)) {
        qWarning() << "Сканирование пропущено: интерфейс" << interfaceName
                   << "не активен (нужны флаги IsUp и IsRunning).";
        return found_ips;
    }

    // Получение MAC и проверка его валидности (не нулевой, 6 октетов).
    bool macOk = false;
    std::array<uint8_t, 6> srcMacArr = getMacAddress(interfaceName, &macOk);
    bool macNonZero = false;
    for (int i = 0; i < 6; ++i) {
        if (srcMacArr[i] != 0) { macNonZero = true; break; }
    }
    if (!macOk || !macNonZero) {
        qWarning() << "Сканирование пропущено: интерфейс" << interfaceName
                   << "не имеет валидного MAC-адреса.";
        return found_ips;
    }
    const uint8_t *src_mac = srcMacArr.data();

    // Создание raw socket
    int sock = createRawSocket();
    if (sock < 0) {
        qCritical() << "Ошибка создания сокета.";
        return found_ips;
    }

    // IPv4 выбранного интерфейса (без хардкода источника ARP).
    QString srcIp = getIpv4Address(interfaceName);
    if (srcIp.isEmpty()) {
        // Как в flasher_bku: фиксированный sender IP в ARP-запросе (не требует назначения на интерфейс).
        srcIp = QStringLiteral("192.168.1.22");
        qWarning() << "IPv4 адрес для интерфейса" << interfaceName
                   << "не найден, ARP-сканирование будет выполнено с src IP" << srcIp;
    }

    // Счётчики для агрегированного отчёта об ошибках sendto().
    QAtomicInt sendErrorCount(0);
    QAtomicInt sendLastErrno(0);

    // Запуск потока для получения ARP-ответов
    ArpReceiverThread receiver_thread(sock, mtx, found_ips);
    receiver_thread.start();

    // Отправка ARP-запросов параллельно
    QThreadPool pool;
    for (int x = 1; x <= 255; ++x) {
        QStringList target_ips;
        target_ips << QString("192.168.%1.1").arg(x);
        target_ips << QString("192.168.%1.193").arg(x);
        for (const QString &dst_ip : target_ips) {
            ArpSenderThread *task = new ArpSenderThread(sock, srcIp, dst_ip, src_mac, interfaceName,
                                                        &sendErrorCount, &sendLastErrno);
            task->setAutoDelete(true); // Автоматическое удаление задачи после выполнения
            pool.start(task);
        }
    }

    // Ожидание завершения отправки запросов
    pool.waitForDone();

    // Остановка потока получения ответов
    receiver_thread.requestInterruption();
    receiver_thread.wait();

    // Закрытие сокета
    ::close(sock);

    // Один агрегированный лог вместо 510 одинаковых perror-сообщений.
    const int errCount = sendErrorCount.loadRelaxed();
    if (errCount > 0) {
        const int lastErrnoVal = sendLastErrno.loadRelaxed();
        qWarning().noquote() << QString("ARP sendto: %1 ошибок отправки на интерфейсе %2 (последняя ошибка: %3)")
                                    .arg(errCount)
                                    .arg(interfaceName)
                                    .arg(QString::fromLocal8Bit(std::strerror(lastErrnoVal)));
    }

    const QVector<TempAliasIp> tempAliases = ensureAliasIpsForReachability(interfaceName, found_ips);

    QVector<QString> verified;
    verified.reserve(found_ips.size());
    for (const QString &ip : found_ips) {
        if (pingHostReachable(ip)) {
            verified.append(ip);
        } else {
            qInfo() << "Сканирование: IP" << ip
                    << "отброшен после ARP (нет ответа на ping — вероятен фантомный ARP маршрутизатора).";
        }
    }

    removeAliasIps(interfaceName, tempAliases);
    return verified;
}
