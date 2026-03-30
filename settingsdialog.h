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

private slots:
    void loadNetworkInterfaces();
    void onNetworkInterfaceChanged(const QString &interfaceName);
    void onScanFinished(const QVector<QString> &foundIps);

private:
    Ui::SettingsDialog *ui;
    FindManager *m_finder;
    bool m_interfacesLoaded;
};

#endif // SETTINGSDIALOG_H
