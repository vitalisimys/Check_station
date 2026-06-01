#include "updatebkuwidget.h"
#include "ui_updateBKU.h"

#include "debug.h"
#include "firmwarefiles.h"
#include "styles.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIntValidator>
#include <QMap>
#include <QMessageBox>
#include <QPixmap>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

namespace {

QString normalizeVersionKey(const QString &rawKey)
{
    const QString key = rawKey.trimmed().toLower();
    // Порядок важен: сначала составные ключи, иначе «Дата коммита» попадёт в commit.
    if (key.contains(QStringLiteral("дата коммита")) || key.contains(QStringLiteral("commit date"))) {
        return QStringLiteral("date");
    }
    if (key.contains(QStringLiteral("дата сбор")) || key.contains(QStringLiteral("date make"))) {
        return QStringLiteral("datemake");
    }
    if (key.contains(QStringLiteral("версия ядра")) || key.contains(QStringLiteral("version core"))
        || key.contains(QStringLiteral("versioncore"))) {
        return QStringLiteral("versioncore");
    }
    if (key == QStringLiteral("commit") || key == QStringLiteral("коммит")) {
        return QStringLiteral("commit");
    }
    if (key == QStringLiteral("branch") || key.contains(QStringLiteral("ветк"))) {
        return QStringLiteral("branch");
    }
    if (key == QStringLiteral("date") || key == QStringLiteral("дата")) {
        return QStringLiteral("date");
    }
    if (key == QStringLiteral("version") || key == QStringLiteral("версия")) {
        return QStringLiteral("version");
    }
    return key;
}

} // namespace

UpdateBkuWidget::UpdateBkuWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::UpdateBkuWidget)
    , m_flasher(new Flasher(this))
{
    ui->setupUi(this);

    ui->editNum->setValidator(new QIntValidator(0, 255, this));
    ui->editVar->setValidator(new QIntValidator(0, 100, this));

    ui->labelPixOk1->setPixmap(QPixmap(QStringLiteral(":/x.png")));
    ui->labelPixOk2->setPixmap(QPixmap(QStringLiteral(":/x.png")));
    ui->labelPixOk3->setPixmap(QPixmap(QStringLiteral(":/x.png")));
    ui->labelPixOk4->setPixmap(QPixmap(QStringLiteral(":/x.png")));
    if (ui->labelPixOkALL) {
        ui->labelPixOkALL->setPixmap(QPixmap(QStringLiteral(":/x.png")));
    }

    connect(m_flasher, &Flasher::logMessage, this, [this](const QString &message, const QString &color) {
        emit logMessage(message, color);
    });
    connect(m_flasher, &Flasher::progessChanged, this, &UpdateBkuWidget::progressChanged);
    connect(m_flasher, &Flasher::transmitFinish, this, &UpdateBkuWidget::waitingConnection);
    // Единственный получатель connectCompleted — диспетчер, который смотрит на m_pendingOp.
    connect(m_flasher, &Flasher::connectCompleted, this, &UpdateBkuWidget::onConnectCompleted);
    connect(m_flasher, &Flasher::updateFailed, this, &UpdateBkuWidget::onUpdateFailed);
    connect(ui->pushButtonEmergency, &QPushButton::clicked, this, &UpdateBkuWidget::startEmergencyTftp);

    QDir().mkpath(FirmwareFiles::directory());
    refreshFirmwareFilesStatus();
    applyConnectionDependentControls();
}

UpdateBkuWidget::~UpdateBkuWidget()
{
    delete ui;
}

void UpdateBkuWidget::setStationContext(const QString &stationIp, const QString &interfaceName)
{
    m_stationIp = stationIp.trimmed();
    m_interfaceName = interfaceName.trimmed();
    const QStringList parts = m_stationIp.split('.');
    m_staNum = parts.size() >= 3 ? parts.at(2) : QString();
    applyConnectionDependentControls();
    updateStartUpdateButtonState();
}

void UpdateBkuWidget::setStationLinkActive(bool reachable)
{
    m_stationReachable = reachable;
    applyConnectionDependentControls();
    updateStartUpdateButtonState();
}

void UpdateBkuWidget::setEnsureTftpServerIpFn(EnsureTftpServerIpFn fn)
{
    m_ensureTftpServerIp = std::move(fn);
}

void UpdateBkuWidget::activatePanel()
{
    refreshFirmwareFilesStatus();
    logFirmwareFilesStatus();
    applyConnectionDependentControls();

    if (m_stationIp.isEmpty()) {
        emit logMessage(QStringLiteral("Радиостанция не подключена. Доступны загрузка файлов и аварийный запуск TFTP."),
                        QStringLiteral("blue"));
        return;
    }
    loadStationInfoAsync();
}

