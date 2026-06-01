#include "updatebkuwidget.h"
#include "ui_updateBKU.h"

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
    , m_flasher(new Flasher(&m_bootcmd, this))
{
    ui->setupUi(this);

    ui->editNum->setValidator(new QIntValidator(0, 255, this));
    ui->editVar->setValidator(new QIntValidator(0, 100, this));

    ui->labelPixOk1->setPixmap(QPixmap(QStringLiteral(":/x.png")));
    ui->labelPixOk2->setPixmap(QPixmap(QStringLiteral(":/x.png")));
    ui->labelPixOk3->setPixmap(QPixmap(QStringLiteral(":/x.png")));
    ui->labelPixOk4->setPixmap(QPixmap(QStringLiteral(":/x.png")));

    connect(m_flasher, &Flasher::logMessage, this, [this](const QString &message, const QString &color) {
        emit logMessage(message, color);
    });
    connect(m_flasher, &Flasher::textConfigFromUboot, this, &UpdateBkuWidget::printConfigFromUboot);
    connect(m_flasher, &Flasher::progessChanged, this, &UpdateBkuWidget::progressChanged);
    connect(m_flasher, &Flasher::transmitFinish, this, &UpdateBkuWidget::waitingConnection);
    connect(ui->pushButtonEmergency, &QPushButton::clicked, this, &UpdateBkuWidget::startEmergencyTftp);

    QDir().mkpath(FirmwareFiles::directory());
    refreshFirmwareFilesStatus();
}

UpdateBkuWidget::~UpdateBkuWidget()
{
    delete ui;
}

QString UpdateBkuWidget::updateFilesDirectory()
{
    return FirmwareFiles::directory();
}

QString UpdateBkuWidget::findFirmwareFileByPrefix(const QDir &dir, const QString &prefix)
{
    return FirmwareFiles::findByPrefix(dir, prefix);
}

bool UpdateBkuWidget::ensureCanonicalFirmwareFile(QDir &dir, const QString &prefix, const QString &canonicalName)
{
    return FirmwareFiles::ensureCanonicalFile(dir, prefix, canonicalName);
}

void UpdateBkuWidget::setStationContext(const QString &stationIp, const QString &interfaceName)
{
    m_stationIp = stationIp.trimmed();
    m_interfaceName = interfaceName.trimmed();
    const QStringList parts = m_stationIp.split('.');
    m_staNum = parts.size() >= 3 ? parts.at(2) : QString();
}

void UpdateBkuWidget::setEnsureTftpServerIpFn(EnsureTftpServerIpFn fn)
{
    m_ensureTftpServerIp = std::move(fn);
}

void UpdateBkuWidget::setExecuteCommandFn(ExecuteCommandFn fn)
{
    m_executeCommand = std::move(fn);
}

void UpdateBkuWidget::activatePanel()
{
    if (m_stationIp.isEmpty()) {
        emit logMessage(QStringLiteral("ОШИБКА: радиостанция не подключена."), QStringLiteral("red"));
        return;
    }
    loadStationInfo();
    refreshFirmwareFilesStatus();
}

void UpdateBkuWidget::deactivatePanel()
{
    if (m_updateInProgress) {
        return;
    }
}

bool UpdateBkuWidget::canStartUpdate() const
{
    if (m_updateInProgress || m_variant.isEmpty()) {
        return false;
    }

    QDir dir(updateFilesDirectory());
    return FirmwareFiles::hasRequiredFirmwareFiles(dir)
           && (!m_hasUbootFirmware || !FirmwareFiles::findByPrefix(dir, QStringLiteral("u-boot")).isEmpty());
}

