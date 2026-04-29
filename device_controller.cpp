#include "device_controller.h"
#include <QDateTime>
#include <QDebug>

DeviceController::DeviceController(QObject *parent)
    : QObject(parent)
    , m_socket(new QUdpSocket(this))
    , m_connected(false)
    , m_tractPowerAckTimer(new QTimer(this))
    , m_lastPacketTime(0)
    , m_connectionLostReported(false)
    , m_connectionWatchdog(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
    , m_autoReconnectEnabled(false)
{
    for (int i = 0; i < CTRL_MAX_CHANNELS; ++i) {
        m_channels[i] = ChannelInfo();
    }

    connect(m_socket, &QUdpSocket::readyRead,
            this, &DeviceController::processPendingDatagrams);

    m_connectionWatchdog->setInterval(500);
    connect(m_connectionWatchdog, &QTimer::timeout,
            this, &DeviceController::checkConnectionTimeout);

    m_reconnectTimer->setInterval(RECONNECT_INTERVAL_MS);
    connect(m_reconnectTimer, &QTimer::timeout,
            this, &DeviceController::attemptReconnect);

    m_tractPowerAckTimer->setSingleShot(true);
    connect(m_tractPowerAckTimer, &QTimer::timeout,
            this, &DeviceController::onTractPowerAckTimeout);
}

DeviceController::~DeviceController() {
    if (m_tractPowerAckTimer) {
        m_tractPowerAckTimer->stop();
    }
    if (m_socket) {
        m_socket->close();
    }
    m_connected = false;
}

// bool DeviceController::loadConfig(const QString &filePath) {
//     Q_UNUSED(filePath);
//     emit logMessage("Конфигурация загружена из констант (protocol_consts.h)");
//     emit logMessage(QString("  Станция: %1:%2").arg(m_config.stationIp).arg(m_config.port));
//     emit logMessage(QString("  Контроллер: %1:%2").arg(m_config.selfIp).arg(m_config.pultPort));
//     return true;
// }

void DeviceController::setStationIp(const QString &ip) {
    m_config.stationIp = ip.trimmed();
}

void DeviceController::setSelfIp(const QString &ip) {
    m_config.selfIp = ip.trimmed();
}

bool DeviceController::initSocket() {
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->close();
    }

    QHostAddress bindAddr;
    if (!m_config.selfIp.isEmpty()) {
        bindAddr = QHostAddress(m_config.selfIp);
    } else {
        bindAddr = QHostAddress::Any;
    }

    quint16 bindPort = m_config.pultPort > 0 ? m_config.pultPort : CONTROLLER_PORT;

    if (!m_socket->bind(bindAddr, bindPort, QAbstractSocket::ShareAddress)) {
        emit errorOccurred("Ошибка привязки сокета: " + m_socket->errorString());
        return false;
    }

    emit logMessage(QString("Сокет привязан к %1:%2")
                        .arg(bindAddr.toString()).arg(bindPort));

    return true;
}

bool DeviceController::sendAck(const QByteArray &indicationPacket, int offset) {
    if (!m_connected || m_peerAddress.isNull()) {
        return false;
    }

    const uint8_t *buf = reinterpret_cast<const uint8_t*>(indicationPacket.constData());
    uint16_t seqNum = readUint16BE(buf + offset + 4);

    QByteArray ackPacket;
    ackPacket.resize(8);
    uint8_t *data = reinterpret_cast<uint8_t*>(ackPacket.data());

    writeUint16BE(data, ACK_MARKER);
    writeUint16BE(data + 2, 0x0004);
    writeUint16BE(data + 4, seqNum);
    writeUint16BE(data + 6, 0x0001);

    qint64 sent = m_socket->writeDatagram(ackPacket, m_peerAddress, m_peerPort);
    return sent != -1;
}