void UpdateBkuWidget::deactivatePanel()
{
    // Зарезервировано для будущей очистки состояния. Сейчас активного бэкграунда,
    // который надо было бы прерывать вручную, у виджета нет.
}

bool UpdateBkuWidget::hasFirmwareReadyForTftp() const
{
    QDir dir(FirmwareFiles::directory());
    return FirmwareFiles::hasRequiredFirmwareFiles(dir)
           && (!m_hasUbootFirmware || !FirmwareFiles::findByPrefix(dir, QStringLiteral("u-boot")).isEmpty());
}

bool UpdateBkuWidget::canStartUpdate() const
{
    if (m_updateInProgress || m_variant.isEmpty()) {
        return false;
    }
    return hasFirmwareReadyForTftp();
}

void UpdateBkuWidget::rebuildBootcmd()
{
    if (m_hasUbootFirmware) {
        m_bootcmd = QStringLiteral("/usr/sbin/fw_setenv bootcmd \"run preboot; run angstremtftp_fdt; run angstremtftp_kernel; "
                                   "run angstremtftp_rootfs; run angstremcore1_boot\"");
    } else {
        m_bootcmd = QStringLiteral("/usr/sbin/fw_setenv bootcmd \"run angstremtftp_fdt; run angstremtftp_kernel; "
                                   "run angstremtftp_rootfs; run angstremcore1_boot\"");
    }
}

void UpdateBkuWidget::refreshFirmwareFilesStatus()
{
    QDir dir(FirmwareFiles::directory());
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    m_loadedFiles.clear();
    for (const QString &fileName : dir.entryList(QDir::Files)) {
        if (isAllowedFirmwareFileName(fileName)) {
            m_loadedFiles.append(fileName);
        }
    }

    m_hasUbootFirmware = !FirmwareFiles::findByPrefix(dir, QStringLiteral("u-boot")).isEmpty();
    rebuildBootcmd();

    applyFirmwareStatusToUi();
    updateStartUpdateButtonState();
}

void UpdateBkuWidget::applyFirmwareStatusToUi()
{
    QDir dir(FirmwareFiles::directory());
    const auto setStatus = [&dir](QLabel *label, const QString &prefix) {
        if (!label) {
            return;
        }
        label->setPixmap(QPixmap(FirmwareFiles::findByPrefix(dir, prefix).isEmpty()
                                     ? QStringLiteral(":/x.png")
                                     : QStringLiteral(":/ok.png")));
    };

    setStatus(ui->labelPixOk1, QStringLiteral("bku-p2020"));
    setStatus(ui->labelPixOk2, QStringLiteral("rootfs"));
    setStatus(ui->labelPixOk3, QStringLiteral("kernel"));
    setStatus(ui->labelPixOk4, QStringLiteral("u-boot"));

    const bool allRequired = FirmwareFiles::hasRequiredFirmwareFiles(dir);
    if (ui->labelPixOkALL) {
        ui->labelPixOkALL->setPixmap(QPixmap(allRequired ? QStringLiteral(":/ok.png")
                                                         : QStringLiteral(":/x.png")));
    }
    if (ui->frameLoadFirmware) {
        ui->frameLoadFirmware->setStyleSheet(allRequired
            ? QStringLiteral("#frameLoadFirmware {\n"
                             "    border: 1px solid #4ade80;\n"
                             "    border-radius: 8px;\n"
                             "    background-color: #0b1220;\n"
                             "}")
            : QStringLiteral("#frameLoadFirmware {\n"
                             "    border: 1px solid #1f2a44;\n"
                             "    border-radius: 8px;\n"
                             "    background-color: #0b1220;\n"
                             "}"));
    }
}

void UpdateBkuWidget::updateStartUpdateButtonState()
{
    const bool enabled = canStartUpdate() && !m_updateInProgress;
    emit startUpdateButtonEnabledChanged(enabled);
    if (ui->pushButtonEmergency) {
        ui->pushButtonEmergency->setEnabled(!m_updateInProgress);
    }
}

void UpdateBkuWidget::applyConnectionDependentControls()
{
    const bool stationLinked = m_stationReachable && !m_updateInProgress;
    if (ui->pushButtonEditNum) {
        ui->pushButtonEditNum->setEnabled(stationLinked);
    }
    if (ui->pushButtonEditVar) {
        ui->pushButtonEditVar->setEnabled(stationLinked);
    }
    if (ui->editNum) {
        ui->editNum->setEnabled(stationLinked);
    }
    if (ui->editVar) {
        ui->editVar->setEnabled(stationLinked);
    }
    if (ui->checkSaveRD) {
        ui->checkSaveRD->setEnabled(stationLinked);
    }
    if (ui->pushButtonLoadFile) {
        ui->pushButtonLoadFile->setEnabled(!m_updateInProgress);
    }
}

