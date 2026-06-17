#include "analyzer_controller.h"

#include <QFileInfo>
#include <QSerialPortInfo>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr const char* DEFAULT_ANALYZER_PORT_NAME = "/dev/ttyACM0";
constexpr int DEFAULT_ANALYZER_BAUD = 115200;
constexpr int ANALYZER_RESPONSE_TIMEOUT_MS = 4000;
constexpr int ANALYZER_KEEP_ALIVE_INTERVAL_MS_DEFAULT = 2000;
constexpr int ANALYZER_KEEP_ALIVE_INTERVAL_MS_MIN = 50;

constexpr uint8_t PROTOCOL_START_BYTE = 0xBB;
constexpr uint8_t CMD_ECHO = 0xC0;
constexpr uint8_t CMD_GET_SPECTRUM_FLOAT = 0xC2;
constexpr uint8_t CMD_GENERATOR = 0xC3;
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

    m_keepAliveTimer->setInterval(ANALYZER_KEEP_ALIVE_INTERVAL_MS_DEFAULT);
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

    m_spectrumStreaming = false;
    m_waitingSpectrumResponse = false;

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
    if (m_spectrumStreaming) {
        return; // Во время стрима отключаем periodic echo, чтобы не забивать порт.
    }
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

QByteArray AnalyzerWorker::uint64ToBytesLE(quint64 value) const
{
    QByteArray res(8, 0);
    for (int i = 0; i < 8; ++i) {
        res[i] = static_cast<char>((value >> (i * 8)) & 0xFF);
    }
    return res;
}

QByteArray AnalyzerWorker::prepareSpectrumCommand(uint8_t rf_in,
                                                   uint8_t bw,
                                                   uint8_t speed,
                                                   quint64 start,
                                                   quint64 stop) const
{
    // Payload (как в Station_alalyzer):
    // start(8 LE) + stop(8 LE) + rf_in(1) + bw(1) + speed(1)
    QByteArray payload;
    payload.append(uint64ToBytesLE(start));
    payload.append(uint64ToBytesLE(stop));
    payload.append(static_cast<char>(rf_in));
    payload.append(static_cast<char>(bw));
    payload.append(static_cast<char>(speed));
    return payload;
}

void AnalyzerWorker::sendSpectrumRequest()
{
    if (!m_serial->isOpen() || !m_spectrumStreaming) {
        return;
    }
    if (m_waitingSpectrumResponse) {
        return; // Держим строго 1 outstanding запрос спектра.
    }

    quint64 reqStart = m_streamStart;
    quint64 reqStop = m_streamStop;
    if (m_altRangesEnabled) {
        if (m_altNextIsWide) {
            reqStart = m_altWideStart;
            reqStop = m_altWideStop;
        } else {
            reqStart = m_altNarrowStart;
            reqStop = m_altNarrowStop;
        }
        m_altNextIsWide = !m_altNextIsWide;
    }
    m_lastRequestStart = reqStart;
    m_lastRequestStop = reqStop;

    const QByteArray payload = prepareSpectrumCommand(m_streamRfIn,
                                                     m_streamBw,
                                                     m_streamSpeed,
                                                     reqStart,
                                                     reqStop);
    const QByteArray packet = makePacket(CMD_GET_SPECTRUM_FLOAT, payload);
    const qint64 written = m_serial->write(packet);
    if (written < 0) {
        emit logMessage(QStringLiteral("Ошибка записи CMD_GET_SPECTRUM_FLOAT: %1")
                            .arg(m_serial->errorString()));
        m_spectrumStreaming = false;
        m_waitingSpectrumResponse = false;
        m_keepAliveTimer->start();
        sendEcho();
        return;
    }
    m_serial->flush();
    m_waitingSpectrumResponse = true;
}

