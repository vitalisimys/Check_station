#ifndef DEVICE_CONTROLLER_H
#define DEVICE_CONTROLLER_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
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
    bool isConnected() const { return m_connected; }
    const QHostAddress& getPeerAddress() const { return m_peerAddress; }
    quint16 getPeerPort() const { return m_peerPort; }

    bool requestAllIndications(uint8_t tractNum);
    bool setFrequencyRx(uint8_t tractNum, uint32_t freqHz);
    bool setFrequencyTx(uint8_t tractNum, uint32_t freqHz);
    bool setTractControl(uint8_t tractNum, bool enable);
    bool setTractMode(uint8_t tractNum, uint8_t mode);

    qint64 getLastPacketTime() const { return m_lastPacketTime; }

signals:
    void connected(const QString &ip);
    void disconnected();
    void logMessage(const QString &msg);
    void errorOccurred(const QString &error);
    void statusUpdated(const QString &count);
    void connectionLost();

private slots:
    void processPendingDatagrams();
    void checkConnectionTimeout();
    void attemptReconnect();

private:
    bool initSocket();
    void parsePacket(const QByteArray &data, const QHostAddress &senderIp, quint16 senderPort);
    void parseSPS(const QByteArray &data, uint8_t tractNum, int offset = 0);
    bool sendAck(const QByteArray &indicationPacket, int offset);
    void setDisconnectedState(const QString &reason);

    void writeUint16BE(uint8_t *buf, uint16_t val);
    void writeUint32BE(uint8_t *buf, uint32_t val);
    uint16_t readUint16BE(const uint8_t *data);
    uint32_t readUint32BE(const uint8_t *data);

    DeviceConfig m_config;
    QUdpSocket *m_socket;
    bool m_connected;
    QHostAddress m_peerAddress;
    quint16 m_peerPort;

    qint64 m_lastPacketTime;
    bool m_connectionLostReported;
    QTimer *m_connectionWatchdog;
    QTimer *m_reconnectTimer;
    bool m_autoReconnectEnabled;
    static constexpr qint64 STATION_INACTIVITY_TIMEOUT_MS = 3000;
    static constexpr int RECONNECT_INTERVAL_MS = 1000;

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