bool DeviceController::connectToDevice() {
    if (m_connected) {
        return true;
    }

    if (m_config.stationIp.isEmpty()) {
        emit errorOccurred("IP станции не выбран.");
        return false;
    }

    QHostAddress destAddr(m_config.stationIp);
    if (destAddr.isNull()) {
        emit errorOccurred(QString("Некорректный IP станции: %1").arg(m_config.stationIp));
        return false;
    }

    // Переинициализируем сокет только если он ещё не привязан или привязан к
    // другому self-IP. При повторных попытках подключения (таймер 5 с) важно
    // сохранить уже открытый сокет, иначе между close/bind можно пропустить
    // пришедший STARTACK от станции.
    bool needRebind = (m_socket->state() == QAbstractSocket::UnconnectedState);
    if (!needRebind && !m_config.selfIp.isEmpty()) {
        const QHostAddress desired(m_config.selfIp);
        if (!desired.isNull() && m_socket->localAddress() != desired) {
            needRebind = true;
        }
    }
    if (needRebind) {
        if (!initSocket()) {
            return false;
        }
    }

    QByteArray packet;
    packet.resize(16);
    uint8_t *data = reinterpret_cast<uint8_t*>(packet.data());

    writeUint16BE(data, MAIN_MARKER);
    writeUint16BE(data + 2, 0x000C);
    writeUint16BE(data + 4, 0x0000);
    writeUint16BE(data + 6, 0x0001);
    writeUint16BE(data + 8, 0x0001);

    writeUint16BE(data + 10, CMD_MOD_START);
    writeUint16BE(data + 12, 0x0002);
    data[14] = MODTYPE_DEFAULT;
    data[15] = m_config.pultNum;

    quint16 destPort = m_config.port > 0 ? m_config.port : STATION_PORT;

    emit logMessage(QString("Отправка MOD_START на %1:%2 (Пульт=%3)")
                        .arg(destAddr.toString()).arg(destPort).arg(m_config.pultNum));

    qint64 sent = m_socket->writeDatagram(packet, destAddr, destPort);
    if (sent == -1) {
        emit errorOccurred("Ошибка отправки пакета: " + m_socket->errorString());
        return false;
    }

    m_lastPacketTime = QDateTime::currentMSecsSinceEpoch();
    m_connectionLostReported = false;

    // Запускаем периодическую повторную отправку MOD_START — каждые
    // RECONNECT_INTERVAL_MS, пока станция не ответит STARTACK. Таймер
    // останавливается в parsePacket() при приёме STARTACK/индикации и в
    // disconnectFromDevice() при ручном отключении.
    m_autoReconnectEnabled = true;
    m_reconnectTimer->start();
    return true;
}

void DeviceController::setDisconnectedState(const QString &reason) {
    clearTractPowerPending();
    if (m_connectionWatchdog->isActive()) {
        m_connectionWatchdog->stop();
    }

    m_socket->close();
    m_connected = false;
    m_peerAddress = QHostAddress();
    m_peerPort = 0;

    for (int i = 0; i < CTRL_MAX_CHANNELS; ++i) {
        m_channels[i] = ChannelInfo();
    }

    emit disconnected();
    emit logMessage(reason);
}

void DeviceController::disconnectFromDevice() {
    m_autoReconnectEnabled = false;
    if (m_reconnectTimer->isActive()) {
        m_reconnectTimer->stop();
    }
    setDisconnectedState("Отключено от станции.");
}

void DeviceController::processPendingDatagrams() {
    while (m_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_socket->pendingDatagramSize());
        QHostAddress sender;
        quint16 senderPort;

        qint64 size = m_socket->readDatagram(
            datagram.data(), datagram.size(), &sender, &senderPort);

        if (size > 0) {
            m_lastPacketTime = QDateTime::currentMSecsSinceEpoch();
            m_connectionLostReported = false;
            parsePacket(datagram, sender, senderPort);
        }
    }
}

void DeviceController::checkConnectionTimeout() {
    // ВРЕМЕННО ОТКЛЮЧЕНО:
    // контроль "обрыва" и автопереподключение, которое после 3 секунд тишины
    // начинает периодически слать MOD_START (attemptReconnect/connectToDevice).
    //
    // TODO: включить обратно после отладки.
    Q_UNUSED(STATION_INACTIVITY_TIMEOUT_MS);
    return;
}

