#ifndef DEVICE_CONTROLLER_H
#define DEVICE_CONTROLLER_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QMutex>
#include <QDateTime>
#include <stdint.h>
#include <string.h>
#include "protocol_consts.h"

struct DeviceConfig {
    QString selfIp;
    QString stationIp;
    uint16_t port;
    uint16_t pultNum;
    uint16_t pultPort;

    DeviceConfig()
        : selfIp(CONTROLLER_IP)
        , stationIp(STATION_IP)
        , port(STATION_PORT)
        , pultNum(DEFAULT_PULT_NUM)
        , pultPort(CONTROLLER_PORT)
    {}
};

struct RadioStatus {
    uint8_t tractNum;
    uint32_t freqRX;
    uint32_t freqTX;
    int16_t rssi;
    int16_t snr;
    uint16_t swr;
    uint8_t channelReady;
    qint64 lastUpdateTime;

    RadioStatus()
        : tractNum(0)
        , freqRX(0)
        , freqTX(0)
        , rssi(0)
        , snr(0)
        , swr(0)
        , channelReady(0)
        , lastUpdateTime(0)
    {}
};

class DeviceController : public QObject {
    Q_OBJECT

public:
    explicit DeviceController(QObject *parent = nullptr);
    ~DeviceController();

    //bool loadConfig(const QString &filePath);
    const DeviceConfig& config() const { return m_config; }
    void setSelfIp(const QString &ip);
    void setStationIp(const QString &ip);

    bool connectToDevice();
    void disconnectFromDevice();
    /// Пока false — checkConnectionTimeout не переводит канал в «обрыв» (долгие операции без UDP, например SSH).
    void setInactivityWatchdogEnabled(bool enabled);
    bool isConnected() const { return m_connected; }
    const QHostAddress& getPeerAddress() const { return m_peerAddress; }
    quint16 getPeerPort() const { return m_peerPort; }

    bool requestAllIndications(uint8_t tractNum);
    bool setFrequencyRx(uint8_t tractNum, uint32_t freqHz);
    bool setFrequencyTx(uint8_t tractNum, uint32_t freqHz);
    bool setPowerLevel(uint8_t tractNum, uint8_t levelCode);
    bool setTractControl(uint8_t tractNum, bool enable, bool awaitAck = false);
    bool setCurrentDirection(uint8_t tractNum, uint8_t dirId);
    bool setTractMode(uint8_t tractNum, uint8_t mode);

    qint64 getLastPacketTime() const { return m_lastPacketTime; }
    bool isAwaitingTractPowerAck() const { return m_tractPowerPending != TractPowerPending::None; }

signals:
    void connected(const QString &ip);
    void disconnected();
    void logMessage(const QString &msg);
    void errorOccurred(const QString &error);
    void statusUpdated(const QString &count);
    void freqRxIndicationReceived(uint8_t tractNum, uint32_t freqHz);
    void freqTxIndicationReceived(uint8_t tractNum, uint32_t freqHz);
    void rssiIndicationReceived(uint8_t tractNum, int16_t rssiDbm);
    /** Индикатор состояния канала/линка (IND_CHREADY, 0x860D). В пульте соответствует linkStatusIndicator. */
    void channelReadyIndicationReceived(uint8_t tractNum, uint8_t linkStatus);
    /** Индикация LINKSTATUS (IND_LINKSTATUS, 0x8612). В пульте: linkStatusIndicator = val & 0xFF. */
    void linkStatusIndicationReceived(uint8_t tractNum, uint16_t val);
    /** Уровень мощности тракта из IND_POWER_TRAKT: код 1..4. */
    void powerLevelIndicationReceived(uint8_t tractNum, uint8_t levelCode);
    /** Статус/ошибка ПП (индикация IND_ERROR): code==0 -> "Норма", code==1 -> "Нет связи с ПП" */
    void ppmStatusIndicationReceived(uint8_t tractNum, int16_t code);
    /** Текущий режим тракта (индикация IND_WORKMODE), отдельно от статуса передатчика в IND_ERROR */
    void workModeIndicationReceived(uint8_t tractNum, uint16_t mode);
    /** Текущее активное направление тракта: DirId из IND_ACTIVEDIR (0x8501). */
    void activeDirectionIndicationReceived(uint8_t tractNum, uint8_t dirId);
    /** Смена профиля на станции: IND_PROF_SE (0x8535), phase: 0=старт, 1=завершено. */
    void profileSwitchIndicationReceived(uint8_t profileId, uint8_t phase);
    void connectionLost();
    /** Отправлена команда, ожидаем IND_TRAKT_ON_SE / IND_TRAKT_OFF_SE с phase != 0 */
    void tractPowerAwaitingAck(uint8_t tractNum, bool enable);
    /** Подтверждение от станции: тракт включён (true) или выключен (false) */
    void tractPowerAcknowledged(uint8_t tractNum, bool isOn);
    void tractPowerAckTimeout(uint8_t tractNum, bool expectedOn);
    /** Фактическое событие IND_TRAKT_ON/OFF (phase!=0), включая внешние переключения. */
    void tractPowerIndicationReceived(uint8_t tractNum, bool isOn);

private slots:
    void processPendingDatagrams();
    void checkConnectionTimeout();
    void attemptReconnect();
    void onTractPowerAckTimeout();

private:
    bool initSocket();
    void parsePacket(const QByteArray &data, const QHostAddress &senderIp, quint16 senderPort);
    void parseSPS(const QByteArray &data, uint8_t tractNum, int offset = 0);
    void handleTractPowerIndication(const QByteArray &data, int payloadOffset, uint16_t desc);
    bool sendAck(const QByteArray &indicationPacket, int offset);
    void setDisconnectedState(const QString &reason);
    void clearTractPowerPending();

    void writeUint16BE(uint8_t *buf, uint16_t val);
    void writeUint32BE(uint8_t *buf, uint32_t val);
    uint16_t readUint16BE(const uint8_t *data);
    uint32_t readUint32BE(const uint8_t *data);

    DeviceConfig m_config;
    QUdpSocket *m_socket;
    bool m_connected;
    QHostAddress m_peerAddress;
    quint16 m_peerPort;

    QTimer *m_tractPowerAckTimer = nullptr;
    enum class TractPowerPending { None, On, Off };
    TractPowerPending m_tractPowerPending = TractPowerPending::None;
    uint8_t m_tractPowerPendingTract = 0;

    qint64 m_lastPacketTime;
    bool m_connectionLostReported;
    QTimer *m_connectionWatchdog;
    QTimer *m_reconnectTimer;
    bool m_autoReconnectEnabled;
    bool m_inactivityWatchdogEnabled = true;
    /// Тишина по UDP дольше этого интервала при m_connected — считаем обрыв связи со станцией.
    static constexpr qint64 STATION_INACTIVITY_TIMEOUT_MS = 12000;
    // Интервал повторной отправки MOD_START, пока станция не ответила STARTACK.
    // Срабатывает сразу после первой отправки в connectToDevice().
    static constexpr int RECONNECT_INTERVAL_MS = 5000;

    static constexpr int CTRL_MAX_CHANNELS = 4;
    struct ChannelInfo {
        uint16_t type = 0;
        uint16_t ln = 0;
        QHostAddress addr;
        quint16 port = 0;
        bool active = false;
    };
    ChannelInfo m_channels[CTRL_MAX_CHANNELS];

    QMutex m_mutex;
};

#endif // DEVICE_CONTROLLER_H