void UpdateBkuWidget::setUpdateControlsEnabled(bool enabled)
{
    if (enabled) {
        applyConnectionDependentControls();
    } else {
        if (ui->pushButtonEditNum) {
            ui->pushButtonEditNum->setEnabled(false);
        }
        if (ui->pushButtonEditVar) {
            ui->pushButtonEditVar->setEnabled(false);
        }
        if (ui->pushButtonLoadFile) {
            ui->pushButtonLoadFile->setEnabled(false);
        }
        if (ui->editNum) {
            ui->editNum->setEnabled(false);
        }
        if (ui->editVar) {
            ui->editVar->setEnabled(false);
        }
        if (ui->checkSaveRD) {
            ui->checkSaveRD->setEnabled(false);
        }
    }
    if (ui->pushButtonEmergency) {
        ui->pushButtonEmergency->setEnabled(false);
    }
}

QString UpdateBkuWidget::presenceText(bool present) const
{
    return present ? QStringLiteral("в составе") : QStringLiteral("отсутствует");
}

QString UpdateBkuWidget::blocNameForVariant(const QString &variantValue) const
{
    if (variantValue == QStringLiteral("21") || variantValue == QStringLiteral("23")) {
        return QStringLiteral("Блока управления");
    }
    if (variantValue == QStringLiteral("16") || variantValue == QStringLiteral("18")
        || variantValue == QStringLiteral("19") || variantValue == QStringLiteral("20")
        || variantValue == QStringLiteral("22") || variantValue == QStringLiteral("25")) {
        return QStringLiteral("БКИ");
    }
    return QStringLiteral("БКУ");
}

void UpdateBkuWidget::applyVersionOutput(const QString &output)
{
    QMap<QString, QString> values;
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        QString trimmedLine = line.trimmed();
        if (trimmedLine.startsWith('-')) {
            trimmedLine = trimmedLine.mid(1).trimmed();
        }
        const int colonIndex = trimmedLine.indexOf(':');
        if (colonIndex <= 0) {
            continue;
        }
        const QString key = normalizeVersionKey(trimmedLine.left(colonIndex));
        const QString value = trimmedLine.mid(colonIndex + 1).trimmed();
        values.insert(key, value);
    }

    const auto valueOrDash = [&values](const QString &key) {
        return values.value(key, QStringLiteral("—"));
    };

    ui->labelVersionCommitValue->setText(valueOrDash(QStringLiteral("commit")));
    ui->labelVersionBranchValue->setText(valueOrDash(QStringLiteral("branch")));
    ui->labelVersionDateValue->setText(valueOrDash(QStringLiteral("date")));
    ui->labelVersionVersionValue->setText(valueOrDash(QStringLiteral("version")));
    ui->labelVersionCoreValue->setText(valueOrDash(QStringLiteral("versioncore")));
    ui->labelVersionDateMakeValue->setText(valueOrDash(QStringLiteral("datemake")));
}

void UpdateBkuWidget::applyConfigLabels()
{
    const PPMConfig &config = m_flasher->currentConfig();
    QMap<int, int> typeMaxCntMap;
    for (const PPM &ppm : config.ppms) {
        typeMaxCntMap[ppm.type] = qMax(typeMaxCntMap.value(ppm.type, 0), ppm.cnt);
    }

    const auto bandText = [&typeMaxCntMap](int type) {
        if (!typeMaxCntMap.contains(type)) {
            return QStringLiteral("—");
        }
        return QString::number(typeMaxCntMap.value(type));
    };

    ui->labelConfBandDmv1Value->setText(bandText(3));
    ui->labelConfBandMvValue->setText(bandText(2));
    ui->labelConfBandDkmvValue->setText(bandText(1));
    ui->labelConfBandDmv2Value->setText(bandText(4));
    ui->labelConfBranchValue->setText(presenceText(config.bi));
    ui->labelConfDateValue->setText(presenceText(config.f_mv_3k));
    ui->labelConfVersionValue->setText(presenceText(config.f_dmv1_2k));
    ui->labelConfVersionCoreValue->setText(presenceText(config.f_dmv2_2k));
    ui->labelConfDateMakeValue->setText(presenceText(config.gnss));
}

