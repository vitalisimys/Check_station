#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QTimer>
#include <QStringList>
#include <QPair>
#include "finder.h"  // Подключаем наш класс поиска

namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

    // Публичные методы для получения выбранных значений
    QString selectedInterface() const;
    QString selectedStationIp() const;

signals:
    void stationConnectRequested(const QString &stationIp, const QString &selfIp, const QString &interfaceName);

private slots:
    bool loadNetworkInterfaces();
    void onNetworkInterfaceChanged(const QString &interfaceName);
    void onScanFinished(const QVector<QString> &foundIps);
    void onConnectStationClicked();

private:
    QPair<bool, QString> executeCommand(const QString &command) const;
    bool ensureStationIpsConfigured(const QString &interfaceName,
                                    const QString &stationIp,
                                    QString *chosenSelfIp,
                                    QString *errorText = nullptr) const;
    QStringList collectEligibleInterfaces() const;
    void lockInterfaceAndHideSelector(const QString &interfaceName);

    Ui::SettingsDialog *ui;
    FindManager *m_finder;
    bool m_interfacesLoaded;
    QString m_lockedInterface;
};

#endif // SETTINGSDIALOG_H
