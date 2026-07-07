#include "power_traffic_generator.h"
#include "debug.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QThread>

PowerTrafficGenerator::PowerTrafficGenerator(QObject *parent)
    : QObject(parent)
    , m_socket(new QUdpSocket(this))
    , m_timer(new QTimer(this))
    , m_bindAddress(QHostAddress(QString::fromLatin1(CONTROLLER_IP)))
    , m_mcastAddress(TRAFFIC_MCAST_IP)
    , m_mcastPort(TRAFFIC_DST_PORT)
    , m_sourcePort(TRAFFIC_SRC_PORT)
    , m_dscp(DSCP_STREAMVOICE)
    , m_ecn(ECN_DEFAULT)
    , m_payloadType(RTP_PAYLOAD_TYPE)
    , m_tractNumber(DEFAULT_TRACT_NUM)
    , m_running(false)
    , m_seqNumber(0)
    , m_timestamp(0)
    , m_packetsSent(0)
{
    m_packetBuffer.resize(TRAFFIC_PACKET_SIZE);

    connect(m_timer, &QTimer::timeout, this, &PowerTrafficGenerator::onTrafficTimer);
    m_timer->setInterval(TRAFFIC_INTERVAL_MS);

    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, [this](QAbstractSocket::SocketError /*error*/) {
                if (m_running) {
                    const QString errText = m_socket->errorString().trimmed();
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                    static constexpr qint64 kSocketErrorLogThrottleMs = 2000;
                    const bool sameAsLast = (errText == m_lastSocketErrorText);
                    const bool throttled = sameAsLast && (nowMs - m_lastSocketErrorLogMs) < kSocketErrorLogThrottleMs;
                    if (!throttled) {
                        emit errorOccurred(errText);
                        m_lastSocketErrorText = errText;
                        m_lastSocketErrorLogMs = nowMs;
                    }
                }
            });
}

void PowerTrafficGenerator::setIntervalMs(int intervalMs)
{
    if (intervalMs <= 0) {
        return;
    }
    m_timer->setInterval(intervalMs);
}

PowerTrafficGenerator::~PowerTrafficGenerator()
{
    stop();
}

void PowerTrafficGenerator::setBindIp(const QString &ip)
{
    const QString trimmed = ip.trimmed();
    if (trimmed.isEmpty()) {
        m_bindAddress = QHostAddress(QString::fromLatin1(CONTROLLER_IP));
        return;
    }
    const QHostAddress addr(trimmed);
    if (!addr.isNull()) {
        m_bindAddress = addr;
    }
}

void PowerTrafficGenerator::setMulticastAddress(const QString &address)
{
    m_mcastAddress = QHostAddress(address);
}

void PowerTrafficGenerator::setMulticastPort(quint16 port)
{
    m_mcastPort = port;
}

void PowerTrafficGenerator::setSourcePort(quint16 port)
{
    m_sourcePort = port;
}

void PowerTrafficGenerator::setDscp(uint8_t dscp)
{
    m_dscp = dscp;
}

void PowerTrafficGenerator::setEcn(uint8_t ecn)
{
    m_ecn = ecn;
}

void PowerTrafficGenerator::setPayloadType(uint8_t pt)
{
    m_payloadType = pt;
}

void PowerTrafficGenerator::setTractNumber(uint8_t tract)
{
    m_tractNumber = tract;
}

bool PowerTrafficGenerator::bindToPort()
{
    if (m_socket->isOpen()) {
        m_socket->close();
    }

    QHostAddress bindAddr = m_bindAddress;
    if (bindAddr.isNull()) {
        bindAddr = QHostAddress::AnyIPv4;
    }

    if (!m_socket->bind(bindAddr, m_sourcePort,
                        QAbstractSocket::ShareAddress |
                            QAbstractSocket::ReuseAddressHint)) {
        emit errorOccurred(QStringLiteral("Не удалось привязаться к порту %1: %2")
                               .arg(m_sourcePort).arg(m_socket->errorString()));
        return false;
    }

    DEBUG << QStringLiteral("Трафик: сокет привязан к %1:%2")
                 .arg(bindAddr.toString()).arg(m_sourcePort);
    return true;
}

