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
    using ExecuteCommandFn = std::function<QPair<bool, QString>(const QString &command)>;
    using EnsureTftpServerIpFn = std::function<bool(QString *errorText, bool *addressWasAdded)>;

    explicit UpdateBkuWidget(QWidget *parent = nullptr);
    ~UpdateBkuWidget() override;

    void setStationContext(const QString &stationIp, const QString &interfaceName);
    void setExecuteCommandFn(ExecuteCommandFn fn);
    void setEnsureTftpServerIpFn(EnsureTftpServerIpFn fn);
    void activatePanel();
    void deactivatePanel();
    bool canStartUpdate() const;
    bool isUpdateInProgress() const { return m_updateInProgress; }
    void refreshFirmwareFilesStatus();

public slots:
    void startUpdate();
    void startEmergencyTftp();

signals:
    void logMessage(const QString &message, const QString &color);
    void progressChanged(int value);
    void updateBusyChanged(bool busy);
    void startUpdateButtonEnabledChanged(bool enabled);

private slots:
    void on_pushButtonLoadFile_clicked();
    void on_pushButtonEditNum_clicked();
    void on_pushButtonEditVar_clicked();
    void printConfigFromUboot(const QString &htmlTable);
    void waitingConnection();
    void updateStateAfterFlash();
    void updateStateAfterChange();

private:
    static QString updateFilesDirectory();
    static QString findFirmwareFileByPrefix(const QDir &dir, const QString &prefix);
    static bool ensureCanonicalFirmwareFile(QDir &dir, const QString &prefix, const QString &canonicalName);
    bool loadFile(const QString &filePath, bool clearExistingOnFirstLoad);
    void applyFirmwareStatusToUi();
    void updateStartUpdateButtonState();
    void setUpdateControlsEnabled(bool enabled);
    bool prepareTftpEnvironment(QString *prepareError, bool *addressWasAdded, bool requireVariant);
    void beginUpdateSession();
    void loadStationInfo();
    void applyVersionOutput(const QString &output);
    void applyConfigLabels();
    QString presenceText(bool present) const;
    QString blocNameForVariant(const QString &variantVariant) const;
    bool isAllowedFirmwareFileName(const QString &fileName) const;
    QString canonicalFirmwareName(const QString &fileName) const;

    Ui::UpdateBkuWidget *ui;
    Flasher *m_flasher = nullptr;
    ExecuteCommandFn m_executeCommand;
    EnsureTftpServerIpFn m_ensureTftpServerIp;

    QString m_stationIp;
    QString m_interfaceName;
    QString m_staNum;
    QString m_variant;
    QString m_blocName = QStringLiteral("БКУ");
    QString m_bootcmd = QStringLiteral("/usr/sbin/fw_setenv bootcmd \"run angstremtftp_fdt; run angstremtftp_kernel; "
                                       "run angstremtftp_rootfs; run angstremcore1_boot\"");
    QStringList m_loadedFiles;
    bool m_updateInProgress = false;
    bool m_hasUbootFirmware = false;

    const QStringList m_allowedFilePrefixes = {
        QStringLiteral("bku-p2020"),
        QStringLiteral("rootfs"),
        QStringLiteral("kernel"),
        QStringLiteral("u-boot"),
    };
};

#endif // UPDATEBKUWIDGET_H