void UpdateBkuWidget::refreshFirmwareFilesStatus()
{
    QDir dir(updateFilesDirectory());
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    m_loadedFiles.clear();
    for (const QString &fileName : dir.entryList(QDir::Files)) {
        if (isAllowedFirmwareFileName(fileName)) {
            m_loadedFiles.append(fileName);
        }
    }

    m_hasUbootFirmware = !findFirmwareFileByPrefix(dir, QStringLiteral("u-boot")).isEmpty();
    if (m_hasUbootFirmware) {
        m_bootcmd = QStringLiteral("/usr/sbin/fw_setenv bootcmd \"run preboot; run angstremtftp_fdt; run angstremtftp_kernel; "
                                   "run angstremtftp_rootfs; run angstremcore1_boot\"");
    } else {
        m_bootcmd = QStringLiteral("/usr/sbin/fw_setenv bootcmd \"run angstremtftp_fdt; run angstremtftp_kernel; "
                                   "run angstremtftp_rootfs; run angstremcore1_boot\"");
    }

    applyFirmwareStatusToUi();
    updateStartUpdateButtonState();
}

void UpdateBkuWidget::applyFirmwareStatusToUi()
{
    QDir dir(updateFilesDirectory());
    const auto setStatus = [this, &dir](QLabel *label, const QString &prefix) {
        if (!label) {
            return;
        }
        label->setPixmap(QPixmap(findFirmwareFileByPrefix(dir, prefix).isEmpty()
                                     ? QStringLiteral(":/x.png")
                                     : QStringLiteral(":/ok.png")));
    };

    setStatus(ui->labelPixOk1, QStringLiteral("bku-p2020"));
    setStatus(ui->labelPixOk2, QStringLiteral("rootfs"));
    setStatus(ui->labelPixOk3, QStringLiteral("kernel"));
    setStatus(ui->labelPixOk4, QStringLiteral("u-boot"));
}

void UpdateBkuWidget::updateStartUpdateButtonState()
{
    const bool enabled = canStartUpdate() && !m_updateInProgress;
    emit startUpdateButtonEnabledChanged(enabled);
    if (ui->pushButtonEmergency) {
        ui->pushButtonEmergency->setEnabled(enabled);
    }
}

