#ifndef TFTPSERVER_H
#define TFTPSERVER_H

#include <QUdpSocket>
#include <QDir>
#include <QtEndian>
#include <QDataStream>
#include <QCoreApplication>

class TftpServer : public QObject
{
    Q_OBJECT

public:
    explicit TftpServer(QObject *parent = nullptr);

signals:
    void logMessage(const QString &message, const QString &color); // Сигнал для вывода сообщений
    void progressChanged(int progressValue); // Сигнал для обновления прогресса
    void startTransmitFile(QString filename);
    void transmitFinish();

public slots:
    bool startServer(); // Метод для запуска сервера
    void stopServer(); // Метод для остановки сервера

private slots:
    void processPendingDatagrams();

private:
    QUdpSocket *udpSocket;
    QFile currentFile;
    quint16 currentBlockNumber;
    QString filename;
    int countTransmitFiles = 0;
    bool needFlashUboot = false;
    int totalBlocks;
    bool isRunning;
    QByteArray readString(QByteArray datagram, int offset);     // Метод для чтения строк из пакета
    QByteArray lastSentData;                                    // Последний отправленный блок данных
    quint16 lastSentBlock;                                      // Номер последнего отправленного блока
    enum Opcode {                                               // Константы для TFTP операций
        Opcode_RRQ = 1,
        Opcode_WRQ = 2,
        Opcode_DATA = 3,
        Opcode_ACK = 4,
        Opcode_ERROR = 5,
        Opcode_OACK = 6
    };
};

#endif // TFTPSERVER_H
