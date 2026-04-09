#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QCloseEvent>
#include <QPair>
#include <QVector>
#include <QHash>
#include <QStringList>
#include <QSharedPointer>
#include <QTemporaryFile>
#include "settingsdialog.h"
#include "device_controller.h"
#include "analyzer_controller.h"
#include "finder.h"
#include "sweep_plot.h"

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
    void onAnalyzerConnected();
    void onAnalyzerDisconnected(const QString &reason);
    void onAnalyzerLogMessage(const QString &msg);
    void onTabWidgetCurrentChanged(int index);
    void onSpectrumDataReceived(const QVector<double> &freqs, const QVector<double> &amps);
    void onSpectrumMaxHoldToggled(bool checked);
    void onSpectrumUiTimer();
    void onSpectrumBwSliderChanged(int value);
    void onHandsSpectrumApplyClicked();
    void onSpectrumCenterSpanApplyClicked();
    void onSpectrumSavePlotClicked();
    void onToggleLogVisibilityClicked();
    void onStartTestingClicked();

private:
    void closeEvent(QCloseEvent *event) override;
    void setStationConnectedUi();
    void setStationDisconnectedUi();
    void setAnalyzerConnectedUi();
    void setAnalyzerDisconnectedUi();
    QPair<bool, QString> executeCommand(const QString &command) const;
    void cleanupAddedSelfIp();
    void startAutoDiscovery();
    QStringList collectEligibleInterfaces() const;
    void handleDiscoveryFinished(const QStringList &ifaces);
    void handleStationsFound(const QString &iface, const QVector<QString> &foundIps);
    bool ensureStationIpsConfigured(const QString &interfaceName,
                                    const QString &stationIp,
                                    QString *chosenSelfIp,
                                    QString *errorText = nullptr) const;
    void setTestingUiBusy(bool busy);
    void prepareTestProfileAfterConnect(const QString &stationIp);
    bool uploadAndActivateTestProfileOverSsh(const QString &stationIp, const QString &localTarPath, QString *errorText);

    void initSpectrumPlot();
    void startSpectrumStream();
    void stopSpectrumStream();
    bool parseHandsRangeHz(double *startHz, double *stopHz) const;
    void redrawSpectrumDisplay();
    bool parseAndValidateHandsRangeHz(quint64 *startHz, quint64 *stopHz) const;
    void syncHandsFreqLineEdits(quint64 startHz, quint64 stopHz);
    void applySpectrumRangeHz(quint64 startHz, quint64 stopHz, bool updateSpanCombo = true,
                              bool triggerBwDebugFrame = true, const quint64 *lockCenterDisplayHz = nullptr);
    void initSpectrumSpanCombo();
    void syncSpectrumCenterSpanFromRangeHz(quint64 startHz, quint64 stopHz, bool updateSpanCombo = true,
                                           bool updateCenterLine = true);
    void armSpectrumGridAlignToTargetHz(quint64 targetHz);
    bool parseTripletLineToHz(const QString &text, quint64 *outHz) const;
    bool spectrumRangeFromCenterSpanUi(quint64 *outStartHz, quint64 *outStopHz) const;
    bool spectrumBandFromCenterSpanMHz(double centerMHz, double spanMHz, quint64 *outStartHz, quint64 *outStopHz,
                                       QString *errorText) const;
    void updateSpectrumBwUi(int sliderIndex);
    void updateSpectrumPeakReadout();
    void syncSweepBoundsFromHz(quint64 startHz, quint64 stopHz);
    void clampSpectrumXAxisToSweep();
    void clampSpectrumYAxisToDbmRange();
    void scheduleSpectrumRedrawAfterAxisChange();
    bool isSpectrumMaxHoldOn() const;
    void updateLogToggleButtonText();

    struct AddedIpEntry {
        QString iface;
        QString connectionUuid; // может быть пустым, тогда определим по --active
        QString ip;
        int cidr = 0;
    };

    Ui::MainWindow *ui;
    DeviceController *m_deviceController;
    AnalyzerController *m_analyzerController = nullptr;
    FindManager *m_finder = nullptr;
    QVector<AddedIpEntry> m_addedIps;
    bool m_cleanupDone = false;

    // Спектр (tabHands / plotWidget)
    bool m_analyzerConnected = false;
    bool m_startSpectrumOnHands = false;
    bool m_spectrumPlotInitialized = false;
    bool m_spectrumStreaming = false;
    int m_tabHandsIndex = -1;
    SweepPlotTraces m_sweepTraces;
    QVector<double> m_spectrumMemoryAmps;
    QTimer m_spectrumUiTimer;
    QVector<double> m_spectrumLatestFreqs;
    QVector<double> m_spectrumLatestAmps;
    bool m_spectrumDisplayDirty = false;
    bool m_logCollapsed = false;
    /// Границы запрошенного sweep (МГц): видимый диапазон оси X не может выходить за них (только «уменьшение» внутри).
    double m_spectrumSweepMinMHz = 220.0;
    double m_spectrumSweepMaxMHz = 470.0;
    quint64 m_spectrumSweepStartHz = 220000000ULL;
    quint64 m_spectrumSweepStopHz = 470000000ULL;

    /// После кадра спектра: сдвиг start/stop на −(fNearest−fTarget), чтобы ближайший бин приблизился к цели.
    bool m_spectrumGridAlignPending = false;
    quint64 m_spectrumGridAlignTargetHz = 0;
    int m_spectrumGridAlignAttemptsLeft = 0;

    // Кэш автопоиска для открытия настроек без повторного сканирования.
    QStringList m_cachedIfaces;
    QHash<QString, QVector<QString>> m_cachedFoundIpsByIface;

    // Подготовленный профиль для текущей станции (собирается сразу после подключения).
    QString m_preparedProfileStationIp;
    bool m_preparingProfile = false;
    QSharedPointer<QTemporaryFile> m_preparedProfileTar;
};
#endif // MAINWINDOW_H
