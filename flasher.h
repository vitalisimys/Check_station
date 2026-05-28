#ifndef FLASHER_H
#define FLASHER_H

#include <QDir>
#include <QThread>
#include <QCoreApplication>
#include <QTimer>
#include <QTime>
#include "ssher.h"
#include "tftpserver.h"

struct PPM {
    int type;                   // Тип PPM
    int cnt = 0;                // Количество блоков данного типа
};

struct PPMConfig {
    int num_ppms = 0;           // Количество PPM
    bool bi = false;            // Наличие BI
    bool f_mv_3k = false;       // Наличие F-MV-3K
    bool f_dmv1_2k = false;     // Наличие F-DMV1-2K
    bool f_dmv2_2k = false;     // Наличие F-DMV2-2K
    bool gnss = false;          // Наличие GNSS
    int variant = 1;            // Вариант конфигурации (1-22)
    QVector<PPM> ppms;          // Массив элементов "PPM"

    // Метод для преобразования +/- в bool
    bool stringToBool(const QString &value) {
        return value.trimmed().toLower() == "+" || value.trimmed().toLower() == "true";
    }
};

class Flasher : public QObject {
    Q_OBJECT
public:
    explicit Flasher(QString *bootcmdPtr, QObject *parent = nullptr);
    ~Flasher();

    void getSetConfig(const QString &ipm);
    const PPMConfig &currentConfig() const { return config; }
    void startUpdating(const QString &ip, bool saveRadioData, const QString &variant);
    void finishUpdating(const QString &ip, bool needChangeLedColor, const QString &variant, QString &blocName);
    QPair<QString, QString> getcontent(const QString &ip);
    void startCheckConnect(const QString &ip);
    bool checkingPort();
    bool changeNumStation(const QString &ip, const QString &enteredNum);
    bool changeVarStation(const QString &ip, const uint &enteredVar);

signals:
    void logMessage(const QString &message, const QString &color);
    void textConfigFromUboot(const QString &htmlTable);
    void connectCompleted();
    void progessChanged(int progressValue);
    void startTransmitFile(const QString &filename);
    void transmitFinish();

public slots:
    void forwardLogMessage(const QString &message, const QString &color) {
        emit logMessage(message, color);
    }
    void forwardProgessChanged(int progressValue) {
        emit progessChanged(progressValue);
    }
    void forwardStartTransmitFile(const QString &filename) {
        emit startTransmitFile(filename);
    }
    void forwardTransmitFinish() {
        emit transmitFinish();
    }
    void changeReadyLed(const QString &ip);

private:
    PPMConfig config;
    SSHer ssher;
    TftpServer *tftpServer;
    QTimer *timerCheckConnect;
    QString *bootcmdPtr;
    QString connectIp;
    int connectAttempt;

    void loadConfig(uint variant);
    bool writeConfigToUboot(const QString &ip, const uint &newVariant);
    void readConfigFromUboot(const QString &ip);
    void printConfig();
    QString createTableRow(const QString &key, const QString &value);
    QString getTypeName(int type);

private slots:
    void onCheckConnectTimeout();
};
#endif // FLASHER_H