void UpdateBkuWidget::loadStationInfoAsync(std::function<void()> onDone)
{
    if (m_updateInProgress && m_pendingOp == PendingOp::EmergencyTftp) {
        if (onDone) {
            onDone();
        }
        return;
    }

    if (m_stationIp.isEmpty()) {
        if (onDone) {
            onDone();
        }
        return;
    }

    QPointer<UpdateBkuWidget> self(this);
    const QString stationIp = m_stationIp;
    QtConcurrent::run([self, stationIp, onDone]() {
        if (!self) {
            return;
        }
        // SSH-вызовы делаем в worker thread (UI остаётся отзывчивым),
        // m_flasher защищён собственным QMutex от параллельного доступа.
        const QPair<QString, QString> content = self->m_flasher->getcontent(stationIp);
        // currentConfig() обновляется через getSetConfig(); тоже SSH, тоже в worker.
        self->m_flasher->getSetConfig(stationIp);

        QMetaObject::invokeMethod(qApp, [self, content, onDone]() {
            if (!self) {
                return;
            }
            self->m_variant = content.first.trimmed();
            if (self->m_variant.isEmpty()) {
                emit self->logMessage(
                    QStringLiteral("ОБНОВЛЕНИЕ ЗАПРЕЩЕНО: конфигурация радиостанции не содержит варианта исполнения."),
                    QStringLiteral("red"));
            }
            self->m_blocName = self->blocNameForVariant(self->m_variant);
            self->ui->editNum->setText(self->m_staNum);
            self->ui->editVar->setText(self->m_variant.isEmpty() ? QStringLiteral("X") : self->m_variant);
            self->applyVersionOutput(content.second);
            self->applyConfigLabels();
            self->updateStartUpdateButtonState();
            if (onDone) {
                onDone();
            }
        }, Qt::QueuedConnection);
    });
}

bool UpdateBkuWidget::isAllowedFirmwareFileName(const QString &fileName) const
{
    const QString lowerName = fileName.toLower();
    for (const QString &prefix : m_allowedFilePrefixes) {
        if (lowerName == prefix || lowerName.startsWith(prefix + QLatin1Char('.'))) {
            return true;
        }
    }
    return false;
}

QString UpdateBkuWidget::canonicalFirmwareName(const QString &fileName) const
{
    const QString lowerName = fileName.toLower();
    if (lowerName.startsWith(QStringLiteral("bku-p2020"))) {
        return QStringLiteral("bku-p2020.dtb");
    }
    if (lowerName.startsWith(QStringLiteral("rootfs"))) {
        return QStringLiteral("rootfs.bin");
    }
    if (lowerName.startsWith(QStringLiteral("kernel"))) {
        return QStringLiteral("kernel.bin");
    }
    if (lowerName.startsWith(QStringLiteral("u-boot"))) {
        return QStringLiteral("u-boot-spi-spl.bin");
    }
    return fileName;
}

bool UpdateBkuWidget::loadFile(const QString &filePath, bool clearExistingOnFirstLoad)
{
    if (!QFile::exists(filePath)) {
        emit logMessage(QStringLiteral("Файл не найден: %1").arg(filePath), QStringLiteral("red"));
        return false;
    }

    const QString sourceName = QFileInfo(filePath).fileName();
    if (!isAllowedFirmwareFileName(sourceName)) {
        emit logMessage(QStringLiteral("Загрузка файла '%1' запрещена.").arg(sourceName), QStringLiteral("red"));
        return false;
    }

    QDir updateDir(FirmwareFiles::directory());
    if (!updateDir.exists() && !updateDir.mkpath(QStringLiteral("."))) {
        emit logMessage(QStringLiteral("Не удалось создать директорию update_files/"), QStringLiteral("red"));
        return false;
    }

    if (clearExistingOnFirstLoad && m_loadedFiles.isEmpty() && !updateDir.entryList(QDir::Files).isEmpty()) {
        for (const QString &file : updateDir.entryList(QDir::Files)) {
            QFile::remove(updateDir.absoluteFilePath(file));
        }
        m_loadedFiles.clear();
    }

    const QString destinationName = canonicalFirmwareName(sourceName);
    const QString destinationPath = updateDir.absoluteFilePath(destinationName);
    if (QFile::exists(destinationPath)) {
        QFile::remove(destinationPath);
    }

    if (!QFile::copy(filePath, destinationPath)) {
        emit logMessage(QStringLiteral("Не удалось скопировать файл '%1' в update_files/").arg(sourceName),
                        QStringLiteral("red"));
        return false;
    }

    if (!m_loadedFiles.contains(destinationName)) {
        m_loadedFiles.append(destinationName);
    }

    return true;
}

