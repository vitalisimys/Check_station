#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "debug.h"
#include <QTime>
#include <QScrollBar>
#include <QPixmap>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_deviceController(new DeviceController(this))
{
    ui->setupUi(this);

    connect(m_deviceController, &DeviceController::connected,
            this, &MainWindow::onDeviceConnected);
    connect(m_deviceController, &DeviceController::disconnected,
            this, &MainWindow::onDeviceDisconnected);
    connect(m_deviceController, &DeviceController::logMessage,
            this, &MainWindow::onDeviceLogMessage);
    connect(m_deviceController, &DeviceController::errorOccurred,
            this, &MainWindow::onDeviceError);

    setStationDisconnectedUi();
    ui->frameStation->setVisible(false);
    onDeviceLogMessage("Приложение запущено. Откройте настройки и выберите станцию.");
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_actionSettings_triggered()
{
    SettingsDialog dialog(this);
    connect(&dialog, &SettingsDialog::stationConnectRequested,
            this, &MainWindow::onStationConnectRequested);
    dialog.exec();
}

void MainWindow::onStationConnectRequested(const QString &stationIp) {
    if (m_deviceController->isConnected()) {
        m_deviceController->disconnectFromDevice();
    }

    ui->frameStation->setVisible(false);
    m_deviceController->setStationIp(stationIp);
    onDeviceLogMessage(QString("Запрос подключения к станции %1").arg(stationIp));
    m_deviceController->connectToDevice();
}

void MainWindow::onDeviceConnected(const QString &ip) {
    setStationConnectedUi();
    ui->frameStation->setVisible(true);
    onDeviceLogMessage(QString("Успешное подключение к р/станции: %1").arg(ip));
}

void MainWindow::onDeviceDisconnected() {
    setStationDisconnectedUi();
    ui->frameStation->setVisible(true);
    onDeviceLogMessage("Соединение со станцией разорвано.");
}

void MainWindow::onDeviceLogMessage(const QString &msg) {
    const QString timeStr = QTime::currentTime().toString("HH:mm:ss");
    ui->logTextEdit->append(QString("[%1] %2").arg(timeStr, msg));

    QScrollBar *sb = ui->logTextEdit->verticalScrollBar();
    if (sb) {
        sb->setValue(sb->maximum());
    }
}

void MainWindow::onDeviceError(const QString &err) {
    onDeviceLogMessage(QString("ОШИБКА: %1").arg(err));
}

void MainWindow::setStationConnectedUi() {
    ui->frameStation->setStyleSheet(
        "#frameStation {"
        " color: #10b981;"
        " border-radius: 8px;"
        " border: 2px solid #10b981;"
        " font-family: \"Consolas\";"
        "}"
    );
    ui->labelPixStation->setPixmap(QPixmap(":/led_green.png"));
    ui->labelStateStation->setText("Подключена");
}

void MainWindow::setStationDisconnectedUi() {
    ui->frameStation->setStyleSheet(
        "#frameStation {"
        " color: #10b981;"
        " border-radius: 8px;"
        " border: 2px solid #ef4444;"
        " font-family: \"Consolas\";"
        "}"
    );
    ui->labelPixStation->setPixmap(QPixmap(":/led_red.png"));
    ui->labelStateStation->setText("Отключена");
}
