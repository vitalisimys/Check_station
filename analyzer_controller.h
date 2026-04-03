#ifndef ANALYZER_CONTROLLER_H
#define ANALYZER_CONTROLLER_H

#include <QObject>
#include <QSerialPort>
#include <QThread>
#include <QElapsedTimer>
#include <QVector>
#include <QByteArray>
#include <QMetaType>

class QTimer;

Q_DECLARE_METATYPE(QVector<double>)

class AnalyzerWorker final : public QObject
{
    Q_OBJECT
public:
    explicit AnalyzerWorker(QObject *parent = nullptr);

public slots:
    void connectPort();
    void disconnectPort();
    void startSpectrumStream();
    void stopSpectrumStream();

private slots:
    void ensureConnected();
    void sendEcho();
    void checkTimeout();
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);

signals:
    void connected();
    void disconnected(const QString &reason);
    void logMessage(const QString &msg);
    void spectrumDataReceived(const QVector<double> &freqs, const QVector<double> &amps);

private:
    void handleTransportDrop(const QString &reason);
    QByteArray uint64ToBytesLE(quint64 value) const;
    QByteArray prepareSpectrumCommand(uint8_t rf_in,
                                       uint8_t bw,
                                       uint8_t speed,
                                       quint64 start,
                                       quint64 stop) const;
    void sendSpectrumRequest();

    QSerialPort *m_serial = nullptr;
    QTimer *m_keepAliveTimer = nullptr;
    QTimer *m_timeoutTimer = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QByteArray m_readBuffer;
    QElapsedTimer m_lastResponse;
    bool m_connected = false;
    bool m_waitingFirstEcho = false;
    bool m_shouldBeConnected = false;

    // Реалтайм-стрим спектра (QCustomPlot на вкладке tabHands)
    bool m_spectrumStreaming = false;
    bool m_waitingSpectrumResponse = false;
    uint8_t m_streamRfIn = 0;
    uint8_t m_streamBw = 0;
    uint8_t m_streamSpeed = 0;
    quint64 m_streamStart = 249000000ULL;
    quint64 m_streamStop = 251000000ULL;
};

class AnalyzerController final : public QObject
{
    Q_OBJECT
public:
    explicit AnalyzerController(QObject *parent = nullptr);
    ~AnalyzerController() override;

    void connectToDefaultPort();
    void disconnectFromPort();
    void startSpectrumStream();
    void stopSpectrumStream();
    bool isConnected() const { return m_connected; }

signals:
    void analyzerConnected();
    void analyzerDisconnected(const QString &reason);
    void logMessage(const QString &msg);
    void spectrumDataReceived(const QVector<double> &freqs, const QVector<double> &amps);

private:
    QThread m_thread;
    AnalyzerWorker *m_worker = nullptr;
    bool m_connected = false;
};

#endif // ANALYZER_CONTROLLER_H

