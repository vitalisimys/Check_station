#ifndef ANALYZER_CONTROLLER_H
#define ANALYZER_CONTROLLER_H

#include <QObject>
#include <QSerialPort>
#include <QThread>
#include <QElapsedTimer>

class QTimer;

class AnalyzerWorker final : public QObject
{
    Q_OBJECT
public:
    explicit AnalyzerWorker(QObject *parent = nullptr);

public slots:
    void connectPort();
    void disconnectPort();

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

private:
    void handleTransportDrop(const QString &reason);

    QSerialPort *m_serial = nullptr;
    QTimer *m_keepAliveTimer = nullptr;
    QTimer *m_timeoutTimer = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QByteArray m_readBuffer;
    QElapsedTimer m_lastResponse;
    bool m_connected = false;
    bool m_waitingFirstEcho = false;
    bool m_shouldBeConnected = false;
};

class AnalyzerController final : public QObject
{
    Q_OBJECT
public:
    explicit AnalyzerController(QObject *parent = nullptr);
    ~AnalyzerController() override;

    void connectToDefaultPort();
    void disconnectFromPort();

signals:
    void analyzerConnected();
    void analyzerDisconnected(const QString &reason);
    void logMessage(const QString &msg);

private:
    QThread m_thread;
    AnalyzerWorker *m_worker = nullptr;
};

#endif // ANALYZER_CONTROLLER_H

