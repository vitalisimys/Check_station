#include "tftpserver.h"
#include "firmwarefiles.h"

#include <QDataStream>

namespace {
constexpr int kTftpBlockSize = 1468;
constexpr quint16 kTftpServerPort = 69;
} // namespace

TftpServer::TftpServer(QObject *parent) : QObject(parent)
{
    udpSocket = new QUdpSocket(this);
}

bool TftpServer::startServer()
{
    if (isRunning) {
        return true;
    }

    if (!udpSocket->bind(QHostAddress::Any, kTftpServerPort)) {
        emit logMessage(QString("Не удалось запустить tftp сервер: %1\n"
                                "Очистить порт: \"sudo lsof -i :69\" \"sudo kill -9 <PID>\"\n"
                                "Остановить системный tftp-сервер: \"sudo systemctl stop tftpd-hpa\"")
                            .arg(udpSocket->errorString()),
                        "red");
        return false;
    }

    resetTransferState();
    connect(udpSocket, &QUdpSocket::readyRead, this, &TftpServer::processPendingDatagrams);
    emit logMessage(QStringLiteral("TFTP-сервер запущен. Ожидание загрузки файлов..."), "green");
    isRunning = true;
    return true;
}

void TftpServer::stopServer()
{
    if (!isRunning && udpSocket->state() == QAbstractSocket::UnconnectedState) {
        // Уже остановлен — повторный вызов безопасен и тих.
        return;
    }
    isRunning = false;
    // Точечно отписываем readyRead, чтобы не задеть других возможных подписчиков.
    disconnect(udpSocket, &QUdpSocket::readyRead, this, &TftpServer::processPendingDatagrams);
    udpSocket->close();
    if (currentFile.isOpen()) {
        currentFile.close();
    }
    resetTransferState();
    emit logMessage("Сервер tftp остановлен", "green");
}

void TftpServer::resetTransferState()
{
    currentBlockNumber = 0;
    lastSentBlock = 0;
    lastSentData.clear();
    filename.clear();
    finishedFiles.clear();
    requiredFiles.clear();
    needFlashUboot = false;
    totalBlocks = 0;
}