void AnalyzerWorker::startSpectrumStream()
{
    if (m_spectrumStreaming) {
        return;
    }
    if (!m_serial->isOpen()) {
        return;
    }

    m_spectrumStreaming = true;
    // Не сбрасываем m_waitingSpectrumResponse: после stop() запрос мог остаться in-flight.
    // Если pending — ждём его ответа (он будет помечен как stale и пропущен), и тогда отправим следующий.

    // Отключаем echo на время работы спектра.
    m_keepAliveTimer->stop();

    emit logMessage(QStringLiteral(">>> Поток спектра: START"));
    if (!m_waitingSpectrumResponse) {
        sendSpectrumRequest();
    }
}

void AnalyzerWorker::stopSpectrumStream()
{
    if (!m_spectrumStreaming) {
        return;
    }

    m_spectrumStreaming = false;
    // Намеренно НЕ сбрасываем m_waitingSpectrumResponse: in-flight запрос ещё может вернуть ответ,
    // и при последующем restart нам нужно понимать, что есть «старый» кадр для пропуска.

    emit logMessage(QStringLiteral(">>> Поток спектра: STOP"));

    if (m_serial->isOpen()) {
        m_keepAliveTimer->start();
        sendEcho(); // быстро возвращаем keep-alive
    }
}

void AnalyzerWorker::setGenerator(quint64 freqHz, quint8 state, quint8 pow)
{
    if (!m_serial->isOpen()) {
        return;
    }
    // Payload (по ТЗ):
    // uint64(freq_Hz) + uint8(state) + uint8(pow)
    // В спектральных командах uint64 используется LE — здесь делаем так же.
    QByteArray payload;
    payload.append(uint64ToBytesLE(freqHz));
    payload.append(static_cast<char>(state));
    payload.append(static_cast<char>(pow));
    const QByteArray packet = makePacket(CMD_GENERATOR, payload);
    const qint64 written = m_serial->write(packet);
    if (written < 0) {
        emit logMessage(QStringLiteral("Ошибка записи CMD_GENERATOR: %1")
                            .arg(m_serial->errorString()));
        return;
    }
    m_serial->flush();
    emit logMessage(QStringLiteral(">>> GEN 0xC3: f=%1 Hz state=%2 pow=%3 (written=%4)")
                        .arg(QString::number(freqHz),
                             QString::number(state),
                             QString::number(pow),
                             QString::number(written)));
}

void AnalyzerWorker::setSpectrumBandwidth(int bwIndex)
{
    const int v = qBound(0, bwIndex, 3);
    const uint8_t bw = static_cast<uint8_t>(v);
    const bool bwChanged = (m_streamBw != bw);
    m_streamBw = bw;
    // Помечаем кадр как stale, если есть pending-ответ от запроса со старым BW.
    // Это работает и когда стрим временно остановлен (m_waitingSpectrumResponse сохраняется через stopSpectrumStream).
    if (bwChanged && m_waitingSpectrumResponse) {
        ++m_spectrumStaleFramesToDrop;
        if (m_spectrumStaleFramesToDrop > 8) {
            m_spectrumStaleFramesToDrop = 8;
        }
    }
}

void AnalyzerWorker::setSpectrumRange(quint64 startHz, quint64 stopHz)
{
    constexpr quint64 kMinHz = 1000ULL;
    constexpr quint64 kMaxHz = 10000000000ULL; // 10 ГГц — защита от мусора
    if (startHz > stopHz) {
        std::swap(startHz, stopHz);
    }
    startHz = qBound(kMinHz, startHz, kMaxHz);
    stopHz = qBound(kMinHz, stopHz, kMaxHz);
    if (startHz >= stopHz) {
        if (stopHz > kMinHz + 1000) {
            startHz = stopHz - 1000;
        } else {
            return;
        }
    }
    const bool rangeChanged = (m_streamStart != startHz || m_streamStop != stopHz);
    m_streamStart = startHz;
    m_streamStop = stopHz;
    // По умолчанию fallback-диапазон тоже обновляем.
    m_lastRequestStart = startHz;
    m_lastRequestStop = stopHz;
    // Если есть pending-ответ от запроса со старым диапазоном, помечаем его как stale.
    // Это спасает сценарий tab-switch: stopSpectrumStream → setSpectrumRange → startSpectrumStream,
    // когда ответ от Q1 (старый диапазон) приходит уже после restart.
    if (rangeChanged && m_waitingSpectrumResponse) {
        ++m_spectrumStaleFramesToDrop;
        if (m_spectrumStaleFramesToDrop > 8) {
            m_spectrumStaleFramesToDrop = 8;
        }
    }
}