void UpdateBkuWidget::setUpdateControlsEnabled(bool enabled)
{
    if (ui->pushButtonEditNum) {
        ui->pushButtonEditNum->setEnabled(enabled);
    }
    if (ui->pushButtonEditVar) {
        ui->pushButtonEditVar->setEnabled(enabled);
    }
    if (ui->pushButtonLoadFile) {
        ui->pushButtonLoadFile->setEnabled(enabled);
    }
    if (ui->editNum) {
        ui->editNum->setEnabled(enabled);
    }
    if (ui->editVar) {
        ui->editVar->setEnabled(enabled);
    }
    if (ui->checkSaveRD) {
        ui->checkSaveRD->setEnabled(enabled);
    }
    if (ui->pushButtonEmergency) {
        ui->pushButtonEmergency->setEnabled(enabled);
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

void UpdateBkuWidget::loadStationInfo()
{
    const QPair<QString, QString> content = m_flasher->getcontent(m_stationIp);
    m_variant = content.first.trimmed();

    if (m_variant.isEmpty()) {
        emit logMessage(QStringLiteral("ОБНОВЛЕНИЕ ЗАПРЕЩЕНО: конфигурация радиостанции не содержит варианта исполнения."),
                        QStringLiteral("red"));
    }

    m_blocName = blocNameForVariant(m_variant);
    ui->editNum->setText(m_staNum);
    ui->editVar->setText(m_variant.isEmpty() ? QStringLiteral("X") : m_variant);
    applyVersionOutput(content.second);
    m_flasher->getSetConfig(m_stationIp);
    applyConfigLabels();
    updateStartUpdateButtonState();
}

void UpdateBkuWidget::printConfigFromUboot(const QString &)
{
    applyConfigLabels();
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

    QDir updateDir(updateFilesDirectory());
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
}

void UpdateBkuWidget::startUpdate()
{
    if (m_updateInProgress) {
        return;
    }

    QString prepareError;
    bool addressWasAdded = false;
    if (!prepareTftpEnvironment(&prepareError, &addressWasAdded, true)) {
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

    beginUpdateSession();

    const bool saveRadioData = ui->checkSaveRD->isChecked();
    const QString stationIp = m_stationIp;
    const QString variant = m_variant;

    QtConcurrent::run([this, saveRadioData, stationIp, variant]() {
        m_flasher->startUpdating(stationIp, saveRadioData, variant);
    });
}

void UpdateBkuWidget::startEmergencyTftp()
{
    if (m_updateInProgress) {
        return;
    }

    if (m_stationIp.isEmpty()) {
        emit logMessage(QStringLiteral("ОШИБКА: радиостанция не подключена."), QStringLiteral("red"));
        return;
    }

    QString prepareError;
    bool addressWasAdded = false;
    if (!prepareTftpEnvironment(&prepareError, &addressWasAdded, false)) {
        if (!prepareError.isEmpty()) {
            emit logMessage(prepareError, QStringLiteral("red"));
        }
        return;
    }

    emit logMessage(QStringLiteral("Аварийный запуск TFTP-сервера..."), QStringLiteral("blue"));

    if (!m_flasher->checkingPort()) {
        return;
    }

    beginUpdateSession();
    emit logMessage(QStringLiteral("TFTP-сервер запущен. Ожидание загрузки файлов радиостанцией..."),
                    QStringLiteral("green"));
}

bool UpdateBkuWidget::prepareTftpEnvironment(QString *prepareError, bool *addressWasAdded, bool requireVariant)
{
    if (requireVariant && m_variant.isEmpty()) {
        if (prepareError) {
            *prepareError = QStringLiteral("ОБНОВЛЕНИЕ ЗАПРЕЩЕНО: не задан вариант исполнения.");
        }
        return false;
    }

    if (!canStartUpdate()) {
        if (prepareError) {
            *prepareError = QStringLiteral("ОШИБКА: загрузите обязательные файлы обновления.");
        }
        return false;
    }

    QDir updateDir(updateFilesDirectory());
    QString localPrepareError;
    if (!FirmwareFiles::prepareForTftp(&localPrepareError)) {
        if (prepareError) {
            *prepareError = localPrepareError.isEmpty()
                                ? QStringLiteral("ОШИБКА: не удалось подготовить файлы обновления для TFTP.")
                                : localPrepareError;
        }
        return false;
    }
    emit logMessage(QStringLiteral("Файлы обновления подготовлены: %1").arg(updateDir.absolutePath()),
                    QStringLiteral("green"));

    if (!m_ensureTftpServerIp) {
        if (prepareError) {
            *prepareError = QStringLiteral("ОШИБКА: проверка адреса TFTP-сервера недоступна.");
        }
        return false;
    }

    emit logMessage(QStringLiteral("Проверка адреса TFTP-сервера 192.168.0.15..."), QStringLiteral("blue"));
    QString networkError;
    bool addressAdded = false;
    if (!m_ensureTftpServerIp(&networkError, &addressAdded)) {
        if (prepareError) {
            *prepareError = networkError.isEmpty() ? QStringLiteral("ОШИБКА подготовки сети для TFTP.")
                                                   : networkError;
        }
        return false;
    }
    if (addressWasAdded) {
        *addressWasAdded = addressAdded;
    }
    if (addressAdded) {
        emit logMessage(QStringLiteral("В сетевое подключение добавлен serverIP: 192.168.0.15/24"),
                        QStringLiteral("green"));
    } else {
        emit logMessage(QStringLiteral("Адрес TFTP-сервера 192.168.0.15 уже настроен."), QStringLiteral("green"));
    }

    return true;
}

void UpdateBkuWidget::beginUpdateSession()
{
    disconnect(m_flasher, &Flasher::connectCompleted, this, &UpdateBkuWidget::updateStateAfterChange);
    connect(m_flasher, &Flasher::connectCompleted, this, &UpdateBkuWidget::updateStateAfterFlash);

    m_updateInProgress = true;
    setUpdateControlsEnabled(false);
    emit updateBusyChanged(true);
    emit startUpdateButtonEnabledChanged(false);
}

void UpdateBkuWidget::waitingConnection()
{
    emit progressChanged(-1);
    emit logMessage(QStringLiteral("Ожидание загрузки %1...").arg(m_blocName), QStringLiteral("blue"));
    m_flasher->startCheckConnect(m_stationIp);
}

void UpdateBkuWidget::updateStateAfterChange()
{
    loadStationInfo();
    m_updateInProgress = false;
    setUpdateControlsEnabled(true);
    emit updateBusyChanged(false);
    refreshFirmwareFilesStatus();
}

void UpdateBkuWidget::updateStateAfterFlash()
{
    const QPair<QString, QString> content = m_flasher->getcontent(m_stationIp);
    m_variant = content.first.trimmed();
    m_blocName = blocNameForVariant(m_variant);

    ui->editNum->setText(m_staNum);
    ui->editVar->setText(m_variant);
    applyVersionOutput(content.second);
    m_flasher->getSetConfig(m_stationIp);
    applyConfigLabels();

    QString blocName = m_blocName;
    m_flasher->finishUpdating(m_stationIp, false, m_variant, blocName);

    m_updateInProgress = false;
    setUpdateControlsEnabled(true);
    emit updateBusyChanged(false);
    refreshFirmwareFilesStatus();
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

    m_updateInProgress = true;
    setUpdateControlsEnabled(false);
    emit updateBusyChanged(true);
    emit startUpdateButtonEnabledChanged(false);

    if (m_flasher->changeNumStation(m_stationIp, QString::number(enteredNum))) {
        emit logMessage(QStringLiteral("Номер радиостанции №%1 изменен на «%2».").arg(m_staNum).arg(enteredNum),
                        QStringLiteral("green"));
        m_staNum = QString::number(enteredNum);
        QStringList octets = m_stationIp.split('.');
        if (octets.size() == 4) {
            octets[2] = m_staNum;
            m_stationIp = octets.join('.');
        }
        disconnect(m_flasher, &Flasher::connectCompleted, this, &UpdateBkuWidget::updateStateAfterFlash);
        connect(m_flasher, &Flasher::connectCompleted, this, &UpdateBkuWidget::updateStateAfterChange);
    }

    waitingConnection();
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

    m_updateInProgress = true;
    setUpdateControlsEnabled(false);
    emit updateBusyChanged(true);
    emit startUpdateButtonEnabledChanged(false);

    const QString stationIp = m_stationIp;
    QtConcurrent::run([this, stationIp, enteredVar]() {
        const bool success = m_flasher->changeVarStation(stationIp, enteredVar);
        QMetaObject::invokeMethod(this, [this, success, enteredVar]() {
            if (success) {
                emit logMessage(QStringLiteral("Номер варианта исполнения радиостанции №%1 изменен с «%2» на «%3».")
                                    .arg(m_staNum)
                                    .arg(m_variant)
                                    .arg(enteredVar),
                                QStringLiteral("green"));
                m_variant = QString::number(enteredVar);
                disconnect(m_flasher, &Flasher::connectCompleted, this, &UpdateBkuWidget::updateStateAfterFlash);
                connect(m_flasher, &Flasher::connectCompleted, this, &UpdateBkuWidget::updateStateAfterChange);
            } else {
                emit logMessage(QStringLiteral("Ошибка при изменении варианта исполнения радиостанции."),
                                QStringLiteral("red"));
                m_updateInProgress = false;
                setUpdateControlsEnabled(true);
                emit updateBusyChanged(false);
                refreshFirmwareFilesStatus();
            }
            QTimer::singleShot(3000, this, &UpdateBkuWidget::waitingConnection);
        }, Qt::QueuedConnection);
    });
}
