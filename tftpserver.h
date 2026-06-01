#ifndef TFTPSERVER_H
#define TFTPSERVER_H

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUdpSocket>
#include <QtEndian>

class TftpServer : public QObject
{
    Q_OBJECT

public:
    explicit TftpServer(QObject *parent = nullptr);

signals:
    void logMessage(const QString &message, const QString &color); // Сигнал для вывода сообщений
    void progressChanged(int progressValue); // Сигнал для обновления прогресса
    void transmitFinish();

public slots:
    bool startServer(); // Метод для запуска сервера
    void stopServer(); // Метод для остановки сервера

private slots:
    void processPendingDatagrams();

private:
    void resetTransferState();
    QByteArray readString(const QByteArray &datagram, int offset) const;

    QUdpSocket *udpSocket;
    QFile currentFile;
    quint16 currentBlockNumber = 0;
    quint16 lastSentBlock = 0;
    QByteArray lastSentData;
    QString filename;
    QSet<QString> finishedFiles;            // Уникальные имена успешно переданных файлов.
    QSet<QString> requiredFiles;            // Имена, которые мы ожидаем увидеть до завершения.
    bool needFlashUboot = false;
    int totalBlocks = 0;
    bool isRunning = false;

    enum Opcode {
        Opcode_RRQ = 1,
        Opcode_WRQ = 2,
        Opcode_DATA = 3,
        Opcode_ACK = 4,
        Opcode_ERROR = 5,
        Opcode_OACK = 6
    };
};

#endif // TFTPSERVER_H
