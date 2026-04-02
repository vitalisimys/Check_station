#include "analyzer_controller.h"

#include <QFileInfo>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <QDateTime>

namespace {
constexpr const char* DEFAULT_ANALYZER_PORT_NAME = "/dev/ttyACM0";
constexpr int DEFAULT_ANALYZER_BAUD = 115200;
constexpr int ANALYZER_RESPONSE_TIMEOUT_MS = 4000;

constexpr uint8_t PROTOCOL_START_BYTE = 0xBB;
constexpr uint8_t CMD_ECHO = 0xC0;
constexpr uint8_t CMD_STATUS = 0xFC;

QByteArray makePacket(uint8_t cmd, const QByteArray &payload)
{
    QByteArray packet;
    packet.append(static_cast<char>(PROTOCOL_START_BYTE));
    packet.append(static_cast<char>(cmd));
    const uint16_t len = static_cast<uint16_t>(payload.size());
    packet.append(static_cast<char>(len & 0xFF));          // LSB
    packet.append(static_cast<char>((len >> 8) & 0xFF));   // MSB
    packet.append(payload);
    packet.append(static_cast<char>(0x00));                // checksum LSB (не используется)
    packet.append(static_cast<char>(0x00));                // checksum MSB (не используется)
    return packet;
}
} // namespace

AnalyzerWorker::AnalyzerWorker(QObject *parent)
    : QObject(parent)
    , m_serial(new QSerialPort(this))
    , m_keepAliveTimer(new QTimer(this))
    , m_timeoutTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
{
    m_serial->setPortName(DEFAULT_ANALYZER_PORT_NAME);
    m_serial->setBaudRate(DEFAULT_ANALYZER_BAUD);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    m_keepAliveTimer->setInterval(2000);
    m_keepAliveTimer->setSingleShot(false);
    connect(m_keepAliveTimer, &QTimer::timeout, this, &AnalyzerWorker::sendEcho);

    m_timeoutTimer->setInterval(250);
    m_timeoutTimer->setSingleShot(false);
    connect(m_timeoutTimer, &QTimer::timeout, this, &AnalyzerWorker::checkTimeout);

    m_reconnectTimer->setInterval(500);
    m_reconnectTimer->setSingleShot(false);
    connect(m_reconnectTimer, &QTimer::timeout, this, &AnalyzerWorker::ensureConnected);

    connect(m_serial, &QSerialPort::readyRead, this, &AnalyzerWorker::onReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, &AnalyzerWorker::onErrorOccurred);
}

void AnalyzerWorker::connectPort()
{
    m_shouldBeConnected = true;
    ensureConnected();
    m_reconnectTimer->start();
}

void AnalyzerWorker::disconnectPort()
{
    m_keepAliveTimer->stop();
    m_timeoutTimer->stop();
    m_reconnectTimer->stop();
    m_shouldBeConnected = false;
    m_connected = false;
    m_waitingFirstEcho = false;
    m_lastResponse.invalidate();
    m_readBuffer.clear();

    if (m_serial->isOpen()) {
        m_serial->close();
    }

    emit disconnected("Отключено");
}

void AnalyzerWorker::ensureConnected()
{
    if (!m_shouldBeConnected) {
        return;
    }

    if (m_serial->isOpen()) {
        return;
    }

    // Если устройство вынули — /dev/ttyACM0 может отсутствовать.
    const QFileInfo fi(QString::fromUtf8(DEFAULT_ANALYZER_PORT_NAME));
    if (!fi.exists()) {
        return;
    }

    // Доп. проверка: порт должен быть виден в availablePorts (иногда файл уже есть, а стек ещё не поднялся)
    bool present = false;
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        if (info.systemLocation() == QString::fromUtf8(DEFAULT_ANALYZER_PORT_NAME)) {
            present = true;
            break;
        }
    }
    if (!present) {
        return;
    }

    if (!m_serial->open(QIODevice::ReadWrite)) {
        return;
    }

    m_connected = false;
    m_waitingFirstEcho = true;
    m_readBuffer.clear();
    m_lastResponse.start();

    m_keepAliveTimer->start();
    m_timeoutTimer->start();
    sendEcho(); // сразу после открытия проверяем связь
}