void DeviceController::attemptReconnect() {
    if (!m_autoReconnectEnabled) {
        if (m_reconnectTimer->isActive()) {
            m_reconnectTimer->stop();
        }
        return;
    }

    if (m_connected) {
        m_autoReconnectEnabled = false;
        if (m_reconnectTimer->isActive()) {
            m_reconnectTimer->stop();
        }
        return;
    }

    if (m_config.stationIp.isEmpty()) {
        emit errorOccurred("Невозможно переподключиться: IP станции не задан.");
        return;
    }

    emit logMessage(QString("Станция %1 не ответила, повторная отправка запроса подключения...")
                        .arg(m_config.stationIp));
    connectToDevice();
}

void DeviceController::parsePacket(const QByteArray &data,
                                   const QHostAddress &senderIp,
                                   quint16 senderPort) {
    if (data.size() < 4) {
        return;
    }

    const uint8_t *buf = reinterpret_cast<const uint8_t*>(data.constData());
    uint16_t tag = readUint16BE(buf);
    const uint8_t *payloadPtr = buf;
    int payloadOffset = 0;

    if (tag == MAIN_MARKER) {
        payloadOffset = HEADER_SIZE;
        if (data.size() < payloadOffset + 4) {
            return;
        }
        payloadPtr = buf + payloadOffset;
        sendAck(data, 0);
    } else if (tag == ACK_MARKER) {
        return;
    } else {
        return;
    }

    uint16_t desc = readUint16BE(payloadPtr);
    if (m_connected) {
        m_peerAddress = senderIp;
        m_peerPort = senderPort;
    }

    switch (desc) {
    case CMD_MOD_STARTACK:
        emit logMessage(QString("Получен STARTACK от %1").arg(senderIp.toString()));
        m_peerAddress = senderIp;
        m_peerPort = senderPort;
        if (!m_connected) {
            m_connected = true;
            m_lastPacketTime = QDateTime::currentMSecsSinceEpoch();
            m_connectionLostReported = false;
            m_connectionWatchdog->start();
            m_autoReconnectEnabled = false;
            if (m_reconnectTimer->isActive()) {
                m_reconnectTimer->stop();
            }
            emit connected(senderIp.toString());
            emit logMessage(QString("Р/станция %1 подключена").arg(senderIp.toString()));
        }
        break;
    case IND_TRAKT_OFF_SE:
    case IND_TRAKT_ON_SE:
        handleTractPowerIndication(data, payloadOffset, desc);
        break;
    default:
        if (desc >= 0x8000 && data.size() >= payloadOffset + 5) {
            uint8_t trLn = payloadPtr[4];
            if (!m_connected) {
                m_connected = true;
                m_peerAddress = senderIp;
                m_peerPort = senderPort;
                m_lastPacketTime = QDateTime::currentMSecsSinceEpoch();
                m_connectionLostReported = false;
                m_connectionWatchdog->start();
                m_autoReconnectEnabled = false;
                if (m_reconnectTimer->isActive()) {
                    m_reconnectTimer->stop();
                }
                emit connected(senderIp.toString());
                emit logMessage(QString("Р/станция %1 подключена (по индикации)")
                                    .arg(senderIp.toString()));
            }
            parseSPS(data, trLn, payloadOffset);
        }
        break;
    }
}