void TftpServer::processPendingDatagrams()
{
    while (udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(udpSocket->pendingDatagramSize());
        QHostAddress sender;
        quint16 senderPort = 0;

        const qint64 received = udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        if (received < 2) {
            // Слишком короткая дейтаграмма — не содержит даже opcode.
            continue;
        }

        const quint16 opcode = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(datagram.constData()));
        switch (opcode) {
        case Opcode_RRQ:
        {
#ifdef QDEBUG
            qDebug() << "Получен RRQ запрос";
#endif
            if (currentFile.isOpen()) {
                currentFile.close();
            }
            currentBlockNumber = 0;
            lastSentBlock = 0;
            lastSentData.clear();
            filename.clear();

            int filenameOffset = 2;
            filename = QString::fromUtf8(readString(datagram, filenameOffset));

            if (filename == QStringLiteral("u-boot-spi-spl.bin")) {
                needFlashUboot = true;
            }
            requiredFiles.insert(filename);

            filenameOffset += filename.toUtf8().size() + 1;
            const QString mode = QString::fromUtf8(readString(datagram, filenameOffset));

#ifdef QDEBUG
            qDebug() << QString("Файл: %1, Режим: %2").arg(filename).arg(mode);
#endif

            if (mode != QStringLiteral("octet")) {
                emit logMessage(QString("Неподдерживаемый режим передачи: %1").arg(mode), "red");
                break;
            }

            QDir dir(FirmwareFiles::directory());
            const QString resolvedPath = FirmwareFiles::resolveTftpRequestPath(dir, filename);
            if (resolvedPath.isEmpty()) {
                const QStringList availableFiles = dir.entryList(QDir::Files);
                emit logMessage(QString("Файл обновления не найден: %1\nКаталог: %2\nДоступные файлы: %3")
                                    .arg(filename)
                                    .arg(dir.absolutePath())
                                    .arg(availableFiles.isEmpty() ? QStringLiteral("—")
                                                                  : availableFiles.join(QStringLiteral(", "))),
                                "red");
                break;
            }

            currentFile.setFileName(resolvedPath);
            if (!currentFile.open(QIODevice::ReadOnly)) {
                emit logMessage(QString("Не удалось открыть файл обновления: %1").arg(currentFile.errorString()), "red");
                break;
            }

            // Отправляем OACK пакет с timeout и blksize
            QByteArray oackPacket;
            QDataStream oackStream(&oackPacket, QIODevice::WriteOnly);
            oackStream.setByteOrder(QDataStream::BigEndian);
            oackStream << static_cast<quint16>(Opcode_OACK);

            oackPacket.append("timeout");
            oackPacket.append('\0');
            oackPacket.append("13");
            oackPacket.append('\0');

            oackPacket.append("blksize");
            oackPacket.append('\0');
            oackPacket.append(QByteArray::number(kTftpBlockSize));
            oackPacket.append('\0');

            udpSocket->writeDatagram(oackPacket, sender, senderPort);

#ifdef QDEBUG
            qDebug() << "Отправлен OACK пакет с timeout и blksize";
#endif

            const qint64 totalSize = currentFile.size();
            totalBlocks = static_cast<int>((totalSize + kTftpBlockSize - 1) / kTftpBlockSize);
            if (totalBlocks <= 0) {
                totalBlocks = 1;
            }
#ifdef QDEBUG
            qDebug() << QString("totalBlocks, размер: %1").arg(totalBlocks);
#endif
            break;
        }
        case Opcode_ACK:
        {
            if (!currentFile.isOpen()) {
                break;
            }
            if (received < 4) {
                break;
            }

            const quint16 ackBlockNumber = qFromBigEndian<quint16>(
                reinterpret_cast<const uchar *>(datagram.constData()) + 2);

            // Если получили ACK для уже отправленного блока — повторяем последний.
            if (ackBlockNumber < currentBlockNumber) {
#ifdef QDEBUG
                qDebug() << QString("Получен дублирующий ACK для блока: %1, повторная отправка блока %2")
                                .arg(ackBlockNumber)
                                .arg(lastSentBlock);
#endif
                if (!lastSentData.isEmpty()) {
                    udpSocket->writeDatagram(lastSentData, sender, senderPort);
                }
                break;
            }

            const QByteArray dataBlock = currentFile.read(kTftpBlockSize);
            if (dataBlock.isEmpty()) {
                // Передача файла завершена. Если последний блок был «ровно полным»,
                // по RFC 1350 нужно отправить пустой DATA-пакет, чтобы клиент понял конец.
                const bool needTerminatingPacket = (currentFile.size() % kTftpBlockSize) == 0;
                if (needTerminatingPacket) {
                    QByteArray emptyDataPacket;
                    QDataStream emptyStream(&emptyDataPacket, QIODevice::WriteOnly);
                    emptyStream.setByteOrder(QDataStream::BigEndian);
                    emptyStream << static_cast<quint16>(Opcode_DATA)
                                << static_cast<quint16>(currentBlockNumber + 1);
                    udpSocket->writeDatagram(emptyDataPacket, sender, senderPort);
                }

                emit logMessage(QString("Файл %1 успешно передан").arg(filename), "green");
                currentFile.close();
                currentBlockNumber = 0;

                finishedFiles.insert(filename);
                const int expected = needFlashUboot ? 4 : 3;
                if (finishedFiles.size() >= expected) {
                    stopServer();
                    emit transmitFinish();
                }
                break;
            }

            currentBlockNumber++;
            QByteArray dataPacket;
            QDataStream dataStream(&dataPacket, QIODevice::WriteOnly);
            dataStream.setByteOrder(QDataStream::BigEndian);
            dataStream << static_cast<quint16>(Opcode_DATA) << static_cast<quint16>(currentBlockNumber);
            dataPacket.append(dataBlock);

            udpSocket->writeDatagram(dataPacket, sender, senderPort);

            lastSentData = dataPacket;
            lastSentBlock = currentBlockNumber;

            const int progressValue = (totalBlocks > 0)
                                          ? (currentBlockNumber * 100) / totalBlocks
                                          : 0;
            emit progressChanged(progressValue);
            break;
        }
        case Opcode_ERROR:
        {
            if (received < 4) {
                break;
            }
            const quint16 errorCode = qFromBigEndian<quint16>(
                reinterpret_cast<const uchar *>(datagram.constData()) + 2);
            const QString errorMessage = QString::fromUtf8(readString(datagram, 4));
            emit logMessage(QString("Получен ERROR пакет, Код: %1, Сообщение: %2").arg(errorCode).arg(errorMessage), "red");
            break;
        }
        default:
            emit logMessage(QString("Неизвестная операция: %1").arg(opcode), "red");
            break;
        }

        if (!isRunning) {
            return;
        }
    }
}

QByteArray TftpServer::readString(const QByteArray &datagram, int offset) const
{
    int end = offset;
    while (end < datagram.size() && datagram[end] != '\0') {
        ++end;
    }
    return datagram.mid(offset, end - offset);
}