void PowerTrafficGenerator::configureSocket()
{
    const int tos = (m_dscp << 2) | m_ecn;
    m_socket->setSocketOption(QAbstractSocket::TypeOfServiceOption, tos);
    m_socket->setSocketOption(QAbstractSocket::MulticastTtlOption, 64);
    m_socket->setSocketOption(QAbstractSocket::MulticastLoopbackOption, 0);
}

bool PowerTrafficGenerator::start()
{
    if (m_running) {
        return false;
    }

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->close();
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }

    if (!bindToPort()) {
        return false;
    }

    configureSocket();

    m_packetsSent = 0;
    m_seqNumber = 0;
    m_timestamp = 0;
    m_lastSocketErrorText.clear();
    m_lastSocketErrorLogMs = 0;

    m_timer->start();
    m_running = true;

    DEBUG << QStringLiteral("📤 Трафик запущен: %1:%2 -> %3:%4")
                 .arg(m_bindAddress.toString())
                 .arg(m_sourcePort)
                 .arg(m_mcastAddress.toString())
                 .arg(m_mcastPort);
    emit started();
    return true;
}

void PowerTrafficGenerator::stop()
{
    if (!m_running) {
        return;
    }

    m_timer->stop();
    m_running = false;
    m_lastSocketErrorText.clear();
    m_lastSocketErrorLogMs = 0;

    if (m_socket && m_socket->isOpen()) {
        m_socket->close();
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }

    DEBUG << QStringLiteral("⏹ Трафик остановлен. Отправлено пакетов: %1")
                 .arg(m_packetsSent);
    emit stopped();
}

void PowerTrafficGenerator::onTrafficTimer()
{
    if (!m_running) {
        return;
    }

    preparePacket();
    const qint64 sent = m_socket->writeDatagram(m_packetBuffer, m_mcastAddress, m_mcastPort);
    if (sent > 0) {
        ++m_packetsSent;
    }
}

void PowerTrafficGenerator::preparePacket()
{
    // Поддержка профиля TETRA_HR (PT=80, payload 18 байт) для выхода на мощность.
    const bool tetraHrProfile = (m_payloadType == RTP_PAYLOAD_TYPE_TETRA_HR);
    const int payloadSize = tetraHrProfile ? RTP_PAYLOAD_SIZE_TETRA_HR : RTP_PAYLOAD_SIZE;
    const uint32_t tsStep = tetraHrProfile ? RTP_PAYLOAD_SIZE_TETRA_HR : 160;

    m_packetBuffer.resize(RTP_HEADER_SIZE + payloadSize);
    m_packetBuffer.fill(0x00);

    uint8_t *data = reinterpret_cast<uint8_t *>(m_packetBuffer.data());

    // RTP header
    data[0] = 0x80;
    data[1] = m_payloadType & 0x7F;

    data[2] = static_cast<uint8_t>((m_seqNumber >> 8) & 0xFF);
    data[3] = static_cast<uint8_t>(m_seqNumber & 0xFF);
    m_seqNumber = static_cast<quint16>((m_seqNumber + 1) & 0xFFFF);

    data[4] = static_cast<uint8_t>((m_timestamp >> 24) & 0xFF);
    data[5] = static_cast<uint8_t>((m_timestamp >> 16) & 0xFF);
    data[6] = static_cast<uint8_t>((m_timestamp >> 8) & 0xFF);
    data[7] = static_cast<uint8_t>(m_timestamp & 0xFF);
    m_timestamp += tsStep;

    data[8] = static_cast<uint8_t>((RTP_SSRC >> 24) & 0xFF);
    data[9] = static_cast<uint8_t>((RTP_SSRC >> 16) & 0xFF);
    data[10] = static_cast<uint8_t>((RTP_SSRC >> 8) & 0xFF);
    data[11] = static_cast<uint8_t>(RTP_SSRC & 0xFF);

    if (tetraHrProfile) {
        uint8_t cnt = 1;
        for (int i = 0; i < RTP_PAYLOAD_SIZE_TETRA_HR; ++i) {
            uint8_t b = static_cast<uint8_t>(cnt << 4);
            cnt = static_cast<uint8_t>((cnt + 1) & 0x0F);
            b |= cnt;
            cnt = static_cast<uint8_t>((cnt + 1) & 0x0F);
            data[RTP_HEADER_SIZE + i] = b;
        }
        data[RTP_HEADER_SIZE + RTP_PAYLOAD_SIZE_TETRA_HR - 1] &= 0x01;
    }
}