void UpdateBkuWidget::on_pushButtonLoadFile_clicked()
{
    const QStringList filePaths = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("Выберите файлы для загрузки"),
        QDir::homePath(),
        QStringLiteral("Файлы (*.dtb *.bin);;Все файлы (*)"));

    if (filePaths.isEmpty()) {
        return;
    }

    bool firstFile = m_loadedFiles.isEmpty();
    for (const QString &filePath : filePaths) {
        loadFile(filePath, firstFile);
        firstFile = false;
    }

    refreshFirmwareFilesStatus();
    logFirmwareFilesStatus(true);
}

void UpdateBkuWidget::logFirmwareFilesStatus(bool forceLog)
{
    QDir dir(FirmwareFiles::directory());

    struct FirmwareEntry {
        QString displayName;
        QString prefix;
        bool required;
    };
    const FirmwareEntry entries[] = {
        {QStringLiteral("bku-p2020.dtb"), QStringLiteral("bku-p2020"), true},
        {QStringLiteral("kernel.bin"), QStringLiteral("kernel"), true},
        {QStringLiteral("rootfs.bin"), QStringLiteral("rootfs"), true},
        {QStringLiteral("u-boot-spi-spl.bin"), QStringLiteral("u-boot"), false},
    };

    QStringList loaded;
    QStringList missingRequired;

    for (const FirmwareEntry &entry : entries) {
        if (FirmwareFiles::findByPrefix(dir, entry.prefix).isEmpty()) {
            if (entry.required) {
                missingRequired.append(entry.displayName);
            }
        } else {
            loaded.append(entry.displayName);
        }
    }

    QString message;
    const bool allRequired = missingRequired.isEmpty();
    const QString loadedList = loaded.join(QStringLiteral(", "));
    const QString missingList = missingRequired.join(QStringLiteral(", "));

    if (allRequired && !loaded.isEmpty()) {
        message = QStringLiteral("Файлы обновления загружены. БКУ готов к обновлению (Нажмите ОБНОВИТЬ БКУ)");
    } else if (loaded.isEmpty()) {
        message = QStringLiteral("Для начала обновления БКУ загрузите следующие файлы: %1").arg(missingList);
    } else {
        message = QStringLiteral("Файлы %1 загружены, для начала обновления БКУ загрузите следующие файлы: %2")
                      .arg(loadedList, missingList);
    }

    const QString color = allRequired ? QStringLiteral("green") : QStringLiteral("red");
    if (!forceLog && message == m_lastLoggedFirmwareStatus) {
        return;
    }
    m_lastLoggedFirmwareStatus = message;
    emit logMessage(message, color);
}

