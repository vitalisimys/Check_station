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
#include <QColor>
#include <QThread>
#include "settingsdialog.h"
#include "device_controller.h"
#include "analyzer_controller.h"
#include "power_traffic_generator.h"
#include "finder.h"
#include "sweep_plot.h"
#include "updatebkuwidget.h"

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
class QGraphicsDropShadowEffect;
class QVariantAnimation;

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
    void on_actionBkuUpdate_triggered();
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
    void onFhssUiTimer();
    void onSpectrumBwSliderChanged(int value);
    void onHandsSpectrumApplyClicked();
    void onSpectrumCenterSpanApplyClicked();
    void onSpectrumSavePlotClicked();
    void onToggleLogVisibilityClicked();
    void onStartTestingClicked();
    void onPowerTestingToggled(bool checked);
    void onPowerTestOptionsChanged();
    void onPowerLevelRadioToggled(bool checked);
    void onPowerTestPauseClicked();
    void onPowerTestStopClicked();
    void onPostRebootWaitTimeout();
    void onPostRebootWaitProgressTick();
    void onPostRebootReconnectTick();
    void onPostReconnectStationBootProgressTick();
    void onPostReconnectStationBootFallbackTimeout();
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
    void onProfileSwitchIndicationReceived(uint8_t profileId, uint8_t phase);
    void onChannelReadyIndicationReceived(uint8_t tractNum, uint8_t linkStatus);
    void onLinkStatusIndicationReceived(uint8_t tractNum, uint16_t val);
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
    void applyStationHeaderFromIp(const QString &ipTrimmed);
    void updateStationLabelText();
    void setAnalyzerConnectedUi();
    void setAnalyzerDisconnectedUi();
    /** Одна строка журнала с цветом по типу (ошибка — красный). */
    void appendDeviceLogLine(const QString &msg);
    void appendDeviceLogLine(const QString &msg, const QColor &color);
    QPair<bool, QString> executeCommand(const QString &command) const;
    void cleanupAddedSelfIp();
    void performShutdownCleanup();
    void runShutdownCleanupWithProgress();
    void startAutoDiscovery();
    void attemptStationConnectAfterEmergencyUpdate();
    void attemptStationConnectAfterBkuReboot(const QString &stationIp);
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
    void refreshStartTestingButtonEnabled();
    void setBkuUpdateMode(bool enabled);
    void updateBkuStartButtonState();
    void handleNormalStationConnected(const QString &ipTrimmed, bool wasInDisconnectRecovery);
    QString connectedInterfaceName() const;
    QString connectedConnectionUuid() const;
    bool ensureTftpServerIpConfigured(QString *errorText = nullptr) const;
    bool tryAssignTftpServerIpOnInterface(const QString &interfaceName, bool *addressWasAdded,
                                          QString *errorText = nullptr) const;
    bool configureTftpServerNetwork(QString *errorText, bool *addressWasAdded, bool strict,
                                  bool *networkAddressReady = nullptr) const;
    void ensureUpdateBkuUiInitialized();
    void suspendTestingSystemsForBkuMode();
    bool shouldProcessStationTestingUdp() const;
    void initStartTestingButtonGlow();
    void startStartTestingButtonGlow();
    void stopStartTestingButtonGlow();
    void applyStartTestingButtonGlowFrame(qreal progress);
    void initStatusLedGlow();
    void startStatusLedGlow();
    void setStatusLedGlowColor(QGraphicsDropShadowEffect *effect, const QString &colorName);
    void applyStatusLedGlowFrame(qreal progress);
    void startProfileIntegritySequenceAfterReboot(const QString &stationIp);
    bool verifyProfileIntegrityAfterRebootOverSsh(const QString &stationIp, QString *errorText);
    void beginPostReconnectStationBootWaitAfterProfileConnect();
    void cancelPostReconnectStationBootWait(bool restoreProgressBar);
    void tryStartPpmInitAfterPostReconnectBootGates();
    void initPpmUiStyle();
    void initPowerTestingUi();
    void initPowerTestingPlots();
    void updatePowerTestingPlots(const QVector<double> &freqs, const QVector<double> &amps);
    bool startPowerMeasurementStep();
    void finishPowerMeasurementStep();
    QString powerTestPowerKindAdjectiveForLog() const;
    QString powerTestTractDisplayNameForLog() const;
    QString receiveTestTractDisplayNameForLog() const;
    void setEmissionAnimating(bool on);
    void applyTraktParamToPpmUi(const QVector<TraktParamEntry> &entries, int traktNum);
    enum class StationHeaderCenter { StartButton, ProgressBar, FramePpm };
    void configureFrameStationHeaderLayout();
    void applyStationHeaderProgressBarLayout(bool expanded);
    void showStationHeaderCenter(StationHeaderCenter center);
    void updateMenubarVisibility();
    bool shouldKeepStationHeaderProgressVisible() const;
    /** Уже в режиме тестирования: framePPM с трактами, кнопка «НАЧАТЬ ТЕСТИРОВАНИЕ» не участвует. */
    bool isActivePpmTestingSession() const;
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
    /** TrmType (2=МВ, 3=ДМВ1, 4=ДМВ2) для TrId из конфигурации станции. */
    int ppmTrmTypeForTract(int tractNum) const;
    /** ППРЧ и тест приёма поддерживаются для TrmType 2..4 (не ДМКВ). */
    bool isFhssCapableTract(int tractNum) const;
    QVector<quint64> receiveTestFrequenciesHzForTract(int tractNum) const;
    /** Подпись выбранного тракта в framePPM (текст radio-кнопки). */
    QString selectedPpmTractDisplayNameFromUi() const;
    enum class PpmStatusStyle { Ok, Warning, Fault };
    /** Подпись IND_ERROR в labelPPMStatus и зеркальных метках на других вкладках */
    void applyPpmTransmitterLabel(const QString &statusText, PpmStatusStyle style);
    /** Рамка PPM: цвет по состояниям TRAKT_* (аналогично frame_ppm_status в пульте). */
    void applyPpmModeFrameForTract(int tractNum);
    void applyPpmModeFrameIdle();
    void setPpmFrameStateForTract(int tractNum, int state);
    /// Индикация IND_ERROR → TRAKT_* для рамок: как PpmForm::leerrorCode в ControlPanelSurs.
    void applyPpmErrorIndicationFrameLikeControlPanel(int tractNum, int16_t code, int16_t lastCode);
    void maybeRestoreDefaultDirectionForTract(int tractNum);
    void setPpmUpdateLabelVisible(bool visible);
    /** CMD_CURR_DIR_SET: установить направление тракта в выбранный DirId. */
    bool sendPpmCurrDirSet(int tractNum, uint8_t dirId, const QString &userLogMessage = QString());
    /** CMD_CURR_DIR_SET: установить направление тракта в DirId=1 («Обновить» на вкладках). */
    bool sendPpmCurrDirSetDir1(int tractNum, const QString &userLogMessage = QString());
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
    /// Интервал CMD_ECHO: на tabRecieve без теста — чаще (экран не моргает), иначе по умолчанию.
    void syncAnalyzerKeepAliveForCurrentTab();
    /// Общий прогресс теста приёма (0–100): только детерминированные шаги уровней 5 с; ожидание baseline не «раздувает» шкалу.
    int receiveTestOverallProgressPercent() const;
    void updateReceiveResultStripsVisibility();
    void resetReceiveTestUiForNewTractSelection(int targetTract);
    void pausePowerTestForPpmDisconnect();
    void pausePowerTestForStationDisconnect();
    void pausePowerTestForAntennaFault();
    void reloadDirectionAfterAntennaFault(int tractNum);
    void pausePowerTestForDirectionRestore();
    /// Отложенное авто-возобновление теста мощности после стабилизации (как после «Нет связи»→«Норма»).
    void attemptScheduleDelayedPowerTestResume(int tractNum);
    /// Вернуть рамку в TRAKT_WRK после загрузки режима, если IND_ERROR не дублировался
    /// (все «рабочие» коды при включённом тракте, как в пульте).
    /// По умолчанию проверяем DirId=1 (сценарий возврата направления), но для загрузки сложных режимов
    /// (например TMO/TMO FHSS/SR FHSS) можно отключить проверку направления.
    void syncPpmFrameForDir1IfTransmitterOk(int tractNum,
                                            bool requireNonZeroWorkMode = false,
                                            bool requireDir1 = true);
    bool isPpmTractReadyForPowerTest(int tractNum) const;
    void updatePowerTestButtonsAccessForSelectedTract();
    void updateReceiveTestButtonsAccessForSelectedTract();
    void stopReceiveTestIfTractNotReady(int tractNum);
    void pauseReceiveTestForPpmNotReady(int tractNum);
    void pauseReceiveTestForStationDisconnect();
    void pauseReceiveTestForAnalyzerDisconnect();
    void restartInterruptedReceiveLevelTest();
    void resumeReceiveLevelTestAfterPause();
    void attemptScheduleDelayedReceiveTestResume(int tractNum);
    void pausePowerTestForAnalyzerDisconnect();
    void pauseFhssForAnalyzerDisconnect();
    void applyAnalyzerHandsTabBlock();
    int resolveTabHandsIndex() const;
    bool isPreTestingHandsOnlyPhase() const;
    void leaveTabHandsIfBlocked();
    void requestRecoveryIndicationsAfterReconnect();
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
    void clearPowerMomentSpectrumPlot();
    void updatePowerLevelRadioButtonsEnabled();
    void stopAllTestsForPpmRecovery();
    void restorePreStartStateAfterExternalProfileSwitch(uint8_t profileId);
    void armSelfIssuedDirOp(int tractNum, uint8_t expectedDirId);
    void armSelfIssuedTractReload(int tractNum);
    void clearSelfIssuedGuardsForTract(int tractNum);
    void clearAllSelfIssuedGuards();
    qint64 uptimeElapsedMs() const;
    void initRuntimeTimerWidget();
    QString formatRuntimeElapsed(qint64 elapsedMs) const;
    void updateRuntimeTimerDisplay();
    /// DirId для ППРЧ по выбранному пункту modeFHSSComboBox: МПР=2, далее +1 по списку.
    uint8_t fhssExpectedDirIdFromModeCombo() const;
    void pauseFhssForPpmDisconnect();
    void pauseFhssForAntennaFault();
    void pauseFhssForExternalDirectionRestore();
    void beginFhssResumeDirectionCommand(uint8_t dirId);
    void tryFinishFhssReturnToDefaultDirection(int tractNum);

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
    void redrawFhssDisplay();
    void scheduleFhssRedrawAfterAxisChange();
    bool isSpectrumMaxHoldOn() const;
    void updateLogToggleButtonText();
    void initFhssTestingUi();
    void initFhssPlot();
    void setFhssTestControlsIdle();
    void setFhssTestControlsIdle(bool clearMaxHold);
    void setFhssTestControlsRunning(bool running);
    void updateFhssModeComboForTract(int tractNum);
    void updateFhssStartTestingButtonCaption();
    bool startFhssTransmission();
    void applyFhssXAxisForTract(int tractNum);
    /// Остановить поток, отключить alternate-режим tabPower, выставить диапазон анализатора под ППРЧ и сбросить FHSS-буферы.
    void syncFhssAnalyzerSpectrumRange(int tractNum);
    /// true, если кадр спектра покрывает ожидаемый sweep ППРЧ (а не узкий «хвост» tabPower/tabHands).
    bool isFhssSpectrumFrameValid(int tractNum, const QVector<double> &freqsMHz) const;

    /// Спецификация диапазона ППРЧ-вкладки для конкретного тракта и выбранного в modeFHSSComboBox режима.
    /// Двухграничный (isSingle=false): startHz/stopHz отображаются на LCD, plotLoHz/plotHiHz задают ось X и запрос анализатора.
    /// Одночастотный (isSingle=true): startHz — центр (на LCD), stopHz=0, plotLoHz/plotHiHz задают окно вокруг центра.
    struct FhssBandSpec {
        bool isSingle = false;
        quint64 startHz = 0;
        quint64 stopHz = 0;
        quint64 plotLoHz = 0;
        quint64 plotHiHz = 0;
    };
    FhssBandSpec currentFhssBandSpec(int tractNum) const;
    /// Перерисовать ось X/тики/LCD и обновить запрос анализатора (если активна вкладка ППРЧ) под текущий режим.
    void applyFhssBandForSelectedMode();
    /// true, если в modeFHSSComboBox выбран режим «МПР» (только для него показывается emissionAntennaWidgetFHSS).
    bool isFhssModeMpr() const;
    bool isFhssModeTmo4() const;
    void applyFhssYAxisForCurrentMode();

    /// Ось X графика ППРЧ (может быть шире диапазона запроса анализатора, напр. для «полей»).
    QPair<quint64, quint64> fhssPlotXAxisRangeHzForTract(int tractNum) const;
    /// Диапазон запроса анализатора для ППРЧ по тракту.
    QPair<quint64, quint64> fhssSpectrumRangeHzForTract(int tractNum) const;
    bool isFhssTabActive() const;
    void updateFhssRangeLcdForTract(int tractNum);
    void updateFhssTestButtonsAccessForSelectedTract();
    /// «Тест ППРЧ активен» — для блокировки остальных вкладок (как у теста мощности).
    /// Включая ожидание DirId из modeFHSSComboBox и паузы (ПП, АНТ, внешнее направление/режим).
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
    QTimer m_runtimeDisplayTimer;
    QLCDNumber *m_runtimeLcd = nullptr;
    QVector<AddedIpEntry> m_addedIps;
    bool m_cleanupDone = false;
    bool m_shutdownCleanupDone = false;

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

    // Кэш найденных IP по интерфейсам для открытия настроек без повторного сканирования.
    QHash<QString, QVector<QString>> m_cachedFoundIpsByIface;

    // Подсказка для первого сценария: яркое свечение кнопки старта после разблокировки.
    QString m_startTestingBaseStyleSheet;
    QGraphicsDropShadowEffect *m_startTestingGlowEffect = nullptr;
    QVariantAnimation *m_startTestingGlowAnimation = nullptr;
    QGraphicsDropShadowEffect *m_stationLedGlowEffect = nullptr;
    QGraphicsDropShadowEffect *m_r3LedGlowEffect = nullptr;
    QVariantAnimation *m_statusLedGlowAnimation = nullptr;

    // Подготовленный профиль для текущей станции (собирается сразу после подключения).
    QString m_preparedProfileStationIp;
    int m_stationLabelNumber = -1;
    QString m_stationHardwareVariant;
    QString m_stationLabelIp;
    /** Зафиксированная подпись «РАДИОСТАНЦИЯ №xv…» — после установки не меняется до смены IP. */
    QString m_stationLabelFixedText;
    bool m_preparingProfile = false;
    QSharedPointer<QTemporaryFile> m_preparedProfileTar;
    /// Защита от внешних переключений трактов/направлений — только после «НАЧАТЬ ТЕСТИРОВАНИЕ».
    bool m_externalSwitchProtectionArmed = false;

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
    int m_ppmIgnoreExternalPowerOffTract = -1;
    qint64 m_ppmIgnoreExternalPowerOffUntilMs = 0;
    /// Наши CMD_CURR_DIR_SET: ждём IND_ACTIVEDIR(expected), пока не подтвердим — чужие DirId не «внешние».
    struct SelfIssuedDirOp {
        uint8_t expectedDirId = 1;
        qint64 deadlineMs = 0;
    };
    QHash<int, SelfIssuedDirOp> m_selfIssuedDirOpByTract;
    /// Хвост перезагрузки тракта/шлюза после нашей смены направления или «Обновить» — не трактовать OFF/ON как внешнее.
    QHash<int, qint64> m_selfIssuedTractReloadUntilMsByTract;
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
    QCPItemText *m_powerMomentPeakLabel = nullptr;
    bool m_emissionAnimating = false;
    QVector<QCPItemRect *> m_powerGraphHelperRects;

    // tabRecieve: тест приёма с генератором анализатора
    QTimer m_receiveTestTickTimer;
    QElapsedTimer m_receiveTestElapsed;
    enum class ReceiveTestPhase { Idle, WaitBaseline, RunningLevel };
    ReceiveTestPhase m_receivePhase = ReceiveTestPhase::Idle;
    bool m_receiveTestRunning = false;
    bool m_receiveTestPaused = false;
    bool m_receiveTestAutoPausedByPpmNotReady = false; // автопауза из-за "плохого" статуса/не зелёной рамки
    bool m_receiveTestAutoPausedByAnalyzerDisconnect = false; // автопауза из-за потери связи с анализатором
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
    /// Дедупликация отложенных попыток авто-возобновления теста приёма после reconnect.
    quint64 m_receiveResumeAfterReconnectSerial = 0;
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
    bool m_powerTestBlockedByStationDisconnect = false; // пауза/блок из-за потери связи со станцией
    bool m_powerTestBlockedByAnalyzerDisconnect = false; // пауза/блок из-за потери связи с анализатором
    bool m_powerTestBlockedByAntFault = false; // тест на паузе из-за "Авария АНТ"
    bool m_powerTestBlockedByDirRestore = false; // пауза из-за внешней смены направления / возврата DirId=1
    bool m_powerTestUserStopRequested = false;   // стоп по кнопке (не путать с штатным завершением)
    bool m_ignorePowerLevelUiSignal = false;
    uint8_t m_powerLevelCode = 4; // 1=min, 4=max; по умолчанию max
    QHash<int, uint8_t> m_powerLevelCodeByTract; // trLn -> код уровня мощности (1..4)
    quint64 m_powerResumeAfterPpmSerial = 0; // отмена/дедупликация отложенного auto-resume после "Норма"
    double m_powerStepAmpAccumDbm = 0.0;
    int m_powerStepAmpSampleCount = 0;
    // Тракт, на котором выполняется тест мощности. 0 = тест не активен/таргет не задан.
    // Важно: не должен иметь "дефолтное" ненулевое значение, иначе IND_ERROR по этому тракту
    // может ошибочно переводить UI в состояние pause (play/stop) без запуска теста.
    uint8_t m_powerTestTargetTract = 0;
    int m_powerTestTargetTrmType = -1;
    QString m_powerTestMulticastAddress;

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
    bool m_fhssBlockedByAnalyzerDisconnect = false;
    bool m_fhssBlockedByAntFault = false;
    /// Пауза ППРЧ из-за внешней смены направления: ждём выбранный в modeFHSSComboBox DirId, затем auto-resume.
    bool m_fhssBlockedByDirRestore = false;
    /// После Stop ждём полной загрузки DirId=1; до этого новый FHSS-тест запускать нельзя.
    bool m_fhssReturnToDefaultDirPending = false;
    int m_fhssReturnToDefaultDirTract = -1;
    bool m_fhssPlotInitialized = false;
    SweepPlotTraces m_fhssTraces;
    QVector<double> m_fhssMemoryAmps;
    QTimer m_fhssUiTimer;
    QVector<double> m_fhssLatestFreqs;
    QVector<double> m_fhssLatestAmps;
    bool m_fhssDisplayDirty = false;
    /// Дедупликация/отмена отложенного auto-resume теста ППРЧ после «Норма».
    quint64 m_fhssResumeAfterPpmSerial = 0;

    // Временный режим после обрыва Ethernet-связи со станцией:
    //  - не блокируем tabWidget и не уводим пользователя на tabHands;
    //  - активные тесты переведены во внешнюю паузу и ждут авто-возобновления после reconnect.
    bool m_stationDisconnectRecoveryActive = false;
    UpdateBkuWidget *m_updateBkuWidget = nullptr;
    bool m_bkuUpdateMode = false;
    bool m_deferredTestingConnectInit = false;
    QTimer m_emergencyConnectRetryTimer;
    QString m_startTestingNormalText;
    /// Профиль подготовлен и станция готова — без учёта подключения анализатора.
    bool m_startTestingButtonAllowed = false;
    int m_tabWidgetLayoutIndex = -1;
    /// После обрыва связи с анализатором: tabHands заблокирована, активные тесты ждут reconnect.
    bool m_analyzerDisconnectRecoveryActive = false;

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

    /// После reconnect по контролю целостности: ждём штатную загрузку трактов на станции (ворота по индикации ВКЛ последнего).
    bool m_postReconnectStationBootWaitActive = false;
    bool m_postReconnectStationBootSshOk = false;
    bool m_postReconnectStationBootLastTractOnSeen = false;
    bool m_postReconnectStationBootFallbackUsed = false;
    /// Расчётная шкала 0..100% завершена — показываем неопределённый прогресс до ворот.
    bool m_postReconnectStationBootIndeterminateUi = false;
    int m_postReconnectStationBootLastTractNum = 0;
    int m_postReconnectStationBootTargetDurationMs = 0;
    QTimer m_postReconnectStationBootProgressTimer;
    QTimer m_postReconnectStationBootFallbackTimer;
    QElapsedTimer m_postReconnectStationBootElapsed;
};
#endif // MAINWINDOW_H