void AnalyzerWorker::setAlternateSpectrumRangesEnabled(bool enabled)
{
    m_altRangesEnabled = enabled;
    m_altNextIsWide = false; // начинаем с narrow, чтобы moment-график обновлялся сразу
    // При включении/выключении не считаем кадры "устаревшими": диапазоны переключаются намеренно.
    if (enabled) {
        m_spectrumStaleFramesToDrop = 0;
    }
}

void AnalyzerWorker::setKeepAliveIntervalMs(int intervalMs)
{
    const int ms =
        qBound(ANALYZER_KEEP_ALIVE_INTERVAL_MS_MIN, intervalMs, ANALYZER_RESPONSE_TIMEOUT_MS / 2);
    if (m_keepAliveTimer->interval() == ms) {
        return;
    }
    m_keepAliveTimer->setInterval(ms);
    if (m_keepAliveTimer->isActive() && m_serial->isOpen() && !m_spectrumStreaming) {
        m_keepAliveTimer->start();
        sendEcho();
    }
}

void AnalyzerWorker::setAlternateSpectrumRanges(quint64 narrowStartHz,
                                                quint64 narrowStopHz,
                                                quint64 wideStartHz,
                                                quint64 wideStopHz)
{
    m_altNarrowStart = narrowStartHz;
    m_altNarrowStop = narrowStopHz;
    m_altWideStart = wideStartHz;
    m_altWideStop = wideStopHz;
    // Не трогаем staleFrames: ranges могут обновляться часто (по шагам power-теста).
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

        if (cmd == CMD_GET_SPECTRUM_FLOAT) {
            const QByteArray payload = packet.mid(4, payloadLen);
            if (payloadLen % 8 != 0) {
                emit logMessage(QStringLiteral("Err: Bad Spectrum len (%1)").arg(payloadLen));
                m_spectrumStreaming = false;
                m_waitingSpectrumResponse = false;
                m_keepAliveTimer->start();
                sendEcho();
                continue;
            }

            const int pointCount = payloadLen / 8;
            QVector<double> freqs(pointCount);
            QVector<double> amps(pointCount);

            const char *dataPtr = payload.constData();
            for (int i = 0; i < pointCount; ++i) {
                float amp = 0.0f;
                float freq = 0.0f;
                std::memcpy(&amp, dataPtr + (i * 4), sizeof(float));
                std::memcpy(&freq, dataPtr + (pointCount * 4) + (i * 4), sizeof(float));

                if (std::isfinite(amp) && std::isfinite(freq)) {
                    amps[i] = static_cast<double>(amp);
                    freqs[i] = static_cast<double>(freq);
                } else {
                    amps[i] = -200.0;
                    const double stepHz = (double)(m_lastRequestStop - m_lastRequestStart) / pointCount;
                    freqs[i] = (m_lastRequestStart / 1e6) + (i * stepHz / 1e6);
                }
            }

            // Любой пакет обновляет таймаут связи
            m_lastResponse.restart();

            m_waitingSpectrumResponse = false;
            if (m_spectrumStreaming) {
                if (m_spectrumStaleFramesToDrop > 0) {
                    --m_spectrumStaleFramesToDrop;
                } else {
                    emit spectrumDataReceived(freqs, amps);
                }
                sendSpectrumRequest(); // следующий кадр сразу после обработки ответа
            } else if (m_spectrumStaleFramesToDrop > 0) {
                // Стрим уже остановлен, но это всё-таки in-flight ответ от Q со старого периода.
                // Декрементируем stale-счётчик, чтобы при последующем restart не пропустить «свежий» кадр.
                --m_spectrumStaleFramesToDrop;
            }
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

    // Любой обрыв транспорта (USB, таймаут) отменяет in-flight запрос спектра.
    // Иначе после reconnect m_waitingSpectrumResponse остаётся true (его намеренно
    // не сбрасывает stopSpectrumStream для tab-switch), и startSpectrumStream()
    // не вызывает sendSpectrumRequest() — порт молчит, echo выключен на время
    // «стрима», через 4 с снова таймаут (цикл до перезапуска приложения).
    m_spectrumStreaming = false;
    m_waitingSpectrumResponse = false;
    m_spectrumStaleFramesToDrop = 0;

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

    connect(m_worker, &AnalyzerWorker::connected, this, &AnalyzerController::analyzerConnected);
    connect(m_worker, &AnalyzerWorker::disconnected, this, &AnalyzerController::analyzerDisconnected);
    connect(m_worker, &AnalyzerWorker::connected, this, [this]() { m_connected = true; });
    connect(m_worker, &AnalyzerWorker::disconnected, this, [this](const QString &) { m_connected = false; });
    connect(m_worker, &AnalyzerWorker::logMessage, this, &AnalyzerController::logMessage);
    connect(m_worker, &AnalyzerWorker::spectrumDataReceived, this, &AnalyzerController::spectrumDataReceived);

    m_thread.start();
}

AnalyzerController::~AnalyzerController()
{
    disconnectFromPort();
    m_thread.quit();
    m_thread.wait();
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

void AnalyzerController::startSpectrumStream()
{
    if (!m_worker) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "startSpectrumStream", Qt::QueuedConnection);
}

void AnalyzerController::stopSpectrumStream()
{
    if (!m_worker) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "stopSpectrumStream", Qt::QueuedConnection);
}