void DeviceController::handleTractPowerIndication(const QByteArray &data,
                                                 int payloadOffset,
                                                 uint16_t desc)
{
    if (data.size() < payloadOffset + 4) {
        return;
    }
    const uint8_t *payload = reinterpret_cast<const uint8_t*>(data.constData()) + payloadOffset;
    const uint16_t dlen = readUint16BE(payload + 2);
    if (data.size() < payloadOffset + 4 + static_cast<int>(dlen) || dlen < 3) {
        return;
    }

    const uint8_t *body = payload + 4;
    const uint8_t trLn = body[1];
    const uint8_t phase = body[2];
    const bool isOnEvt = (desc == IND_TRAKT_ON_SE);

    // phase==0 — станция сообщает старт операции, финал приходит с phase!=0
    if (phase == 0) {
        // Чтобы не засорять лог (и не вводить в заблуждение), показываем "старт операции"
        // только для той операции/тракта, которую мы прямо сейчас ждём как подтверждение.
        if (m_tractPowerPending != TractPowerPending::None && trLn == m_tractPowerPendingTract) {
            const bool expectingOn = (m_tractPowerPending == TractPowerPending::On);
            if ((expectingOn && isOnEvt) || (!expectingOn && !isOnEvt)) {
                emit logMessage(QString::fromUtf8("Станция: старт операции %1, тракт %2")
                                    .arg(isOnEvt ? QStringLiteral("включения") : QStringLiteral("выключения"))
                                    .arg(trLn));
            }
        }
        return;
    }

    if (m_tractPowerPending == TractPowerPending::None) {
        return;
    }
    if (trLn != m_tractPowerPendingTract) {
        return;
    }

    const bool expectingOn = (m_tractPowerPending == TractPowerPending::On);
    if (expectingOn && desc != IND_TRAKT_ON_SE) {
        return;
    }
    if (!expectingOn && desc != IND_TRAKT_OFF_SE) {
        return;
    }

    clearTractPowerPending();
    emit tractPowerAcknowledged(trLn, isOnEvt);
}

void DeviceController::clearTractPowerPending()
{
    if (m_tractPowerAckTimer) {
        m_tractPowerAckTimer->stop();
    }
    m_tractPowerPending = TractPowerPending::None;
    m_tractPowerPendingTract = 0;
}

void DeviceController::onTractPowerAckTimeout()
{
    if (m_tractPowerPending == TractPowerPending::None) {
        return;
    }
    const uint8_t t = m_tractPowerPendingTract;
    const bool expectedOn = (m_tractPowerPending == TractPowerPending::On);
    clearTractPowerPending();
    emit tractPowerAckTimeout(t, expectedOn);
}

void DeviceController::parseSPS(const QByteArray &data,
                                uint8_t tractNum,
                                int offset) {
    if (data.size() < 5) {
        return;
    }

    const uint8_t *buf = reinterpret_cast<const uint8_t*>(data.constData());
    uint16_t desc = readUint16BE(buf + offset);

    RadioStatus status;
    status.tractNum = tractNum;
    status.lastUpdateTime = QDateTime::currentMSecsSinceEpoch();
    bool updated = false;

    switch (desc) {
    case IND_FREQRX:
        if (data.size() >= offset + 9) {
            status.freqRX = readUint32BE(buf + offset + 5);
            emit freqRxIndicationReceived(tractNum, status.freqRX);
            updated = true;
        }
        break;
    case IND_FREQTX:
        if (data.size() >= offset + 9) {
            status.freqTX = readUint32BE(buf + offset + 5);
            emit freqTxIndicationReceived(tractNum, status.freqTX);
            updated = true;
        }
        break;
    case IND_WORKMODE:
        // Как в Station_starter: байт режима на смещении +5 от начала блока описания
        if (data.size() >= offset + 6) {
            const uint16_t mode = static_cast<uint16_t>(buf[offset + 5]);
            emit workModeIndicationReceived(tractNum, mode);
            updated = true;
        }
        break;
    case IND_RSSI:
        if (data.size() >= offset + 7) {
            status.rssi = static_cast<int16_t>(readUint16BE(buf + offset + 5));
            emit rssiIndicationReceived(tractNum, status.rssi);
            updated = true;
        }
        break;
    case IND_SNR:
        if (data.size() >= offset + 7) {
            status.snr = static_cast<int16_t>(readUint16BE(buf + offset + 5));
            updated = true;
        }
        break;
    case IND_CHREADY:
        if (data.size() >= offset + 6) {
            status.channelReady = buf[offset + 5];
            updated = true;
        }
        break;
    case IND_ERROR:
        // payload (как в пульте): [trLn:1][category:1][code_be16:2][0]
        // layout с учётом offset: desc(2)+len(2)+trLn(1)+category(1)+code(2) => минимум offset+8
        if (data.size() >= offset + 8) {
            const uint8_t category = buf[offset + 5];
            Q_UNUSED(category);
            const int16_t code = static_cast<int16_t>(readUint16BE(buf + offset + 6));
            emit ppmStatusIndicationReceived(tractNum, code);
            updated = true;
        }
        break;
    case IND_DIAGN_DEVICE:
        if (data.size() >= offset + 8) {
            static_cast<void>(readUint32BE(buf + offset + 4));
            updated = true;
        }
        break;
    default:
        break;
    }

    if (updated) {
        emit statusUpdated(QString("tract:%1").arg(status.tractNum));
    }
}

