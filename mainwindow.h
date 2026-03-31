#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QCloseEvent>
#include <QPair>
#include <QVector>
#include "settingsdialog.h"
#include "device_controller.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_actionSettings_triggered();
    void onStationConnectRequested(const QString &stationIp, const QString &selfIp, const QString &interfaceName);
    void onDeviceConnected(const QString &ip);
    void onDeviceDisconnected();
    void onDeviceLogMessage(const QString &msg);
    void onDeviceError(const QString &err);

private:
    void closeEvent(QCloseEvent *event) override;
    void setStationConnectedUi();
    void setStationDisconnectedUi();
    QPair<bool, QString> executeCommand(const QString &command) const;
    void cleanupAddedSelfIp();

private:
    struct AddedIpEntry {
        QString iface;
        QString connectionName; // может быть пустым, тогда определим по --active
        QString ip;
        int cidr = 0;
    };

    Ui::MainWindow *ui;
    DeviceController *m_deviceController;
    QVector<AddedIpEntry> m_addedIps;
    bool m_cleanupDone = false;
};
#endif // MAINWINDOW_H
