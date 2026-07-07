#ifndef POWER_TRAFFIC_GENERATOR_H
#define POWER_TRAFFIC_GENERATOR_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QHostAddress>
#include "protocol_consts.h"

class PowerTrafficGenerator : public QObject {
    Q_OBJECT

public:
    explicit PowerTrafficGenerator(QObject *parent = nullptr);
    ~PowerTrafficGenerator();

    void setBindIp(const QString &ip);
    void setMulticastAddress(const QString &address);
    void setMulticastPort(quint16 port);
    void setSourcePort(quint16 port);
    void setIntervalMs(int intervalMs);
    void setDscp(uint8_t dscp);
    void setEcn(uint8_t ecn);
    void setPayloadType(uint8_t pt);
    void setTractNumber(uint8_t tract);

    bool start();
    void stop();
    bool isRunning() const { return m_running; }

signals:
    void started();
    void stopped();
    void errorOccurred(const QString &error);
    void logMessage(const QString &msg);

private slots:
    void onTrafficTimer();

private:
    void preparePacket();
    void configureSocket();
    bool bindToPort();

    QUdpSocket *m_socket;
    QTimer *m_timer;

    QHostAddress m_bindAddress;
    QHostAddress m_mcastAddress;
    quint16 m_mcastPort;
    quint16 m_sourcePort;
    uint8_t m_dscp;
    uint8_t m_ecn;
    uint8_t m_payloadType;
    uint8_t m_tractNumber;

    bool m_running;
    quint16 m_seqNumber;
    uint32_t m_timestamp;
    QByteArray m_packetBuffer;
    quint64 m_packetsSent;
    QString m_lastSocketErrorText;
    qint64 m_lastSocketErrorLogMs = 0;
};

#endif // POWER_TRAFFIC_GENERATOR_H