void AnalyzerController::setGenerator(quint64 freqHz, quint8 state, quint8 pow)
{
    if (!m_worker) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "setGenerator", Qt::QueuedConnection,
                              Q_ARG(quint64, freqHz),
                              Q_ARG(quint8, state),
                              Q_ARG(quint8, pow));
}

void AnalyzerController::setSpectrumBandwidth(int bwIndex)
{
    if (!m_worker) {
        return;
    }
    // Синхронно на потоке воркера: чтобы следующий запрос спектра ушёл уже с новым BW.
    QMetaObject::invokeMethod(m_worker, "setSpectrumBandwidth", Qt::BlockingQueuedConnection,
                              Q_ARG(int, bwIndex));
}

void AnalyzerController::setSpectrumRange(quint64 startHz, quint64 stopHz)
{
    if (!m_worker) {
        return;
    }
    // Синхронно на потоке воркера: иначе следующий sendSpectrumRequest может уйти
    // со старым диапазоном, а в UI попадёт кадр от предыдущего sweep.
    QMetaObject::invokeMethod(m_worker, "setSpectrumRange", Qt::BlockingQueuedConnection,
                              Q_ARG(quint64, startHz), Q_ARG(quint64, stopHz));
}

void AnalyzerController::setAlternateSpectrumRangesEnabled(bool enabled)
{
    if (!m_worker) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "setAlternateSpectrumRangesEnabled", Qt::BlockingQueuedConnection,
                              Q_ARG(bool, enabled));
}

void AnalyzerController::setAlternateSpectrumRanges(quint64 narrowStartHz,
                                                    quint64 narrowStopHz,
                                                    quint64 wideStartHz,
                                                    quint64 wideStopHz)
{
    if (!m_worker) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "setAlternateSpectrumRanges", Qt::BlockingQueuedConnection,
                              Q_ARG(quint64, narrowStartHz),
                              Q_ARG(quint64, narrowStopHz),
                              Q_ARG(quint64, wideStartHz),
                              Q_ARG(quint64, wideStopHz));
}

void AnalyzerController::setKeepAliveIntervalMs(int intervalMs)
{
    if (!m_worker) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "setKeepAliveIntervalMs", Qt::BlockingQueuedConnection,
                              Q_ARG(int, intervalMs));
}
