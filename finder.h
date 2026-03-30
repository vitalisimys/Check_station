#ifndef FINDER_H
#define FINDER_H

#include <QThreadPool>
#include <QMutex>
#include <QNetworkInterface>
#include <netinet/if_ether.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <unistd.h>

// Структура ARP-пакета
struct arp_packet {
    uint16_t htype; // Тип аппаратного адреса (Ethernet)
    uint16_t ptype; // Тип протокола (IPv4)
    uint8_t hlen;   // Длина аппаратного адреса (6 байт для MAC)
    uint8_t plen;   // Длина протокольного адреса (4 байта для IPv4)
    uint16_t oper;  // Операция (ARP-запрос или ARP-ответ)
    uint8_t sha[6]; // MAC-адрес отправителя
    uint8_t spa[4]; // IP-адрес отправителя
    uint8_t tha[6]; // MAC-адрес получателя
    uint8_t tpa[4]; // IP-адрес получателя
};

// Поток для отправки ARP-запросов
class ArpSenderThread : public QRunnable {

public:
    ArpSenderThread(int sock, const QString &src_ip, const QString &dst_ip, const uint8_t *src_mac, const QString &interface);

protected:
    void run() override;

private:
    int m_sock;
    QString m_src_ip;
    QString m_dst_ip;
    const uint8_t *m_src_mac;
    QString m_interface;
};

// Поток для получения ARP-ответов
class ArpReceiverThread : public QThread {
    Q_OBJECT

public:
    ArpReceiverThread(int sock, QMutex &mtx, QVector<QString> &found_ips);

protected:
    void run() override;

private:
    int m_sock;
    QMutex &m_mtx;
    QVector<QString> &m_found_ips;
};

// Класс для управления поиском радиостанций
class FindManager : public QObject {
    Q_OBJECT

public:
    explicit FindManager(QObject *parent = nullptr);
    ~FindManager();

    // Запуск поиска радиостанций
    QVector<QString> searchStations(const QString &interfaceName);

private:
    int createRawSocket();
    uint8_t *getMacAddress(const QString &interfaceName);
};

#endif // FINDER_H