void UpdateBkuWidget::startUpdate()
{
    if (m_updateInProgress) {
        return;
    }

    QString prepareError;
    if (!prepareTftpEnvironment(&prepareError, true, true)) {
        if (!prepareError.isEmpty()) {
            emit logMessage(prepareError, QStringLiteral("red"));
        }
        return;
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(QStringLiteral("Внимание"));
    msgBox.setText(QStringLiteral("Вы уверены что хотите обновить ПО %1 с конфигурацией:\n"
                                  "Номер радиостанции: «%2»\n"
                                  "Вариант исполнения: «%3»")
                       .arg(m_blocName, m_staNum, m_variant));
    msgBox.setIcon(QMessageBox::Warning);

    QPushButton *updateButton = new QPushButton(QStringLiteral("Обновить"), &msgBox);
    QPushButton *cancelButton = new QPushButton(QStringLiteral("Отмена"), &msgBox);
    updateButton->setStyleSheet(stylesheetButtonMessBox);
    cancelButton->setStyleSheet(stylesheetButtonMessBox);
    msgBox.addButton(updateButton, QMessageBox::AcceptRole);
    msgBox.addButton(cancelButton, QMessageBox::RejectRole);
    msgBox.setStyleSheet(stylesheetMessBox);

    if (msgBox.exec() != QMessageBox::AcceptRole) {
        return;
    }

    if (!m_flasher->checkingPort()) {
        return;
    }

    beginUpdateSession(PendingOp::AfterFlash);

    const bool saveRadioData = ui->checkSaveRD->isChecked();
    const QString stationIp = m_stationIp;
    const QString variant = m_variant;
    const QString bootcmd = m_bootcmd; // копия — нельзя ссылаться на UI-поле из worker.

    QPointer<UpdateBkuWidget> self(this);
    QtConcurrent::run([self, saveRadioData, stationIp, variant, bootcmd]() {
        if (!self) {
            return;
        }
        self->m_flasher->startUpdating(stationIp, saveRadioData, variant, bootcmd);
    });
}

void UpdateBkuWidget::startEmergencyTftp()
{
    if (m_updateInProgress) {
        return;
    }

    QString prepareError;
    if (!prepareTftpEnvironment(&prepareError, false, false)) {
        if (!prepareError.isEmpty()) {
            emit logMessage(prepareError, QStringLiteral("red"));
        }
        return;
    }

    emit logMessage(QStringLiteral("Аварийный запуск TFTP-сервера..."), QStringLiteral("blue"));

    if (!m_flasher->checkingPort()) {
        return;
    }

    beginUpdateSession(PendingOp::EmergencyTftp);
    emit logMessage(QStringLiteral("TFTP-сервер запущен. Ожидание загрузки файлов радиостанцией..."),
                    QStringLiteral("green"));
}

bool UpdateBkuWidget::prepareTftpEnvironment(QString *prepareError, bool requireVariant, bool requireNetwork)
{
    if (requireVariant && m_variant.isEmpty()) {
        if (prepareError) {
            *prepareError = QStringLiteral("ОБНОВЛЕНИЕ ЗАПРЕЩЕНО: не задан вариант исполнения.");
        }
        return false;
    }

    if (!hasFirmwareReadyForTftp()) {
        if (prepareError) {
            *prepareError = QStringLiteral("ОШИБКА: загрузите обязательные файлы обновления.");
        }
        return false;
    }

    QString localPrepareError;
    if (!FirmwareFiles::prepareForTftp(&localPrepareError)) {
        if (prepareError) {
            *prepareError = localPrepareError.isEmpty()
                                ? QStringLiteral("ОШИБКА: не удалось подготовить файлы обновления для TFTP.")
                                : localPrepareError;
        }
        return false;
    }

    if (!m_ensureTftpServerIp) {
        if (requireNetwork) {
            if (prepareError) {
                *prepareError = QStringLiteral("ОШИБКА: проверка адреса TFTP-сервера недоступна.");
            }
            return false;
        }
        return true;
    }

    if (debug && requireNetwork) {
        emit logMessage(QStringLiteral("Проверка адреса TFTP-сервера 192.168.0.15..."), QStringLiteral("blue"));
    }
    QString networkError;
    bool addressAdded = false;
    bool networkAddressReady = false;
    const bool networkOk =
        m_ensureTftpServerIp(&networkError, &addressAdded, requireNetwork, &networkAddressReady);
    if (!networkOk) {
        if (prepareError) {
            *prepareError = networkError.isEmpty() ? QStringLiteral("ОШИБКА подготовки сети для TFTP.")
                                                   : networkError;
        }
        return false;
    }
    if (addressAdded) {
        emit logMessage(QStringLiteral("В сетевое подключение добавлен serverIP: 192.168.0.15/24"),
                        QStringLiteral("green"));
    } else if (debug && requireNetwork) {
        emit logMessage(QStringLiteral("Адрес TFTP-сервера 192.168.0.15 уже настроен."), QStringLiteral("green"));
    } else if (!requireNetwork && !networkAddressReady) {
        emit logMessage(QStringLiteral("Предупреждение: 192.168.0.15 не назначен автоматически. "
                                      "При необходимости укажите его на ethernet-интерфейсе, подключённом к станции. "
                                      "TFTP-сервер будет запущен."),
                        QStringLiteral("yellow"));
    }

    return true;
}

void UpdateBkuWidget::beginUpdateSession(PendingOp pending)
{
    m_pendingOp = pending;
    m_updateInProgress = true;
    if (pending == PendingOp::EmergencyTftp && m_flasher) {
        m_flasher->stopCheckConnect();
    }
    setUpdateControlsEnabled(false);
    emit updateBusyChanged(true);
    emit startUpdateButtonEnabledChanged(false);
}

void UpdateBkuWidget::finishUpdateSession()
{
    m_pendingOp = PendingOp::None;
    m_updateInProgress = false;
    setUpdateControlsEnabled(true);
    emit updateBusyChanged(false);
    refreshFirmwareFilesStatus();
}

void UpdateBkuWidget::waitingConnection()
{
    emit progressChanged(-1);

    if (m_pendingOp == PendingOp::EmergencyTftp) {
        if (m_flasher) {
            m_flasher->stopTftpServer();
        }
        emit logMessage(QStringLiteral("Аварийное обновление: передача файлов по TFTP завершена. "
                                      "Дождитесь перезагрузки радиостанции. "
                                      "Для проверки результата подключите станцию в режиме тестирования."),
                        QStringLiteral("green"));
        finishUpdateSession();
        return;
    }

    emit logMessage(QStringLiteral("Ожидание загрузки %1...").arg(m_blocName), QStringLiteral("blue"));
    m_flasher->startCheckConnect(m_stationIp);
}

void UpdateBkuWidget::onConnectCompleted()
{
    if (m_pendingOp == PendingOp::EmergencyTftp || m_pendingOp == PendingOp::None) {
        return;
    }

    switch (m_pendingOp) {
    case PendingOp::AfterFlash: {
        // Завершение полной прошивки: считываем содержимое, применяем UI,
        // прогоняем finishUpdating и закрываем сессию.
        QPointer<UpdateBkuWidget> self(this);
        const QString stationIp = m_stationIp;
        QtConcurrent::run([self, stationIp]() {
            if (!self) {
                return;
            }
            const QPair<QString, QString> content = self->m_flasher->getcontent(stationIp);
            self->m_flasher->getSetConfig(stationIp);

            QString variantTrimmed = content.first.trimmed();
            QString blocName = self->blocNameForVariant(variantTrimmed);
            self->m_flasher->finishUpdating(stationIp, false, variantTrimmed, blocName);

            QMetaObject::invokeMethod(qApp, [self, content, variantTrimmed, blocName]() {
                if (!self) {
                    return;
                }
                self->m_variant = variantTrimmed;
                self->m_blocName = blocName;
                self->ui->editNum->setText(self->m_staNum);
                self->ui->editVar->setText(self->m_variant);
                self->applyVersionOutput(content.second);
                self->applyConfigLabels();
                self->finishUpdateSession();
            }, Qt::QueuedConnection);
        });
        break;
    }
    case PendingOp::AfterChange: {
        // После смены номера/варианта: просто обновляем UI и заканчиваем сессию.
        QPointer<UpdateBkuWidget> self(this);
        loadStationInfoAsync([self]() {
            if (!self) {
                return;
            }
            self->finishUpdateSession();
        });
        break;
    }
    case PendingOp::EmergencyTftp:
        break;
    case PendingOp::None:
        // Чужое срабатывание — игнорируем, не дёргаем UI.
        break;
    }
}

void UpdateBkuWidget::onUpdateFailed(const QString &errorText)
{
    // Останавливаем TFTP-сервер и возвращаем UI в рабочее состояние,
    // чтобы оператор не остался с заблокированными кнопками после ошибки SSH.
    if (m_flasher) {
        m_flasher->stopTftpServer();
    }
    if (!errorText.isEmpty()) {
        emit logMessage(errorText, QStringLiteral("red"));
    }
    finishUpdateSession();
}

void UpdateBkuWidget::on_pushButtonEditNum_clicked()
{
    bool ok = false;
    const int enteredNum = ui->editNum->text().trimmed().toInt(&ok);
    if (!ok || enteredNum < 0 || enteredNum > 255) {
        emit logMessage(QStringLiteral("Номер радиостанции должен быть целым числом в диапазоне от 0 до 255"),
                        QStringLiteral("red"));
        return;
    }

    if (QString::number(enteredNum) == m_staNum) {
        emit logMessage(QStringLiteral("Введенный новый номер радиостанции - «%1» не отличается от текущего номера - «%2»")
                            .arg(enteredNum)
                            .arg(m_staNum),
                        QStringLiteral("red"));
        return;
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(QStringLiteral("Внимание"));
    msgBox.setText(QStringLiteral("Вы уверены, что хотите изменить номер радиостанции с «%1» на «%2»?<br>"
                                  "Для применения изменений радиостанция будет перезагружена.")
                       .arg(m_staNum, ui->editNum->text()));
    msgBox.setIcon(QMessageBox::Warning);

    QPushButton *changeButton = new QPushButton(QStringLiteral("Изменить номер"), &msgBox);
    QPushButton *cancelButton = new QPushButton(QStringLiteral("Отмена"), &msgBox);
    changeButton->setStyleSheet(stylesheetButtonMessBox);
    cancelButton->setStyleSheet(stylesheetButtonMessBox);
    msgBox.addButton(changeButton, QMessageBox::AcceptRole);
    msgBox.addButton(cancelButton, QMessageBox::RejectRole);
    msgBox.setStyleSheet(stylesheetMessBox);

    if (msgBox.exec() != QMessageBox::AcceptRole) {
        return;
    }

    beginUpdateSession(PendingOp::AfterChange);

    const QString stationIp = m_stationIp;
    const QString staNumBefore = m_staNum;
    const QString newNum = QString::number(enteredNum);

    QPointer<UpdateBkuWidget> self(this);
    QtConcurrent::run([self, stationIp, staNumBefore, newNum]() {
        if (!self) {
            return;
        }
        const bool success = self->m_flasher->changeNumStation(stationIp, newNum);
        QMetaObject::invokeMethod(qApp, [self, staNumBefore, newNum, success]() {
            if (!self) {
                return;
            }
            if (success) {
                emit self->logMessage(QStringLiteral("Номер радиостанции №%1 изменен на «%2».")
                                          .arg(staNumBefore, newNum),
                                      QStringLiteral("green"));
                self->m_staNum = newNum;
                QStringList octets = self->m_stationIp.split('.');
                if (octets.size() == 4) {
                    octets[2] = self->m_staNum;
                    self->m_stationIp = octets.join('.');
                }
                self->waitingConnection();
            } else {
                emit self->logMessage(QStringLiteral("Ошибка при изменении номера радиостанции."),
                                      QStringLiteral("red"));
                self->finishUpdateSession();
            }
        }, Qt::QueuedConnection);
    });
}

void UpdateBkuWidget::on_pushButtonEditVar_clicked()
{
    bool ok = false;
    const uint enteredVar = ui->editVar->text().trimmed().toUInt(&ok);
    if (!ok || enteredVar > 25 || enteredVar == 11 || enteredVar == 12 || enteredVar == 13 || enteredVar == 24) {
        emit logMessage(QStringLiteral("Вариант исполнения радиостанции должен быть целым числом в диапазоне от 0 до 25, "
                                      "за исключением 11,12,13,24"),
                        QStringLiteral("red"));
        return;
    }

    if (QString::number(enteredVar) == m_variant) {
        emit logMessage(QStringLiteral("Введенный вариант исполнения радиостанции №%1 - «%2» не отличается от текущего "
                                      "варианта - «%3»")
                            .arg(m_staNum)
                            .arg(enteredVar)
                            .arg(m_variant),
                        QStringLiteral("red"));
        return;
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(QStringLiteral("Внимание"));
    msgBox.setText(QStringLiteral("Вы уверены, что хотите изменить вариант исполнения радиостанции №%1 с «%2» на «%3»?\n"
                                  "Радиоданные будут удалены.\n"
                                  "Перед началом сохраните их через «СПО Пульт оператора»\n"
                                  "Для применения изменений радиостанция будет перезагружена.")
                       .arg(m_staNum, m_variant, ui->editVar->text()));
    msgBox.setIcon(QMessageBox::Warning);

    QPushButton *changeButton = new QPushButton(QStringLiteral("Изменить вариант"), &msgBox);
    QPushButton *cancelButton = new QPushButton(QStringLiteral("Отмена"), &msgBox);
    changeButton->setStyleSheet(stylesheetButtonMessBox);
    cancelButton->setStyleSheet(stylesheetButtonMessBox);
    msgBox.addButton(changeButton, QMessageBox::AcceptRole);
    msgBox.addButton(cancelButton, QMessageBox::RejectRole);
    msgBox.setStyleSheet(stylesheetMessBox);

    if (msgBox.exec() != QMessageBox::AcceptRole) {
        return;
    }

    beginUpdateSession(PendingOp::AfterChange);

    const QString stationIp = m_stationIp;
    const QString staNumBefore = m_staNum;
    const QString variantBefore = m_variant;
    QPointer<UpdateBkuWidget> self(this);
    QtConcurrent::run([self, stationIp, enteredVar, staNumBefore, variantBefore]() {
        if (!self) {
            return;
        }
        const bool success = self->m_flasher->changeVarStation(stationIp, enteredVar);
        QMetaObject::invokeMethod(qApp, [self, success, enteredVar, staNumBefore, variantBefore]() {
            if (!self) {
                return;
            }
            if (success) {
                emit self->logMessage(QStringLiteral("Номер варианта исполнения радиостанции №%1 изменен с «%2» на «%3».")
                                          .arg(staNumBefore)
                                          .arg(variantBefore)
                                          .arg(enteredVar),
                                      QStringLiteral("green"));
                self->m_variant = QString::number(enteredVar);
                QTimer::singleShot(3000, self.data(), &UpdateBkuWidget::waitingConnection);
            } else {
                emit self->logMessage(QStringLiteral("Ошибка при изменении варианта исполнения радиостанции."),
                                      QStringLiteral("red"));
                self->finishUpdateSession();
            }
        }, Qt::QueuedConnection);
    });
}
