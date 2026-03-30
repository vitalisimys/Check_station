#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include <QNetworkInterface>
#include <QProcess>
#include <QtConcurrent>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SettingsDialog)
    , m_finder(new FindManager(this))
    , m_interfacesLoaded(false)
{
    ui->setupUi(this);

    // Настройка networkComboBox: сначала элементы, потом placeholder
    if (loadNetworkInterfaces()) {
        ui->networkComboBox->setCurrentIndex(-1);
        ui->networkComboBox->setPlaceholderText("Выберите сетевой интерфейс подключения радиостанции");
    } else {
        ui->networkComboBox->setPlaceholderText("Сетевые интерфейсы не найдены.");
    }


    // Настройка findStationComboBox
    ui->findStationComboBox->setCurrentIndex(-1);
    ui->findStationComboBox->setPlaceholderText("Ожидание сканирования...");

    // Коннект на изменение интерфейса
    connect(ui->networkComboBox, &QComboBox::currentTextChanged,
            this, &SettingsDialog::onNetworkInterfaceChanged);
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

QString SettingsDialog::selectedInterface() const {
    return ui->networkComboBox->currentText();
}

QString SettingsDialog::selectedStationIp() const {
    return ui->findStationComboBox->currentText();
}

bool SettingsDialog::loadNetworkInterfaces() {
    // Получаем список Ethernet-интерфейсов через QNetworkInterface
    // Это кроссплатформенно и не требует nmcli
    bool extInt = false;
    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        // Фильтр: только up, running, ethernet, без loopback
        if (!(interface.flags() & QNetworkInterface::IsUp) ||
            !(interface.flags() & QNetworkInterface::IsRunning) ||
            interface.flags() & QNetworkInterface::IsLoopBack) {
            continue;
        }
        if (interface.hardwareAddress().isEmpty()) {
            continue; // Пропускаем виртуальные интерфейсы без MAC
        }

        // Проверяем, что это Ethernet (опционально, по названию)
        if (interface.name().startsWith("eth") ||
            interface.name().startsWith("en") ||
            interface.name().startsWith("wlan")) {
            ui->networkComboBox->addItem(interface.name());
            extInt = true;
        }
    }

    return extInt;
}

void SettingsDialog::onNetworkInterfaceChanged(const QString &interfaceName) {
    if (interfaceName.isEmpty()) {
        ui->findStationComboBox->clear();
        ui->findStationComboBox->setPlaceholderText("Ожидание сканирования...");
        return;
    }

    // Блокируем ComboBox на время сканирования
    ui->networkComboBox->setEnabled(false);
    ui->findStationComboBox->clear();
    ui->findStationComboBox->setPlaceholderText("Сканирование...");

    // Запускаем поиск в отдельном потоке (QThreadPool внутри FindManager)
    // Чтобы не блокировать UI, используем QtConcurrent или выносим в worker
    // Для простоты здесь вызываем напрямую, но в продакшене лучше асинхронно

    // ВАЖНО: searchStations блокирующий, поэтому выносим в отдельный поток
    QtConcurrent::run([this, interfaceName]() {
        QVector<QString> found = m_finder->searchStations(interfaceName);
        // Возвращаем результат в главный поток через invokeMethod
        QMetaObject::invokeMethod(this, [this, found]() {
            onScanFinished(found);
        }, Qt::QueuedConnection);
    });
}

void SettingsDialog::onScanFinished(const QVector<QString> &foundIps) {
    ui->networkComboBox->setEnabled(true);
    ui->findStationComboBox->clear();

    for (const QString &ip : foundIps) {
        ui->findStationComboBox->addItem(ip);
    }

    ui->findStationComboBox->setPlaceholderText(
        foundIps.isEmpty() ? "Радиостанции не найдены" : "Выберите найденную радиостанцию"
        );
    ui->findStationComboBox->setCurrentIndex(-1);  // <-- ЭТО КЛЮЧЕВОЕ: после setPlaceholderText
}