bool DeviceController::requestAllIndications(uint8_t tractNum) {
    if (!m_connected || m_peerAddress.isNull()) {
        return false;
    }

    QByteArray packet;
    packet.resize(15);
    uint8_t *data = reinterpret_cast<uint8_t*>(packet.data());

    writeUint16BE(data, MAIN_MARKER);
    writeUint16BE(data + 2, 0x0009);
    writeUint16BE(data + 4, 0x0000);
    writeUint16BE(data + 6, 0x0001);
    writeUint16BE(data + 8, 0x0001);
    writeUint16BE(data + 10, CMD_READ_ALL_INDIC);
    writeUint16BE(data + 12, 0x0001);
    data[14] = tractNum;

    m_socket->writeDatagram(packet, m_peerAddress, m_peerPort);
    return true;
}

bool DeviceController::setFrequencyRx(uint8_t tractNum, uint32_t freqHz) {
    if (!m_connected || m_peerAddress.isNull()) {
        emit errorOccurred("Нет подключения к станции!");
        return false;
    }

    QByteArray packet;
    packet.resize(19);
    uint8_t *data = reinterpret_cast<uint8_t*>(packet.data());

    writeUint16BE(data, MAIN_MARKER);
    writeUint16BE(data + 2, 0x000F);
    writeUint16BE(data + 4, 0x0000);
    writeUint16BE(data + 6, 0x0001);
    writeUint16BE(data + 8, 0x0001);
    writeUint16BE(data + 10, CMD_SET_FREQRX);
    writeUint16BE(data + 12, 0x0005);
    data[14] = tractNum;
    writeUint32BE(data + 15, freqHz);

    emit logMessage(QString("Установка частоты RX: Тракт=%1, Частота=%2 Гц")
                        .arg(tractNum).arg(freqHz));
    return m_socket->writeDatagram(packet, m_peerAddress, m_peerPort) != -1;
}

bool DeviceController::setFrequencyTx(uint8_t tractNum, uint32_t freqHz) {
    if (!m_connected || m_peerAddress.isNull()) {
        emit errorOccurred("Нет подключения к станции!");
        return false;
    }

    QByteArray packet;
    packet.resize(19);
    uint8_t *data = reinterpret_cast<uint8_t*>(packet.data());

    writeUint16BE(data, MAIN_MARKER);
    writeUint16BE(data + 2, 0x000F);
    writeUint16BE(data + 4, 0x0000);
    writeUint16BE(data + 6, 0x0001);
    writeUint16BE(data + 8, 0x0001);
    writeUint16BE(data + 10, CMD_SET_FREQTX);
    writeUint16BE(data + 12, 0x0005);
    data[14] = tractNum;
    writeUint32BE(data + 15, freqHz);

    emit logMessage(QString("Установка частоты TX: Тракт=%1, Частота=%2 Гц")
                        .arg(tractNum).arg(freqHz));
    return m_socket->writeDatagram(packet, m_peerAddress, m_peerPort) != -1;
}

