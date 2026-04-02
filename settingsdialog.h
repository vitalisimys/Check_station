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
    explicit SettingsDialog(QWidget *parent = nullptr,
                            const QStringList &initialIfaces = QStringList(),
                            const QString &preselectedIface = QString(),
                            const QVector<QString> &cachedFoundIps = QVector<QString>());
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
    void onStationSelectionChanged(int index);

private:
    QPair<bool, QString> executeCommand(const QString &command) const;
    bool ensureStationIpsConfigured(const QString &interfaceName,
                                    const QString &stationIp,
                                    QString *chosenSelfIp,
                                    QString *errorText = nullptr) const;
    QStringList collectEligibleInterfaces() const;

    Ui::SettingsDialog *ui;
    FindManager *m_finder;
    QString m_preparedStationIp;
    QString m_preparedSelfIp;
    // Разрешаем автоподключение, только когда это результат "живого" сканирования,
    // а не подстановка кэша при открытии настроек.
    bool m_allowAutoConnectSingleStation = true;
};

#endif // SETTINGSDIALOG_H
