#include "finder.h"
#include "debug.h"
#include <cerrno>
#include <cstring>
#include <QNetworkAddressEntry>

ArpSenderThread::ArpSenderThread(int sock, const QString &src_ip, const QString &dst_ip, const uint8_t *src_mac, const QString &interface)
    : m_sock(sock), m_src_ip(src_ip), m_dst_ip(dst_ip), m_src_mac(src_mac), m_interface(interface) {}

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
    // Отправка пакета через сокет, заранее привязанный к интерфейсу.
    // Это снижает вероятность EINVAL из-за sockaddr_ll на некоторых драйверах.
    ssize_t sent_bytes = send(m_sock, frame, sizeof(frame), 0);
    if (sent_bytes < 0) {
        const int err = errno;
        qCritical() << "send() не удался:"
                    << "errno=" << err
                    << "(" << strerror(err) << ")"
                    << "sock=" << m_sock
                    << "iface=" << m_interface
                    << "src_ip=" << m_src_ip
                    << "dst_ip=" << m_dst_ip
                    << "frame_size=" << sizeof(frame);
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

// Реализация FindManager
FindManager::FindManager(QObject *parent)
    : QObject(parent) {}

FindManager::~FindManager() {}

// Создание raw socket
int FindManager::createRawSocket() {
    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
    if (sock < 0) {
        perror("socket");
    }
    return sock;
}

std::optional<FindManager::InterfaceInfo> FindManager::resolveInterfaceInfo(const QString &interfaceName) const {
    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        if (interface.name() == interfaceName) {
            QString macAddress = interface.hardwareAddress();
            QStringList macParts = macAddress.split(':');
            if (macParts.size() != ETH_ALEN) {
                qCritical() << "Ошибка: MAC-адрес интерфейса" << interfaceName
                            << "имеет неожиданный формат:" << macAddress;
                return std::nullopt;
            }

            InterfaceInfo info{};
            info.ifindex = if_nametoindex(interfaceName.toUtf8().constData());
            if (info.ifindex == 0) {
                qCritical() << "Ошибка: не удалось получить ifindex для интерфейса"
                            << interfaceName << "errno:" << errno << "(" << strerror(errno) << ")";
                return std::nullopt;
            }

            for (int i = 0; i < 6; ++i) {
                bool ok = false;
                int value = macParts[i].toInt(&ok, 16);
                if (!ok || value < 0 || value > 0xFF) {
                    qCritical() << "Ошибка: некорректный MAC-байт" << macParts[i]
                                << "для интерфейса" << interfaceName;
                    return std::nullopt;
                }
                info.mac[i] = static_cast<uint8_t>(value);
            }

            for (const QNetworkAddressEntry &entry : interface.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                    info.ipv4 = entry.ip().toString();
                    break;
                }
            }

            if (info.ipv4.isEmpty()) {
                qCritical() << "Ошибка: у интерфейса" << interfaceName
                            << "нет IPv4-адреса, ARP-сканирование пропущено.";
                return std::nullopt;
            }

            return info;
        }
    }
    qCritical() << "Ошибка: интерфейс" << interfaceName << "не найден в QNetworkInterface::allInterfaces().";
    return std::nullopt;
}

// Поиск радиостанций
QVector<QString> FindManager::searchStations(const QString &interfaceName) {
    QVector<QString> found_ips;
    QMutex mtx;

    // Создание raw socket
    int sock = createRawSocket();
    if (sock < 0) {
        qCritical() << "Ошибка создания сокета.";
        return found_ips;
    }

    // Получение параметров интерфейса
    const auto interfaceInfo = resolveInterfaceInfo(interfaceName);
    if (!interfaceInfo.has_value()) {
        ::close(sock);
        return found_ips;
    }

    if (!interfaceInfo->ipv4.startsWith("192.168.")) {
        qWarning() << "Сканирование пропущено: интерфейс" << interfaceName
                   << "имеет IPv4" << interfaceInfo->ipv4
                   << ", а поиск выполняется только по диапазону 192.168.x.x";
        ::close(sock);
        return found_ips;
    }

    struct sockaddr_ll bind_address;
    memset(&bind_address, 0, sizeof(bind_address));
    bind_address.sll_family = AF_PACKET;
    bind_address.sll_protocol = htons(ETH_P_ARP);
    bind_address.sll_ifindex = interfaceInfo->ifindex;
    if (bind(sock, reinterpret_cast<struct sockaddr *>(&bind_address), sizeof(bind_address)) < 0) {
        qCritical() << "Ошибка bind(AF_PACKET) для интерфейса" << interfaceName
                    << "errno:" << errno << "(" << strerror(errno) << ")";
        ::close(sock);
        return found_ips;
    }

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
            ArpSenderThread *task = new ArpSenderThread(sock, interfaceInfo->ipv4, dst_ip, interfaceInfo->mac, interfaceName);
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

    return found_ips;
}