bool DeviceController::setTractControl(uint8_t tractNum, bool enable, bool awaitAck) {
    if (!m_connected || m_peerAddress.isNull()) {
        emit errorOccurred("Нет подключения к станции!");
        return false;
    }
    if (awaitAck && isAwaitingTractPowerAck()) {
        emit errorOccurred("Уже ожидается подтверждение вкл/выкл тракта.");
        return false;
    }

    QByteArray packet;
    packet.resize(16);
    uint8_t *data = reinterpret_cast<uint8_t*>(packet.data());

    writeUint16BE(data, MAIN_MARKER);
    writeUint16BE(data + 2, 0x000C);
    writeUint16BE(data + 4, 0x0000);
    writeUint16BE(data + 6, 0x0001);
    writeUint16BE(data + 8, 0x0001);
    writeUint16BE(data + 10, CMD_TRACT_CONTROL);
    writeUint16BE(data + 12, 0x0002);
    data[14] = tractNum;
    data[15] = enable ? 1 : 0;

    emit logMessage(QString("Управление трактом: Тракт=%1, %2")
                        .arg(tractNum).arg(enable ? "ВКЛ" : "ВЫКЛ"));
    const bool sent = (m_socket->writeDatagram(packet, m_peerAddress, m_peerPort) != -1);
    if (!sent) {
        return false;
    }

    if (awaitAck) {
        m_tractPowerPending = enable ? TractPowerPending::On : TractPowerPending::Off;
        m_tractPowerPendingTract = tractNum;
        if (m_tractPowerAckTimer) {
            m_tractPowerAckTimer->start(TRACT_POWER_ACK_TIMEOUT_SEC * 1000);
        }
        emit tractPowerAwaitingAck(tractNum, enable);
    }
    return true;
}

bool DeviceController::setCurrentDirection(uint8_t tractNum, uint8_t dirId)
{
    if (!m_connected || m_peerAddress.isNull()) {
        emit errorOccurred("Нет подключения к станции!");
        return false;
    }

    QByteArray packet;
    packet.resize(16);
    uint8_t *data = reinterpret_cast<uint8_t*>(packet.data());

    writeUint16BE(data, MAIN_MARKER);
    writeUint16BE(data + 2, 0x000C);
    writeUint16BE(data + 4, 0x0000);
    writeUint16BE(data + 6, 0x0001);
    writeUint16BE(data + 8, 0x0001);
    // desc=0x0501, len=2, payload=[tractNum, dirId]
    writeUint16BE(data + 10, CMD_CURR_DIR_SET);
    writeUint16BE(data + 12, 0x0002);
    data[14] = tractNum;
    data[15] = dirId;

    emit logMessage(QString("Смена направления: Тракт=%1, DirId=%2 (cmd=0x%3)")
                        .arg(tractNum)
                        .arg(dirId)
                        .arg(QString::number(CMD_CURR_DIR_SET, 16).toUpper()));
    return m_socket->writeDatagram(packet, m_peerAddress, m_peerPort) != -1;
}

bool DeviceController::setTractMode(uint8_t tractNum, uint8_t mode) {
    if (!m_connected || m_peerAddress.isNull()) {
        emit errorOccurred("Нет подключения к станции!");
        return false;
    }

    QByteArray packet;
    packet.resize(16);
    uint8_t *data = reinterpret_cast<uint8_t*>(packet.data());

    writeUint16BE(data, MAIN_MARKER);
    writeUint16BE(data + 2, 0x000C);
    writeUint16BE(data + 4, 0x0000);
    writeUint16BE(data + 6, 0x0001);
    writeUint16BE(data + 8, 0x0001);
    writeUint16BE(data + 10, CMD_MOD_MODE);
    writeUint16BE(data + 12, 0x0002);
    data[14] = tractNum;
    data[15] = mode;

    emit logMessage(QString("Режим тракта: Тракт=%1, Режим=%2")
                        .arg(tractNum).arg(mode));
    return m_socket->writeDatagram(packet, m_peerAddress, m_peerPort) != -1;
}

void DeviceController::writeUint16BE(uint8_t *buf, uint16_t val) {
    buf[0] = (val >> 8) & 0xFF;
    buf[1] = val & 0xFF;
}

void DeviceController::writeUint32BE(uint8_t *buf, uint32_t val) {
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8) & 0xFF;
    buf[3] = val & 0xFF;
}

uint16_t DeviceController::readUint16BE(const uint8_t *data) {
    return (static_cast<uint16_t>(data[0]) << 8) | data[1];
}

uint32_t DeviceController::readUint32BE(const uint8_t *data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           data[3];
}
