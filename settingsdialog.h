#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QTimer>
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
    void stationConnectRequested(const QString &stationIp);

private slots:
    bool loadNetworkInterfaces();
    void onNetworkInterfaceChanged(const QString &interfaceName);
    void onScanFinished(const QVector<QString> &foundIps);
    void onConnectStationClicked();

private:
    Ui::SettingsDialog *ui;
    FindManager *m_finder;
    bool m_interfacesLoaded;
};

#endif // SETTINGSDIALOG_H