void AnalyzerWorker::sendEcho()
{
    if (!m_serial->isOpen()) {
        return;
    }
    const QByteArray packet = makePacket(CMD_ECHO, QByteArray());
    const qint64 written = m_serial->write(packet);
    if (written < 0) {
        emit disconnected("Ошибка записи в порт: " + m_serial->errorString());
        return;
    }
    m_serial->flush();
}

void AnalyzerWorker::checkTimeout()
{
    if (!m_serial->isOpen()) {
        return;
    }

    if (!m_lastResponse.isValid()) {
        return;
    }

    if (m_lastResponse.elapsed() > ANALYZER_RESPONSE_TIMEOUT_MS) {
        if (m_connected || m_waitingFirstEcho) {
            const QString reason = QString("Потеряна связь (таймаут ответа > %1мс)")
                                       .arg(ANALYZER_RESPONSE_TIMEOUT_MS);
            handleTransportDrop(reason);
        }
    }
}

void AnalyzerWorker::onReadyRead()
{
    m_readBuffer.append(m_serial->readAll());

    while (m_readBuffer.size() >= 6) {
        const int startIdx = m_readBuffer.indexOf(static_cast<char>(PROTOCOL_START_BYTE));
        if (startIdx == -1) {
            m_readBuffer.clear();
            return;
        }
        if (startIdx > 0) {
            m_readBuffer.remove(0, startIdx);
            continue;
        }

        const uint16_t payloadLen =
            (static_cast<uint8_t>(m_readBuffer[3]) << 8) | static_cast<uint8_t>(m_readBuffer[2]);
        const int totalSize = 4 + payloadLen + 2;
        if (m_readBuffer.size() < totalSize) {
            break;
        }

        const QByteArray packet = m_readBuffer.left(totalSize);
        m_readBuffer.remove(0, totalSize);

        if (packet.size() < 6) {
            continue;
        }

        const uint8_t cmd = static_cast<uint8_t>(packet[1]);
        if (cmd == CMD_STATUS) {
            continue;
        }

        // Любой валидный пакет считаем "ответом", но для "подключено" ждём именно ECHO.
        if (cmd == CMD_ECHO) {
            m_lastResponse.restart();
            if (!m_connected) {
                m_connected = true;
                m_waitingFirstEcho = false;
                emit connected();
            }
        } else {
            // Если устройство отвечает чем-то другим — тоже поддерживаем таймер живости.
            if (m_lastResponse.isValid()) {
                m_lastResponse.restart();
            } else {
                m_lastResponse.start();
            }
        }
    }
}

void AnalyzerWorker::onErrorOccurred(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) {
        return;
    }
    handleTransportDrop("Serial error: " + m_serial->errorString());
}

void AnalyzerWorker::handleTransportDrop(const QString &reason)
{
    m_connected = false;
    m_waitingFirstEcho = false;
    m_lastResponse.invalidate();
    m_readBuffer.clear();

    m_keepAliveTimer->stop();
    m_timeoutTimer->stop();

    if (m_serial->isOpen()) {
        m_serial->close();
    }

    emit disconnected(reason);

    // Если пользователь хотел быть подключенным — пытаемся переоткрыть порт, когда он появится снова.
    if (m_shouldBeConnected && !m_reconnectTimer->isActive()) {
        m_reconnectTimer->start();
    }
}

AnalyzerController::AnalyzerController(QObject *parent)
    : QObject(parent)
{
    m_worker = new AnalyzerWorker();
    m_worker->moveToThread(&m_thread);

    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(this, &AnalyzerController::destroyed, &m_thread, &QThread::quit);

    connect(m_worker, &AnalyzerWorker::connected, this, &AnalyzerController::analyzerConnected);
    connect(m_worker, &AnalyzerWorker::disconnected, this, &AnalyzerController::analyzerDisconnected);
    connect(m_worker, &AnalyzerWorker::logMessage, this, &AnalyzerController::logMessage);

    m_thread.start();
}

AnalyzerController::~AnalyzerController()
{
    disconnectFromPort();
    m_thread.quit();
    m_thread.wait(1000);
}

void AnalyzerController::connectToDefaultPort()
{
    QMetaObject::invokeMethod(m_worker, "connectPort", Qt::QueuedConnection);
}

void AnalyzerController::disconnectFromPort()
{
    if (!m_worker) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "disconnectPort", Qt::QueuedConnection);
}
