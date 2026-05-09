#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QElapsedTimer>
#include <QTimer>
#include <QCloseEvent>
#include <QPair>
#include <QVector>
#include <QHash>
#include <QStringList>
#include <QSharedPointer>
#include <QTemporaryFile>
#include <QIcon>
#include <QThread>
#include "settingsdialog.h"
#include "device_controller.h"
#include "analyzer_controller.h"
#include "power_traffic_generator.h"
#include "finder.h"
#include "sweep_plot.h"

class QButtonGroup;
class QRadioButton;
class QMovie;
class QMouseEvent;
class QEvent;
class QCPItemRect;
class QFrame;
class QLabel;
class QProgressBar;
class QLCDNumber;

struct ReceiveResultStripUi {
    QFrame *frame = nullptr;
    QLabel *baselineValue = nullptr;
    QLabel *rssiValue = nullptr;
    QLCDNumber *freqTestLcd = nullptr;
    QWidget *levelIndicators[8] = {};
    QLabel *resultValue = nullptr;
    QLabel *statusTestFinishOk = nullptr;
    QLabel *statusTestFinishNot = nullptr;
};

/// Запись из TraktParam.xml (TrLN, TrmType, TrmNr).
struct TraktParamEntry {
    int trLn = 0;
    int trmType = 0;
    int trmNr = 0;
};

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
    void onStationConnectRequested(const QString &stationIp, const QString &interfaceName);
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
    void onPowerTestingToggled(bool checked);
    void onPowerLevelRadioToggled(bool checked);
    void onPowerTestPauseClicked();
    void onPowerTestStopClicked();
    void onPostRebootWaitTimeout();
    void onPostRebootWaitProgressTick();
    void onPostRebootReconnectTick();
    void onTractPowerAwaitingAck(uint8_t tractNum, bool enable);
    void onTractPowerAcknowledged(uint8_t tractNum, bool isOn);
    void onTractPowerAckTimeout(uint8_t tractNum, bool expectedOn);
    void onTractPowerIndicationReceived(uint8_t tractNum, bool isOn);
    void onPpmRadioClicked(int id);
    void onPpmUpdateClicked();
    void onFreqRxIndicationReceived(uint8_t tractNum, uint32_t freqHz);
    void onFreqTxIndicationReceived(uint8_t tractNum, uint32_t freqHz);
    void onRssiIndicationReceived(uint8_t tractNum, int16_t rssiDbm);
    void onPowerLevelIndicationReceived(uint8_t tractNum, uint8_t levelCode);
    void onPpmStatusIndicationReceived(uint8_t tractNum, int16_t code);
    void onWorkModeIndicationReceived(uint8_t tractNum, uint16_t mode);
    void onActiveDirectionIndicationReceived(uint8_t tractNum, uint8_t dirId);
    void onChannelReadyIndicationReceived(uint8_t tractNum, uint8_t linkStatus);
    void onLinkStatusIndicationReceived(uint8_t tractNum, uint16_t val);
    void onAntennaFaultPulseTick();
    void onPowerGraphPlotMouseMove(QMouseEvent *event);
    void onReceiveTestStartClicked();
    void onReceiveTestPauseClicked();
    void onReceiveTestStopClicked();
    void onReceiveTestTick();
    void onStartTestingFhssClicked();
    void onFhssStopClicked();

private:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
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
    void setStartTestingButtonEnabled(bool enabled);
    void startProfileIntegritySequenceAfterReboot(const QString &stationIp);
    bool verifyProfileIntegrityAfterRebootOverSsh(const QString &stationIp, QString *errorText);
    void initPpmUiStyle();
    void initPowerTestingUi();
    void initPowerTestingPlots();
    void updatePowerTestingPlots(const QVector<double> &freqs, const QVector<double> &amps);
    bool startPowerMeasurementStep();
    void finishPowerMeasurementStep();
    void setEmissionAnimating(bool on);
    void applyTraktParamToPpmUi(const QVector<TraktParamEntry> &entries, int traktNum);
    void setPpmRadioUiState(int id, bool isOn, bool checked);
    void setAllPpmRadiosEnabled(bool enabled);
    QVector<int> ppmTractNumbersForUi() const;
    int ppmFirstTractNumber() const;
    void startPpmInitAfterIntegrityOk();
    void continuePpmInitSequence();
    void startPpmSwitchToTract(int tractNum);
    void continuePpmSwitchSequence();
    bool shouldUpdatePowerReadoutForTract(uint8_t tractNum) const;
    int selectedPpmTractFromUi() const;
    enum class PpmStatusStyle { Ok, Warning, Fault };
    /** Только подпись: статус передатчика (IND_ERROR) в labelPPMStatus */
    void applyPpmTransmitterLabel(const QString &statusText, PpmStatusStyle style);
    /** Рамка PPM: цвет по состояниям TRAKT_* (аналогично frame_ppm_status в пульте). */
    void applyPpmModeFrameForTract(int tractNum);
    void applyPpmModeFrameIdle();
    void setPpmFrameStateForTract(int tractNum, int state);
    /// Индикация IND_ERROR → TRAKT_* для рамок: как PpmForm::leerrorCode в ControlPanelSurs.
    void applyPpmErrorIndicationFrameLikeControlPanel(int tractNum, int16_t code, int16_t lastCode);
    void maybeRestoreDefaultDirectionForTract(int tractNum);
    void setPpmUpdateLabelVisible(bool visible);
    bool restartPpmModeForTract(int tractNum);
    void markPpmModeLaunchStarted(int tractNum);
    void clearPpmModeLaunchStateForTract(int tractNum);
    void ensurePpmModeLaunchDeadlineSeeded(int tractNum);
    void refreshPpmModeLaunchTimeoutEval(int tractNum);
    void refreshPpmStatusUiForTract(int tractNum);
    void resetPowerReadoutUi();
    void initReceiveTestingUi();
    void resetReceiveReadoutUi();
    void ensureReceiveResultStripsBuilt();
    void tearDownReceiveTest(bool generatorOff);
    void setReceiveTestControlsIdle();
    void setReceiveTestControlsRunning(bool playbackPaused);
    void syncReceiveStripFreqTestLabels();
    QLabel *receiveStripResultLabel(int freqIndex) const;
    /// Частотные полоски tabRecieve: превью набора частот для текущего выбора ППМ (без запущенного теста).
    void syncReceiveTabPreviewFromCurrentTract();
    /// Общий прогресс теста приёма (0–100): только детерминированные шаги уровней 5 с; ожидание baseline не «раздувает» шкалу.
    int receiveTestOverallProgressPercent() const;
    void updateReceiveResultStripsVisibility();
    void resetReceiveTestUiForNewTractSelection(int targetTract);
    void pausePowerTestForPpmDisconnect();
    void pausePowerTestForAntennaFault();
    void pausePowerTestForDirectionRestore();
    /// Отложенное авто-возобновление теста мощности после стабилизации (как после «Нет связи»→«Норма»).
    void attemptScheduleDelayedPowerTestResume(int tractNum);
    /// IND_ACTIVEDIR=1 + «Норма»/ЛУМ: выставить TRAKT_WRK, если IND_ERROR не менялся (повтор выбора DirId=1).
    /// Если requireNonZeroWorkMode=true — только при ненулевом IND_WORKMODE (иначе TRAKT_END_ON от IND_TRAKT_* «перебивает» в жёлтый навсегда).
    void syncPpmFrameForDir1IfTransmitterOk(int tractNum, bool requireNonZeroWorkMode = false);
    bool isPpmTractReadyForPowerTest(int tractNum) const;
    void updatePowerTestButtonsAccessForSelectedTract();
    void updateReceiveTestButtonsAccessForSelectedTract();
    void stopReceiveTestIfTractNotReady(int tractNum);
    void pauseReceiveTestForPpmNotReady(int tractNum);
    void setPowerTestControlsIdle();
    void setPowerTestControlsRunning(bool playbackPaused);
    void resetPowerTestUiForNewTractSelection(int targetTract);
    void updateTabWidgetLockState();
    void applyHandsDefaultsForTract(int tractNum);
    void applyHandsAnalyzerCenterSpan05FromUi();
    void hidePowerGraphHoverLabel();
    void initPowerGraphHelperRects();
    void updatePowerGraphHelperRectsXSpan();
    void updatePowerGraphScatterLayers();
    void applyPowerLevelUiByCode(uint8_t levelCode, bool rescaleGraph);
    /** tractOverride > 0: центр для мин. мощности по этому TrId (например при смене тракта до смены m_ppmCurrentOnTract). */
    double currentPowerGraphCenterDbm(int tractOverride = 0) const;
    void applyPowerGraphCenterScale();
    void clearPowerGraphPlotCurves();
    void updatePowerLevelRadioButtonsEnabled();
    void stopAllTestsForPpmRecovery();
    void maybePauseTestsForExternalWorkModeChange(int tractNum, uint16_t prevMode, uint16_t newMode);
    void pauseTestsForExternalWorkModeAndRestartPpm(int tractNum);
    void tryResumeTestsAfterExternalWorkModeRecovery(int tractNum);
    /// Активен ли тест мощности/приёма на тракте — для паузы теста при внешней смене IND_WORKMODE.
    bool isPowerTestRunningForExternalWorkModePause(int tractNum) const;
    bool isReceiveTestRunningForExternalWorkModePause(int tractNum) const;

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
    void initFhssTestingUi();
    void initFhssPlot();
    void setFhssTestControlsIdle();
    void setFhssTestControlsIdle(bool clearMaxHold);
    void setFhssTestControlsRunning(bool running);
    void updateFhssModeComboForTract(int tractNum);
    bool startFhssTransmission();
    void applyFhssXAxisForTract(int tractNum);
    /// Ось X графика ППРЧ (может быть шире диапазона запроса анализатора, напр. для «полей»).
    QPair<quint64, quint64> fhssPlotXAxisRangeHzForTract(int tractNum) const;
    /// Диапазон запроса анализатора для ППРЧ по тракту.
    QPair<quint64, quint64> fhssSpectrumRangeHzForTract(int tractNum) const;
    bool isFhssTabActive() const;
    void updateFhssRangeLcdForTract(int tractNum);
    void updateFhssTestButtonsAccessForSelectedTract();
    /// «Тест ППРЧ активен» — для блокировки остальных вкладок (как у теста мощности).
    /// Считаем активным, пока запрошена работа: исполняется передача, ожидание DirId=2,
    /// либо тест поставлен на внешнюю паузу (Нет связи с ПП / Авария АНТ).
    bool isFhssTestActive() const;
    /// После «Норма» отложенно возобновить ППРЧ-тест (если был на внешней паузе).
    void attemptScheduleDelayedFhssTestResume(int tr);

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
    PowerTrafficGenerator *m_powerTrafficGenerator = nullptr;
    QElapsedTimer m_uptime;
    QVector<AddedIpEntry> m_addedIps;
    bool m_cleanupDone = false;

    // Спектр (tabHands / plotWidget)
    bool m_analyzerConnected = false;
    bool m_startSpectrumOnHands = false;
    bool m_spectrumPlotInitialized = false;
    bool m_spectrumStreaming = false;
    int m_tabHandsIndex = -1;
    int m_tabPowerIndex = -1;
    int m_tabReceiveIndex = -1;
    int m_tabFhssIndex = -1;
    int m_lastUnlockedTabIndex = -1;
    bool m_tabWidgetWasLocked = false;
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

    QButtonGroup *m_ppmButtonGroup = nullptr;
    QVector<QRadioButton *> m_ppmExtraRadios;
    int m_maxTrLn = 0;
    QVector<int> m_ppmTractsSorted; // по порядку UI (id=0..N-1) -> trLn
    QHash<int, int> m_ppmTrmTypeByTract; // trLn -> TrmType
    QHash<int, int16_t> m_ppmLastStatusCodeByTract; // trLn -> IND_ERROR code
    QHash<int, uint16_t> m_ppmLastWorkModeByTract; // trLn -> IND_WORKMODE value
    QHash<int, uint8_t> m_ppmLastLinkStatusByTract; // trLn -> IND_CHREADY/linkStatusIndicator
    QHash<int, int> m_ppmFrameStateByTract; // trLn -> TRAKT_* visual state (ControlPanel-like)
    QHash<int, uint8_t> m_ppmLastDirIdByTract; // trLn -> last IND_ACTIVEDIR DirId
    QHash<int, bool> m_ppmRestoreDefaultDirPendingByTract; // trLn -> wait TRAKT_WRK then set DirId=1
    QHash<int, bool> m_ppmRestoreDefaultDirInFlightByTract; // trLn -> CMD_CURR_DIR_SET(...,1) already sent
    /// После внешней смены направления (≠1): номер тракта, пока не придёт IND_ACTIVEDIR с DirId=1.
    /// Пока ≥0 — индикации вкл/выкл тракта не считаем «внешними» для защиты восстановления.
    int m_ppmExternalDirRecoveryTract = -1;
    /** Ожидание ненулевого режима после вкл. тракта (жёлтая рамка); таймаут → красная рамка */
    QHash<int, bool> m_ppmModeLaunchPendingByTract;
    QHash<int, bool> m_ppmModeLaunchTimedOutByTract;
    QHash<int, qint64> m_ppmModeLaunchSinceMsByTract;
    int m_ppmCurrentOnTract = -1;
    int m_ppmPendingTargetOnTract = -1;
    bool m_ppmSwitchNeedsPostUpdate = false;
    // Защита от "самоподрыва": после внутреннего переключения тракта/режима
    // некоторое время игнорируем переходы IND_WORKMODE и выключение текущего тракта.
    qint64 m_ppmLastTractSwitchFinishedAtMs = -1;
    int m_ppmLastTractSwitchToTract = -1;
    int m_ppmIgnoreExternalPowerOffTract = -1;
    qint64 m_ppmIgnoreExternalPowerOffUntilMs = 0;
    // Дедупликация: "режим упал в 0" может быть следствием выключения тракта извне.
    // В этом случае не нужно сразу считать это внешней сменой режима — ждём окно и
    // если режим не восстановился, запускаем восстановление тракта.
    QHash<int, quint64> m_ppmWorkModeZeroSerialByTract;
    // Защита от петель: не инициировать перезапуск режима чаще, чем раз в N мс.
    QHash<int, qint64> m_ppmLastExternalWorkModeRestartAtMsByTract;

    enum class PpmPowerSequenceStage {
        None,
        InitAllOff,
        InitFirstOn,
        InitFirstOnWaitAck,
        SwitchOffCurrent,
        SwitchOffOthersBeforeOn,
        SwitchOnTarget
    };
    PpmPowerSequenceStage m_ppmPowerStage = PpmPowerSequenceStage::None;
    int m_ppmPowerSeqIndex = 0;

    QMovie *m_powerTestMovie = nullptr;
    QTimer m_powerTestAutoStopTimer;
    QTimer m_powerTestStepPauseTimer;
    QTimer m_powerTestBeforePowerOnTimer;
    bool m_powerPlotsInitialized = false;
    SweepPlotTraces m_powerMomentTraces;
    QCPGraph *m_powerGraphTrace = nullptr;
    QCPGraph *m_powerGraphScatterOk = nullptr;
    QCPGraph *m_powerGraphScatterBad = nullptr;
    QCPItemText *m_powerGraphHoverLabel = nullptr;
    QVector<QCPItemRect *> m_powerGraphHelperRects;

    // tabRecieve: тест приёма с генератором анализатора
    QTimer m_receiveTestTickTimer;
    QElapsedTimer m_receiveTestElapsed;
    enum class ReceiveTestPhase { Idle, WaitBaseline, RunningLevel };
    ReceiveTestPhase m_receivePhase = ReceiveTestPhase::Idle;
    bool m_receiveTestRunning = false;
    bool m_receiveTestPaused = false;
    bool m_receiveTestAutoPausedByPpmNotReady = false; // автопауза из-за "плохого" статуса/не зелёной рамки
    bool m_receiveTestAutoPausedByExternalWorkMode = false;
    int m_receiveTestTract = -1;
    int m_receiveFreqIndex = 0;   // индекс в m_receiveTestFreqsHz
    int m_receiveLevelIndex = 0;  // уровень генератора
    quint64 m_receiveTestFreqHz = 0;
    QVector<quint64> m_receiveTestFreqsHz; // последовательность частот теста приёма для выбранного тракта
    quint8 m_receiveTestPow = 0;
    int m_receiveTestPowDbm = 0;
    int m_receiveBaselineRssiDbm = 0;
    int m_receiveLastRssiDbm = 0;
    double m_receiveLastRssiDbmFull = 0.0;  // RSSI с дробной частью (1 знак), dBm
    int m_receiveLevelMaxRssiDbm = -9999;
    QVector<int> m_receiveFreqBaselineRssiDbm; // baseline RSSI по частотам (размер = m_receiveTestFreqsHz.size())
    QVector<bool> m_receiveFreqAllLevelsOk; // итог по каждой частоте (true если все уровни OK)
    QVector<ReceiveResultStripUi> m_receiveResultStrips;
    bool m_receiveResultStripsBuilt = false;
    /// При паузе теста приёма фиксируем отображаемый процент общего прогресса (elapsed не останавливается).
    int m_receiveProgressFrozenPercent = -1;
    QIcon m_receiveTestIconPause;
    QIcon m_receiveTestIconPlay;
    QIcon m_receiveTestIconStop;
    QIcon m_powerTestIconPause;
    QIcon m_powerTestIconPlay;
    QIcon m_powerTestIconStop;
    QHash<int, int> m_lastRssiDbmByTract;

    // Повтор команд включения/выключения тракта при таймауте ACK.
    // key = (tractNum<<1) | (expectedOn?1:0), value = retryCount
    QHash<quint32, int> m_tractPowerAckRetries;
    bool m_powerGraphAutoYInitialized = false;
    double m_powerGraphAutoYCenterDbm = 0.0;
    bool m_powerStepBestValid = false;
    double m_powerStepBestFreqMHz = 0.0;
    double m_powerStepBestAmpDbm = -200.0;
    int m_powerTestCurrentFreqRetryCount = 0;
    quint64 m_powerTestCurrentFreqHz = 0;
    quint64 m_powerMomentDisplayFreqHz = 0;
    QVector<double> m_powerGraphFreqsMHz;
    QVector<double> m_powerGraphAmpsDbm;
    QVector<quint64> m_powerGraphTargetFreqsHz;
    QVector<quint64> m_powerTestSequenceFreqsHz;
    int m_powerTestSequenceIndex = -1;
    bool m_powerMeasurementRunning = false;
    bool m_powerTrafficStartPending = false;
    bool m_powerTestPaused = false;         // пауза (без сброса последовательности), чтобы можно было продолжить
    bool m_powerTestBlockedByPpm = false;   // кнопка заблокирована из-за "Нет связи с ПП"
    bool m_powerTestBlockedByAntFault = false; // тест на паузе из-за "Авария АНТ"
    bool m_powerTestBlockedByDirRestore = false; // пауза из-за внешней смены направления / возврата DirId=1
    bool m_ignorePowerLevelUiSignal = false;
    bool m_powerTestAutoPausedByExternalWorkMode = false;
    uint8_t m_powerLevelCode = 4; // 1=min, 4=max; по умолчанию max
    QHash<int, uint8_t> m_powerLevelCodeByTract; // trLn -> код уровня мощности (1..4)
    quint64 m_powerResumeAfterPpmSerial = 0; // отмена/дедупликация отложенного auto-resume после "Норма"
    quint64 m_resumeAfterExternalWorkModeSerial = 0;
    bool m_testsPausedForExternalWorkMode = false;
    int m_externalWorkModePauseTract = -1;
    /** После CMD_CURR_DIR_SET из Check_station: не считать смену IND_WORKMODE «внешней», пока не пришёл ненулевой режим. */
    int m_ppmCurrDirSetByCheckStationTract = -1;
    double m_powerStepAmpAccumDbm = 0.0;
    int m_powerStepAmpSampleCount = 0;
    // Тракт, на котором выполняется тест мощности. 0 = тест не активен/таргет не задан.
    // Важно: не должен иметь "дефолтное" ненулевое значение, иначе IND_ERROR по этому тракту
    // может ошибочно переводить UI в состояние pause (play/stop) без запуска теста.
    uint8_t m_powerTestTargetTract = 0;
    int m_powerTestTargetTrmType = -1;
    QString m_powerTestMulticastAddress;

    // "Авария антенны": фоновая "подкачка" трафика для выхода на мощность.
    QThread *m_antFaultPulseThread = nullptr;
    QTimer *m_antFaultPulseTimer = nullptr; // живёт в m_antFaultPulseThread
    bool m_antFaultPulseActive = false;
    int m_antFaultPulseTract = -1;
    quint64 m_antFaultPulseSerial = 0;
    bool m_antFaultPulseTrafficActive = false;
    QHash<int, quint64> m_lastTxFreqHzByTract;

    // tabFHSS: тест ППРЧ (live spectrum + maxhold + подача мощности multicast)
    bool m_fhssControlsInitialized = false;
    bool m_fhssRunning = false;
    bool m_fhssDirSwitchPending = false;
    int m_fhssTract = -1;
    bool m_fhssAutoMaxHold = false; // maxhold линия только для plotWidgetFHSSGraph (не связана с pushButtonSpectrumMaxHold)
    // После остановки ППРЧ-теста maxhold НЕ сбрасываем: держим до следующего запуска теста кнопкой START.
    bool m_fhssKeepMaxHoldUntilNextStart = false;
    int m_fhssMaxHoldTract = -1;
    bool m_fhssBlockedByPpm = false;
    bool m_fhssBlockedByAntFault = false;
    bool m_fhssPlotInitialized = false;
    SweepPlotTraces m_fhssTraces;
    QVector<double> m_fhssMemoryAmps;
    /// Дедупликация/отмена отложенного auto-resume теста ППРЧ после «Норма».
    quint64 m_fhssResumeAfterPpmSerial = 0;

    enum class ProfileIntegrityStage {
        None = 0,
        WaitingAfterReboot,
        Reconnecting,
        Checking
    };
    ProfileIntegrityStage m_profileIntegrityStage = ProfileIntegrityStage::None;
    QString m_profileIntegrityStationIp;
    QTimer m_postRebootWaitTimer;
    QTimer m_postRebootWaitProgressTimer;
    QElapsedTimer m_postRebootWaitElapsed;
    QTimer m_postRebootReconnectTimer;
};
#endif // MAINWINDOW_H
