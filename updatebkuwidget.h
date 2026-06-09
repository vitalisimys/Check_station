#ifndef UPDATEBKUWIDGET_H
#define UPDATEBKUWIDGET_H

#include <QWidget>
#include <QList>
#include <QStringList>
#include <functional>

#include "flasher.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class UpdateBkuWidget;
}
QT_END_NAMESPACE

class UpdateBkuWidget : public QWidget
{
    Q_OBJECT

public:
    using EnsureTftpServerIpFn =
        std::function<bool(QString *errorText, bool *addressWasAdded, bool strict, bool *networkAddressReady)>;
    using ResolveStationIpFn = std::function<QString()>;

    explicit UpdateBkuWidget(QWidget *parent = nullptr);
    ~UpdateBkuWidget() override;

    void setStationContext(const QString &stationIp, const QString &interfaceName);
    void setStationLinkActive(bool reachable);
    void setEnsureTftpServerIpFn(EnsureTftpServerIpFn fn);
    void setResolveStationIpFn(ResolveStationIpFn fn);
    void activatePanel();
    void deactivatePanel();
    bool canStartUpdate() const;
    bool canStartEmergencyTftp() const;
    bool isStationLinkedForUpdate() const { return m_stationReachable; }
    bool isUpdateInProgress() const { return m_updateInProgress; }
    bool isAwaitingBootcmdReset() const { return m_awaitingBootcmdReset; }
    QString stationVariantForLabel() const { return m_variant; }
    void notifyStationReachableForPostUpdate();
    void startPostKernelBootSshCheck(const QString &stationIp);
    void refreshFirmwareFilesStatus();

public slots:
    void startUpdate();
    void startEmergencyTftp();

signals:
    void logMessage(const QString &message, const QString &color);
    void progressChanged(int value);
    void updateBusyChanged(bool busy);
    void startUpdateButtonEnabledChanged(bool enabled);
    void bkuHeaderButtonStateChanged();
    void postEmergencyTftpWaitingStarted();
    /** После reboot (смена номера/варианта, прошивка): переподключить UDP и подсеть на новый IP станции. */
    void stationReconnectAfterRebootRequested(const QString &stationIp);
    /** Станция перезагружена (прошивка / смена номера или варианта): при переходе в «Тестирование» — как первое подключение. */
    void deferredTestingInitRequired();

private slots:
    void on_pushButtonLoadFile_clicked();
    void on_pushButtonEditNum_clicked();
    void on_pushButtonEditVar_clicked();
    void waitingConnection();
    void onConnectCompleted();
    void onUpdateFailed(const QString &errorText);

private:
    // Что именно мы ждём по завершении следующего connectCompleted.
    enum class PendingOp {
        None,
        AfterFlash,        // полная прошивка БКУ
        AfterChange,       // смена номера/варианта станции
        EmergencyTftp,     // аварийный TFTP и последующий сброс bootcmd по SSH
    };

    bool loadFile(const QString &filePath, bool clearExistingOnFirstLoad);
    void applyFirmwareStatusToUi();
    void updateStartUpdateButtonState();
    void setUpdateControlsEnabled(bool enabled);
    void applyConnectionDependentControls();
    bool hasFirmwareReadyForTftp() const;
    bool prepareTftpEnvironment(QString *prepareError, bool requireVariant, bool requireNetwork);
    void beginUpdateSession(PendingOp pending);
    void finishUpdateSession();
    void cancelPendingLoadStationInfo();
    void loadStationInfoAsync(std::function<void()> onDone = {}, bool forceDespiteUpdateInProgress = false);
    void applyVersionOutput(const QString &output);
    void applyConfigLabels();
    QString presenceText(bool present) const;
    QString blocNameForVariant(const QString &variantVariant) const;
    bool isAllowedFirmwareFileName(const QString &fileName) const;
    QString canonicalFirmwareName(const QString &fileName) const;
    void rebuildBootcmd();
    void logFirmwareFilesStatus(bool forceLog = false);
    QString resolvedStationIp() const;
    void schedulePostRebootSshCheck(const QString &stationIp);

    Ui::UpdateBkuWidget *ui;
    Flasher *m_flasher = nullptr;
    EnsureTftpServerIpFn m_ensureTftpServerIp;
    ResolveStationIpFn m_resolveStationIp;

    QString m_stationIp;
    QString m_interfaceName;
    bool m_stationReachable = false;
    QString m_staNum;
    QString m_variant;
    QString m_blocName = QStringLiteral("БКУ");
    QString m_bootcmd = QStringLiteral("/usr/sbin/fw_setenv bootcmd \"run angstremtftp_fdt; run angstremtftp_kernel; "
                                       "run angstremtftp_rootfs; run angstremcore1_boot\"");
    QStringList m_loadedFiles;
    bool m_updateInProgress = false;
    bool m_awaitingBootcmdReset = false;
    bool m_hasUbootFirmware = false;
    int m_loadStationInfoGeneration = 0;
    PendingOp m_pendingOp = PendingOp::None;
    QString m_lastLoggedFirmwareStatus;

    const QStringList m_allowedFilePrefixes = {
        QStringLiteral("bku-p2020"),
        QStringLiteral("rootfs"),
        QStringLiteral("kernel"),
        QStringLiteral("u-boot"),
    };
};

#endif // UPDATEBKUWIDGET_H
