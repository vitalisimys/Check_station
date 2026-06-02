#ifndef FLASHER_H
#define FLASHER_H

#include <QAtomicInteger>
#include <QCoreApplication>
#include <QDir>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QTime>
#include <QTimer>
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
    explicit Flasher(QObject *parent = nullptr);
    ~Flasher();

    void getSetConfig(const QString &ipm);
    const PPMConfig &currentConfig() const { return config; }
    void startUpdating(const QString &ip, bool saveRadioData, const QString &variant, const QString &bootcmd);
    void finishUpdating(const QString &ip, bool needChangeLedColor, const QString &variant, QString &blocName);
    QPair<QString, QString> getcontent(const QString &ip);
    void startCheckConnect(const QString &ip, bool longInitialDelay = true);
    void stopCheckConnect();
    void setQuietConnectionErrors(bool quiet);
    bool checkingPort();
    bool changeNumStation(const QString &ip, const QString &enteredNum);
    bool changeVarStation(const QString &ip, const uint &enteredVar);

    // Принудительно остановить TFTP-сервер (например, после ошибки прошивки).
    void stopTftpServer();

signals:
    void logMessage(const QString &message, const QString &color);
    void connectCompleted();
    /** SSH к станции пока недоступен, будет повтор (для журнала в режиме обновления). */
    void checkConnectRetry(const QString &ip);
    void progessChanged(int progressValue);
    void transmitFinish();
    void updateFailed(const QString &errorText);

public slots:
    void forwardLogMessage(const QString &message, const QString &color) {
        emit logMessage(message, color);
    }
    void forwardProgessChanged(int progressValue) {
        emit progessChanged(progressValue);
    }
    void forwardTransmitFinish() {
        emit transmitFinish();
    }

private:
    PPMConfig config;
    SSHer ssher;                // Используется UI-операциями (синхронно с самим UI-потоком).
    QMutex sshMutex;            // Защита одиночной libssh2-сессии от параллельного доступа.
    TftpServer *tftpServer;
    QAtomicInteger<int> m_firstCheckConnect{1}; // Заменяет static-флаг между сессиями обновления.
    QAtomicInteger<int> m_checkConnectGeneration{0};
    bool m_quietConnectionErrors = false;

    void loadConfig(uint variant);
    bool writeConfigToUboot(const QString &ip, const uint &newVariant);
    void readConfigFromUboot(const QString &ip);
    void emitUpdateFailed(const QString &message);
};
#endif // FLASHER_H
