#include "tftpserver.h"
#include "firmwarefiles.h"

TftpServer::TftpServer(QObject *parent) : QObject(parent)
{
    udpSocket = new QUdpSocket(this);
    currentBlockNumber = 0;
    isRunning = false;
}

bool TftpServer::startServer()
{
    if (!udpSocket->bind(QHostAddress::Any, 69)) {
        emit logMessage(QString("Не удалось запустить tftp сервер: %1\n"
                                "Очистить порт: \"sudo lsof -i :69\" \"sudo kill -9 <PID>\"\n"
                                "Остановить системный tftp-сервер: \"sudo systemctl stop tftpd-hpa\"")
                            .arg(udpSocket->errorString()),
                        "red");
        return false;
    } else {
        connect(udpSocket, &QUdpSocket::readyRead, this, &TftpServer::processPendingDatagrams);
        emit logMessage("Сервер tftp запущен", "blue");
        isRunning = true;
        return true;
    }
}

void TftpServer::stopServer()
{
    isRunning = false;
    udpSocket->disconnect();
    udpSocket->close();
    if (currentFile.isOpen()) {
        currentFile.close();
    }
    countTransmitFiles = 0;
    emit logMessage("Сервер tftp остановлен", "blue");
}

void TftpServer::processPendingDatagrams()
{
    while (udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(udpSocket->pendingDatagramSize());
        QHostAddress sender;
        quint16 senderPort;

        udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

        quint16 opcode = qFromBigEndian<quint16>(datagram.constData());
        switch (opcode) {
        case Opcode_RRQ:
        {
#ifdef QDEBUG
            qDebug() << "Получен RRQ запрос";
#endif
            // Очистка состояния предыдущей передачи
            if (currentFile.isOpen()) {
                currentFile.close();
            }
            currentBlockNumber = 0;
            lastSentBlock = 0;
            lastSentData.clear();
            filename.clear();

            int filenameOffset = 2;
            filename = readString(datagram, filenameOffset);

            if (filename == "u-boot-spi-spl.bin") {
                needFlashUboot =true;
            }

            filenameOffset += filename.size() + 1;
            QString mode = readString(datagram, filenameOffset);
            emit startTransmitFile(filename);

#ifdef QDEBUG
            qDebug() << QString("Файл: %1, Режим: %2").arg(filename).arg(mode);
#endif

            if (mode != "octet") {
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

            if (currentFile.isOpen()) {
                currentFile.close();
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

            // Добавляем timeout
            oackPacket.append("timeout");
            oackPacket.append('\0');
            oackPacket.append("13");
            oackPacket.append('\0');

            // Добавляем blksize
            oackPacket.append("blksize");
            oackPacket.append('\0');
            oackPacket.append("1468");
            oackPacket.append('\0');

            udpSocket->writeDatagram(oackPacket, sender, senderPort);

#ifdef QDEBUG
            qDebug() << "Отправлен OACK пакет с timeout и blksize";
#endif

            // Рассчитываем totalBlocks
            qint64 totalSize = currentFile.size();
            totalBlocks = (totalSize + 1467) / 1468;
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

            quint16 ackBlockNumber = qFromBigEndian<quint16>(datagram.constData() + 2);

            // double progressPercent = (static_cast<double>(ackBlockNumber) / totalBlocks) * 100;
            // if (progressPercent <= 0.1 || progressPercent >= 99.9) {
            //     qDebug() << QString("Получен ACK пакет");
            //     qDebug() << QString("Подтверждён блок: %1, Прогресс: %2%")
            //                     .arg(ackBlockNumber)
            //                     .arg(progressPercent, 0, 'f', 2);
            // }

            // Если получили ACK для уже отправленного блока, повторно отправляем последний блок
            if (ackBlockNumber < currentBlockNumber) {
#ifdef QDEBUG
                qDebug() << QString("Получен дублирующий ACK для блока: %1, повторная отправка блока %2")
                                .arg(ackBlockNumber)
                                .arg(lastSentBlock);
#endif
                udpSocket->writeDatagram(lastSentData, sender, senderPort);
                break;
            }

            QByteArray dataBlock = currentFile.read(1468);
            if (dataBlock.isEmpty()) {
                emit logMessage(QString("Файл %1 успешно передан").arg(filename), "green");
                currentFile.close();
                currentBlockNumber = 0;

                countTransmitFiles++;
                if (countTransmitFiles == (needFlashUboot ? 4 : 3)) {
                    stopServer();
                    needFlashUboot = false;
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

            // Сохраняем последний отправленный блок
            lastSentData = dataPacket;
            lastSentBlock = currentBlockNumber;

            // if (progressPercent <= 0.1 || progressPercent >= 99.9) {
            //     qDebug() << QString("Отправлен блок данных, размер: %1, Блок: %2, Прогресс: %3%")
            //                     .arg(dataBlock.size())
            //                     .arg(currentBlockNumber)
            //                     .arg(progressPercent, 0, 'f', 2);
            // }

            // Обновляем прогресс
            int progressValue = (currentBlockNumber * 100) / totalBlocks;
            emit progressChanged(progressValue);
            break;
        }
        case Opcode_ERROR:
        {
            quint16 errorCode = qFromBigEndian<quint16>(datagram.constData() + 2);
            QString errorMessage = readString(datagram, 4);
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

QByteArray TftpServer::readString(QByteArray datagram, int offset)
{
    int end = offset;
    while (end < datagram.size() && datagram[end] != '\0') {
        ++end;
    }
    return datagram.mid(offset, end - offset);
}
