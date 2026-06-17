#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "ui_receiveresultstrip.h"
#include "debug.h"
#include "styles.h"
#include "sweep_plot.h"
#include "qcustomplot.h"
#include "protocol_consts.h"
#include <QEvent>
#include <QMouseEvent>
#include <QProcess>
#include <QTime>
#include <QScrollBar>
#include <QColor>
#include <QPixmap>
#include <algorithm>
#include <cmath>
#include <utility>
#include <QtConcurrent>
#include <QPointer>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSet>
#include <QRandomGenerator>
#include <QMap>
#include <QHash>
#include <QStringList>
#include <QPushButton>
#include <QStyle>
#include <QComboBox>
#include <QDialog>
#include <QSlider>
#include <QSignalBlocker>
#include <QLineEdit>
#include <QTabBar>
#include <QRadioButton>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QAbstractButton>
#include <QFrame>
#include <QLabel>
#include <QLCDNumber>
#include <QMenuBar>
#include <QFontMetrics>
#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QPalette>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QListView>
#include <QPainter>
#include <QPolygon>
#include <QGraphicsDropShadowEffect>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QDateTime>
#include <QIcon>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include "ssher.h"
#include <limits>
#include <memory>

namespace {
constexpr int kSpectrumGridAlignMaxAttempts = 50; // максимальное количество попыток адаптации диапазона под искомую частоту
// КОСТЫЛЬ_АНАЛИЗАТОР_РЕМОНТ: временно игнорируем аппаратную доступность анализатора.
// Когда анализатор вернётся, установить false и убрать связанные пометки.
constexpr bool kAnalyzerRepairBypass = true;

inline bool analyzerAvailableForUi(bool analyzerConnected)
{
    return kAnalyzerRepairBypass || analyzerConnected;
}
constexpr quint64 kHandsMaxFreqHz = 2500000000ULL; // 2500.000.000 Гц
constexpr uint32_t kPowerTestStartFreqHz = 30125000U; // 30.025.000 Гц
constexpr uint32_t kPowerTestStartFreqType3Hz = 220125000U; // 220.025.000 Гц
constexpr uint32_t kPowerTestStartFreqType4Hz = 520125000U; // 520.025.000 Гц
constexpr int kPowerTestDurationMs = 4000;
constexpr int kPowerTestPauseBetweenStepsMs = 1000;
constexpr int kPowerTestPauseBetweenRemeasureMs = 2000;
constexpr quint64 kPowerTestAnalyzerSpanHz = 1000000ULL; // sweep 1 МГц для live-спектра в tabPower
constexpr int kAnalyzerKeepAliveMsDefault = 2000;
/// tabRecieve без запущенного теста: частый CMD_ECHO, чтобы экран анализатора не моргал (раз в 2 с).
constexpr int kAnalyzerKeepAliveMsReceiveTabIdle = 300;
constexpr double kPowerTestMomentHalfWindowMHz = 0.04; // отображаем ±50 кГц вокруг несущей
constexpr quint64 kPowerGraphWideSpanHz = 500000ULL; // 0.5 МГц для power-оценки в tabPower (plotWidgetPowerGraph)
constexpr double kPowerGraphRadiopathOffsetDbm = 60.0; // ёмкость радиотракта от станции до анализатора
constexpr double kPowerGraphAutoYHalfRangeDbm = 10.0;
constexpr double kPowerGraphGreenHalfWidthDbm = 1.5; // зелёная зона: center ± 1.5 dBm
constexpr double kPowerGraphRedBandThicknessDbm = 2.0; // красная зона сверху/снизу вокруг зелёной
constexpr double kPowerGraphInitialYHalfRangeDbm = 2.0; // зелёная зона ±1.5 dBm + 0.5 dBm красной зоны
constexpr double kPowerGraphMaxLevelCenterDbm = 46.0;
/** Мин. мощность: номинал для TrmType 4 (и неизвестного типа). */
constexpr double kPowerGraphMinLevelCenterDbmTrmType4 = 30.0;
/** Мин. мощность: номинал для TrmType 2 и 3. */
constexpr double kPowerGraphMinLevelCenterDbmTrmType23 = 36.0;
constexpr int kPowerTestRemeasureMaxCount = 3; // максимум переизмерений шага на одной частоте
constexpr int kPowerTestPrimaryCheckFreqCount = 30; // «Первичная проверка»: 30 частот вместо полного списка

constexpr int kFhssMaxPoints = 2000; // ограничение истории, чтобы plot не рос бесконечно
constexpr quint64 kFhssTmo4HalfSpanHz = 350000ULL; // 0.35 МГц → окно 0.7 МГц для «ТМО-4»

/// Оценка длительности стартового включения одного тракта на станции (с), для UI после reconnect.
constexpr int kPostReconnectStationTractBootSecPerTract = 8;
constexpr double kPi = 3.14159265358979323846;

/// Склонение для журнала: «1 интерфейс», «2 интерфейса», «5 интерфейсов».
inline QString ruEthernetIfaceWord(int n)
{
    const int n10 = n % 10;
    const int n100 = n % 100;
    if (n10 == 1 && n100 != 11) {
        return QStringLiteral("интерфейс");
    }
    if (n10 >= 2 && n10 <= 4 && (n100 < 10 || n100 >= 20)) {
        return QStringLiteral("интерфейса");
    }
    return QStringLiteral("интерфейсов");
}

/// Склонение для журнала: «1 радиостанция», «2 радиостанции», «5 радиостанций».
inline QString ruStationWord(int n)
{
    const int n10 = n % 10;
    const int n100 = n % 100;
    if (n10 == 1 && n100 != 11) {
        return QStringLiteral("радиостанция");
    }
    if (n10 >= 2 && n10 <= 4 && (n100 < 10 || n100 >= 20)) {
        return QStringLiteral("радиостанции");
    }
    return QStringLiteral("радиостанций");
}

/** Группировка найденных IP по подсети 192.168.X.* с приоритетом *.193 (как в SettingsDialog / handleStationsFound). */
QMap<int, QString> chosenStationsBySubnetFromFoundIps(const QVector<QString> &foundIps)
{
    QMap<int, QString> chosenBySubnet;
    const QRegularExpression re(R"(^192\.168\.(\d{1,3})\.(\d{1,3})$)");

    for (const QString &rawIp : foundIps) {
        const QString ip = rawIp.trimmed();
        const auto m = re.match(ip);
        if (!m.hasMatch()) {
            continue;
        }
        const int subnet = m.captured(1).toInt();
        const int host = m.captured(2).toInt();
        if (subnet < 0 || subnet > 255 || host < 0 || host > 255) {
            continue;
        }

        auto it = chosenBySubnet.find(subnet);
        if (it == chosenBySubnet.end()) {
            chosenBySubnet.insert(subnet, ip);
            continue;
        }

        const QString &current = it.value();
        const auto cur = re.match(current);
        const int currentHost = cur.hasMatch() ? cur.captured(2).toInt() : -1;
        if (currentHost != 193 && host == 193) {
            it.value() = ip;
        }
    }
    return chosenBySubnet;
}

/// Сообщения журнала logTextEdit, которые показываются красным (ошибки и сбои).
bool isApplicationLogErrorMessage(const QString &msg)
{
    const QString &s = msg;

    // Явные маркеры ошибок.
    if (s.contains(QStringLiteral("ОШИБКА"), Qt::CaseInsensitive)) return true;
    if (s.startsWith(QStringLiteral("Ошибка"))) return true;
    if (s.startsWith(QStringLiteral("Err:"), Qt::CaseInsensitive)) return true;
    if (s.startsWith(QStringLiteral("Анализатор отключен:"), Qt::CaseInsensitive)) return true;
    if (s.contains(QStringLiteral("Serial error"), Qt::CaseInsensitive)) return true;
    if (s.contains(QStringLiteral("Таймаут"), Qt::CaseInsensitive)) return true;
    if (s.contains(QStringLiteral("Не удалось"), Qt::CaseInsensitive)) return true;
    if (s.contains(QStringLiteral("не удалось"))) return true;
    if (s.contains(QStringLiteral("Подключение не выполнено"), Qt::CaseInsensitive)) return true;

    // Потеря/отсутствие связи и аварии.
    if (s.contains(QStringLiteral("Потеряна связь"))) return true;
    if (s.contains(QStringLiteral("не ответила"))) return true;
    if (s.contains(QStringLiteral("Нет связи с"))) return true;
    if (s.contains(QStringLiteral("потеря связи"), Qt::CaseInsensitive)) return true;
    if (s.contains(QStringLiteral("нет подключения"), Qt::CaseInsensitive)) return true;
    if (s.contains(QStringLiteral("Авария"))) return true;

    // Запреты, недоступность, неподдерживаемое и т.п.
    if ((s.contains(QStringLiteral("PPM:"), Qt::CaseInsensitive)
         || s.contains(QStringLiteral("ППМ:")))
        && s.contains(QStringLiteral("невозможн"), Qt::CaseInsensitive)) {
        return true;
    }
    if (s.contains(QStringLiteral("ЗАПРЕЩЕНО"))) return true;
    if (s.contains(QStringLiteral("запрещ"), Qt::CaseInsensitive)) return true;
    if (s.contains(QStringLiteral("Неподдерживаемый"), Qt::CaseInsensitive)) return true;
    if (s.contains(QStringLiteral("Неизвестн"), Qt::CaseInsensitive)) return true;
    if (s.contains(QStringLiteral("недоступен"), Qt::CaseInsensitive)) return true;
    if (s.contains(QStringLiteral("Предупреждение"), Qt::CaseInsensitive)) return true;
    if (s.contains(QStringLiteral("Нельзя"), Qt::CaseInsensitive)) return true;

    // «Не найдено / не существует».
    if (s.contains(QStringLiteral("Ethernet-интерфейсы не найдены"))) return true;
    if (s.contains(QStringLiteral("Радиостанции на"), Qt::CaseInsensitive)
        && s.contains(QStringLiteral("не найдены"), Qt::CaseInsensitive)) {
        return true;
    }
    if (s.contains(QStringLiteral("Файл не найден"))) return true;
    if (s.contains(QStringLiteral("Файл обновления не найден"))) return true;
    if (s.contains(QStringLiteral("не существует"))) return true;
    if (s.contains(QStringLiteral("не выбран"))) return true;

    // Валидация ввода и недопустимые параметры.
    if (s.contains(QStringLiteral("должен быть"))) return true;
    if (s.contains(QStringLiteral("не отличается"))) return true;
    if (s.contains(QStringLiteral("некорректн"), Qt::CaseInsensitive)) return true;
    if (s.contains(QStringLiteral("отклонён")) || s.contains(QStringLiteral("отклонен"))) return true;
    if (s.contains(QStringLiteral("выходит за допустимые"))) return true;
    if (s.contains(QStringLiteral("Выберите корректный"))) return true;

    // Прочие специфичные сообщения-проблемы.
    if (s.contains(QStringLiteral("ППРЧ: дождитесь"))) return true;

    return false;
}

/// Сообщения журнала logTextEdit, обозначающие завершённые/успешные действия (зелёный).
/// Зелёный: оконченные операции, подтверждение выполненного действия и призыв
/// к следующему шагу оператора («нажмите …», «готова к …», «подготовлены …»).
bool isApplicationLogSuccessMessage(const QString &msg)
{
    const QString &s = msg;

    // Универсальные маркеры успеха/завершения.
    if (s.contains(QStringLiteral("успешно"), Qt::CaseInsensitive)) return true;
    if (s.contains(QChar(0x2705))) return true; // emoji ✅
    if (s.contains(QStringLiteral("Успешное подключение"), Qt::CaseInsensitive)) return true;

    // Запуск/остановка/завершение операций («TFTP-сервер запущен», «Приложение запущено»,
    // «Тест ... остановлен», «... завершен»).
    if (s.contains(QStringLiteral("запущен"))) return true;
    if (s.contains(QStringLiteral("запущено"))) return true;
    if (s.contains(QStringLiteral("остановлен"))) return true;
    if (s.contains(QStringLiteral("завершен"))) return true;
    if (s.contains(QStringLiteral("завершён"))) return true;

    // Установленная/восстановленная связь («связь установлена», «SSH соединение установлено»,
    // «Связь с радиостанцией восстановлена», «Радиостанция X подключена»).
    if (s.contains(QStringLiteral("связь установлена"))) return true;
    if (s.contains(QStringLiteral("соединение установлено"))) return true;
    if (s.contains(QStringLiteral("восстановлена"), Qt::CaseInsensitive)) return true;
    if (s.contains(QStringLiteral("Аутентификация прошла успешно"))) return true;
    if (s.contains(QStringLiteral("Радиостанция"))
        && s.contains(QStringLiteral("подключена"))) {
        return true;
    }

    // «Найдено N интерфейс...» — по примеру пользователя.
    if (s.startsWith(QStringLiteral("Найдено"), Qt::CaseInsensitive)
        && s.contains(QStringLiteral("интерфейс"), Qt::CaseInsensitive)) {
        return true;
    }

    // Завершённые служебные действия.
    if (s.contains(QStringLiteral("Тракты загружены"))) return true;
    if (s.contains(QStringLiteral("Контроль целостности: ОК"))) return true;
    if (s.contains(QStringLiteral("сохранён"))) return true;
    if (s.contains(QStringLiteral("сохранен"))) return true;
    if (s.contains(QStringLiteral("изменен"))) return true;
    if (s.contains(QStringLiteral("изменён"))) return true;
    if (s.contains(QStringLiteral("Удалён"))) return true;
    if (s.contains(QStringLiteral("Удален"))) return true;
    if (s.contains(QStringLiteral("уже настроен"))) return true;
    if (s.contains(QStringLiteral("В сетевое подключение добавлен"))) return true;
    if (s.contains(QStringLiteral("Файлы обновления подготовлены"))) return true;
    if (s.contains(QStringLiteral("Сокет привязан"))) return true;

    // Призыв к действию оператора после завершённого этапа.
    if (s.contains(QStringLiteral("нажмите"), Qt::CaseInsensitive)) return true;

    // Готовность к следующему шагу («радиостанция готова к началу тестирования»).
    if (s.contains(QStringLiteral("готов"), Qt::CaseInsensitive)
        && s.contains(QStringLiteral(" к "), Qt::CaseInsensitive)) {
        return true;
    }

    // Завершённая подготовка/загрузка («Радиоданные подготовлены …», «… загружены»).
    if (s.contains(QStringLiteral("подготовлены"), Qt::CaseInsensitive)) return true;
    if (s.contains(QStringLiteral("подготовлено"), Qt::CaseInsensitive)) return true;
    if (s.contains(QStringLiteral("загружены"), Qt::CaseInsensitive)) return true;
    if (s.contains(QStringLiteral("загружено"), Qt::CaseInsensitive)) return true;

    // Подтверждение выполненного действия.
    if (s.contains(QStringLiteral("передан"), Qt::CaseInsensitive)) return true;
    if (s.contains(QStringLiteral("выполнено"), Qt::CaseInsensitive)) return true;
    if (s.contains(QStringLiteral("выполнен"), Qt::CaseInsensitive)) return true;

    return false;
}

/// Цвет строки журнала по содержимому. Порядок проверок: красный (ошибка) → зелёный
/// (завершённое действие) → синий (информирующее сообщение, по умолчанию).
QColor applicationLogColorForMessage(const QString &msg)
{
    if (isApplicationLogErrorMessage(msg)) {
        return QColor(QStringLiteral("#f87171")); // красный — ошибки/проблемы
    }
    if (isApplicationLogSuccessMessage(msg)) {
        return QColor(QStringLiteral("#4ade80")); // зелёный — завершённые действия
    }
    return QColor(QStringLiteral("#60a5fa"));     // синий — информирующие
}

/// Низкоуровневые/ожидаемые ошибки устройства — только в debug, не в журнал оператора.
bool shouldShowDeviceErrorToOperator(const QString &err)
{
    const QString s = err.trimmed();
    if (s.isEmpty()) {
        return false;
    }
    // QUdpSocket при обрыве/переподключении (MOD_START, RTP и т.п.).
    if (s.startsWith(QStringLiteral("Ошибка отправки пакета:"), Qt::CaseInsensitive)) {
        return false;
    }
    static const QStringList transientSocketErrors = {
        QStringLiteral("Unable to send"),
        QStringLiteral("The address is not available"),
        QStringLiteral("Network is unreachable"),
        QStringLiteral("Host is unreachable"),
        QStringLiteral("Connection refused"),
        QStringLiteral("Connection reset"),
        QStringLiteral("Socket is not connected"),
        QStringLiteral("Network unreachable"),
    };
    for (const QString &pattern : transientSocketErrors) {
        if (s.contains(pattern, Qt::CaseInsensitive)) {
            return false;
        }
    }
    // Ожидаемо при попытках команд без связи; оператору достаточно сообщения о разрыве/восстановлении.
    if (s == QStringLiteral("Нет подключения к радиостанции!")) {
        return false;
    }
    return true;
}

/// Обновить динамическое QSS-свойство "pauseMode" на кнопке Pause/Play
/// и форсировать пересчёт стиля.
/// isPlayIcon == true  -> кнопка сейчас показывает Play  (тест на паузе)  -> при hover зелёная рамка.
/// isPlayIcon == false -> кнопка сейчас показывает Pause (тест выполняется) -> при hover синяя рамка.
inline void setPauseButtonMode(QPushButton *btn, bool isPlayIcon)
{
    if (!btn) {
        return;
    }
    const QString mode = isPlayIcon ? QStringLiteral("play") : QStringLiteral("pause");
    if (btn->property("pauseMode").toString() == mode) {
        return;
    }
    btn->setProperty("pauseMode", mode);
    if (btn->style()) {
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }
    btn->update();
}

/// Fusion + stylesheet: у выпадающего QListView фон viewport часто остаётся Base (белым),
/// а строки рисуются поверх — сверху/снизу видны «полоски».
void setupComboOpenOnWholeAreaClick(QComboBox *cb, QObject *eventFilterHost)
{
    if (!cb || !eventFilterHost) {
        return;
    }
    cb->setEditable(true);
    cb->setInsertPolicy(QComboBox::NoInsert);
    if (QLineEdit *line = cb->lineEdit()) {
        line->setReadOnly(true);
        line->setFrame(false);
        line->setCursor(Qt::ArrowCursor);
        line->setFocusPolicy(Qt::StrongFocus);
        line->installEventFilter(eventFilterHost);
    }
    cb->installEventFilter(eventFilterHost);
}

inline void polishComboDropDownSurface(QComboBox *cb, const QColor &bg = QColor(QStringLiteral("#1e293b")))
{
    if (!cb) {
        return;
    }
    // Важно: у QComboBox view может создаваться лениво/меняться.
    // Принудительно используем QListView, чтобы контролировать фон viewport.
    if (!cb->view() || !qobject_cast<QListView *>(cb->view())) {
        cb->setView(new QListView(cb));
    }

    QAbstractItemView *view = cb->view();
    if (!view) {
        return;
    }
    // Убираем рамки/отступы: иначе фон контейнера может "подсвечивать" полосами.
    view->setFrameShape(QFrame::NoFrame);
    view->setContentsMargins(0, 0, 0, 0);
    view->setAutoFillBackground(true);
    view->setAutoFillBackground(true);
    QPalette pal = view->palette();
    pal.setColor(QPalette::Base, bg);
    pal.setColor(QPalette::AlternateBase, bg);
    pal.setColor(QPalette::Window, bg);
    pal.setColor(QPalette::Button, bg);
    view->setPalette(pal);
    if (QWidget *vp = view->viewport()) {
        vp->setAutoFillBackground(true);
        vp->setContentsMargins(0, 0, 0, 0);
        QPalette vpPal = vp->palette();
        vpPal.setColor(QPalette::Base, bg);
        vpPal.setColor(QPalette::Window, bg);
        vp->setPalette(vpPal);
    }

    // Контейнер popup (QComboBoxPrivateContainer) тоже должен быть тёмным,
    // иначе будут белые "поля" вокруг view.
    if (QWidget *popup = view->window()) {
        popup->setAutoFillBackground(true);
        popup->setContentsMargins(0, 0, 0, 0);
        QPalette wp = popup->palette();
        wp.setColor(QPalette::Window, bg);
        wp.setColor(QPalette::Base, bg);
        wp.setColor(QPalette::AlternateBase, bg);
        popup->setPalette(wp);
        popup->setStyleSheet(QStringLiteral("background-color: #1e293b;"));
    }
}

inline double powerGraphAnalyzerToRealDbm(double analyzerDbm)
{
    return analyzerDbm + kPowerGraphRadiopathOffsetDbm;
}

inline double fhssGraphAnalyzerToDisplayDbm(double analyzerDbm)
{
    return analyzerDbm + kPowerGraphRadiopathOffsetDbm;
}

QVector<double> ampsWithRadiopathOffset(const QVector<double> &amps)
{
    QVector<double> out;
    out.resize(amps.size());
    for (int i = 0; i < amps.size(); ++i) {
        out[i] = fhssGraphAnalyzerToDisplayDbm(amps.at(i));
    }
    return out;
}

inline bool powerAmpInsideGreenBand(double dbm, double centerDbm)
{
    const double hi = centerDbm + kPowerGraphGreenHalfWidthDbm;
    const double lo = centerDbm - kPowerGraphGreenHalfWidthDbm;
    return dbm >= lo && dbm <= hi;
}

const QVector<quint64> kPowerTestFrequenciesType2Hz = {
    30025000ULL,
    30425000ULL,
    31125000ULL,
    31625000ULL,
    32225000ULL,
    32725000ULL,
    33025000ULL,
    33525000ULL,
    34025000ULL,
    34925000ULL,
    35025000ULL,
    35525000ULL,
    36125000ULL,
    36625000ULL,
    37225000ULL,
    37725000ULL,
    38525000ULL,
    38525000ULL,
    39125000ULL,
    39625000ULL,
    40225000ULL,
    40725000ULL,
    41025000ULL,
    41525000ULL,
    42125000ULL,
    42625000ULL,
    43225000ULL,
    43725000ULL,
    44025000ULL,
    44525000ULL,
    45125000ULL,
    45525000ULL,
    46225000ULL,
    46725000ULL,
    47025000ULL,
    47525000ULL,
    48125000ULL,
    48625000ULL,
    49225000ULL,
    49725000ULL,
    50025000ULL,
    50525000ULL,
    51125000ULL,
    51625000ULL,
    52225000ULL,
    52725000ULL,
    53025000ULL,
    53525000ULL,
    54125000ULL,
    54625000ULL,
    55225000ULL,
    55725000ULL,
    56025000ULL,
    56525000ULL,
    57125000ULL,
    57625000ULL,
    58225000ULL,
    58725000ULL,
    59025000ULL,
    59525000ULL,
    60125000ULL,
    60625000ULL,
    61225000ULL,
    61725000ULL,
    62025000ULL,
    62525000ULL,
    63125000ULL,
    63625000ULL,
    64225000ULL,
    64725000ULL,
    65025000ULL,
    65525000ULL,
    66125000ULL,
    66625000ULL,
    67225000ULL,
    67725000ULL,
    68025000ULL,
    68525000ULL,
    69125000ULL,
    69625000ULL,
    70225000ULL,
    70725000ULL,
    71025000ULL,
    71525000ULL,
    72125000ULL,
    72525000ULL,
    73225000ULL,
    73725000ULL,
    74025000ULL,
    74525000ULL,
    75125000ULL,
    75625000ULL,
    76225000ULL,
    76725000ULL,
    77025000ULL,
    77525000ULL,
    78125000ULL,
    78625000ULL,
    79225000ULL,
    79725000ULL,
    80025000ULL,
    80525000ULL,
    81125000ULL,
    81625000ULL,
    82225000ULL,
    82725000ULL,
    83025000ULL,
    83525000ULL,
    84125000ULL,
    84625000ULL,
    85025000ULL,
    85725000ULL,
    86025000ULL,
    86525000ULL,
    87125000ULL,
    87625000ULL,
    88225000ULL,
    88725000ULL,
    89025000ULL,
    89525000ULL,
    90125000ULL,
    90625000ULL,
    91225000ULL,
    91725000ULL,
    92025000ULL,
    92525000ULL,
    93125000ULL,
    93625000ULL,
    94225000ULL,
    94725000ULL,
    95025000ULL,
    95525000ULL,
    96125000ULL,
    96625000ULL,
    97225000ULL,
    97725000ULL,
    98025000ULL,
    98525000ULL,
    99125000ULL,
    99625000ULL,
    100225000ULL,
    100725000ULL,
    101025000ULL,
    101525000ULL,
    102125000ULL,
    102625000ULL,
    103225000ULL,
    103725000ULL,
    104025000ULL,
    104525000ULL,
    105125000ULL,
    105625000ULL,
    106225000ULL,
    106725000ULL,
    107025000ULL,
    107525000ULL,
    108125000ULL,
    108625000ULL,
    109225000ULL,
    109725000ULL,
    110025000ULL,
    110525000ULL,
    111125000ULL,
    111625000ULL,
    112225000ULL,
    112725000ULL,
    113025000ULL,
    113525000ULL,
    114125000ULL,
    114625000ULL,
    115225000ULL,
    115725000ULL,
    116025000ULL,
    116525000ULL,
    117125000ULL,
    117625000ULL,
    118525000ULL,
    118925000ULL,
    119025000ULL,
    119525000ULL,
    120125000ULL,
    120625000ULL,
    121225000ULL,
    121725000ULL,
    122025000ULL,
    122525000ULL,
    123125000ULL,
    123625000ULL,
    124225000ULL,
    124725000ULL,
    125025000ULL,
    125525000ULL,
    126125000ULL,
    126625000ULL,
    127225000ULL,
    127725000ULL,
    128025000ULL,
    128525000ULL,
    129125000ULL,
    129625000ULL,
    130225000ULL,
    130725000ULL,
    131025000ULL,
    131525000ULL,
    132125000ULL,
    132625000ULL,
    133225000ULL,
    133725000ULL,
    134025000ULL,
    134525000ULL,
    135125000ULL,
    135625000ULL,
    136225000ULL,
    136725000ULL,
    137025000ULL,
    137525000ULL,
    138125000ULL,
    138625000ULL,
    139225000ULL,
    139725000ULL,
    140025000ULL,
    140525000ULL,
    141125000ULL,
    141625000ULL,
    142225000ULL,
    142725000ULL,
    143025000ULL,
    143525000ULL,
    144125000ULL,
    144625000ULL,
    145225000ULL,
    145725000ULL,
    146025000ULL,
    146525000ULL,
    147125000ULL,
    147625000ULL,
    148225000ULL,
    148725000ULL,
    149025000ULL,
    149525000ULL,
    150125000ULL,
    150625000ULL,
    151225000ULL,
    151725000ULL,
    152025000ULL,
    152525000ULL,
    153125000ULL,
    153625000ULL,
    154225000ULL,
    154725000ULL,
    155025000ULL,
    155525000ULL,
    156125000ULL,
    156625000ULL,
    157025000ULL,
    157725000ULL,
    158025000ULL,
    158525000ULL,
    159125000ULL,
    159625000ULL,
    160225000ULL,
    160725000ULL,
    161025000ULL,
    161525000ULL,
    162125000ULL,
    162625000ULL,
    163225000ULL,
    163725000ULL,
    164025000ULL,
    164525000ULL,
    165125000ULL,
    165625000ULL,
    166225000ULL,
    166725000ULL,
    167025000ULL,
    167525000ULL,
    168125000ULL,
    168625000ULL,
    169225000ULL,
    169725000ULL,
    170025000ULL,
    170525000ULL,
    171125000ULL,
    171625000ULL,
    172225000ULL,
    172725000ULL,
    173025000ULL,
    173525000ULL,
    174125000ULL,
    174625000ULL,
    175225000ULL,
    175725000ULL,
    176025000ULL,
    176525000ULL,
    177125000ULL,
    177625000ULL,
    178225000ULL,
    178725000ULL,
    179025000ULL,
    179975000ULL
};
const QVector<quint64> kPowerTestFrequenciesType3Hz = {
    220025000ULL,
    221125000ULL,
    222225000ULL,
    223325000ULL,
    224425000ULL,
    225525000ULL,
    226625000ULL,
    227725000ULL,
    228825000ULL,
    229925000ULL,
    230025000ULL,
    231125000ULL,
    232225000ULL,
    233325000ULL,
    234425000ULL,
    235525000ULL,
    236625000ULL,
    237725000ULL,
    238825000ULL,
    239925000ULL,
    240025000ULL,
    241125000ULL,
    242225000ULL,
    243325000ULL,
    244425000ULL,
    245525000ULL,
    246625000ULL,
    247725000ULL,
    248825000ULL,
    249925000ULL,
    250025000ULL,
    251125000ULL,
    252225000ULL,
    253325000ULL,
    254425000ULL,
    255525000ULL,
    256625000ULL,
    257725000ULL,
    258825000ULL,
    259925000ULL,
    260025000ULL,
    261125000ULL,
    262225000ULL,
    263325000ULL,
    264425000ULL,
    265525000ULL,
    266625000ULL,
    267725000ULL,
    268825000ULL,
    269925000ULL,
    270025000ULL,
    271125000ULL,
    272225000ULL,
    273325000ULL,
    274425000ULL,
    275525000ULL,
    276625000ULL,
    277725000ULL,
    278825000ULL,
    279925000ULL,
    280025000ULL,
    281125000ULL,
    282225000ULL,
    283325000ULL,
    284425000ULL,
    285525000ULL,
    286625000ULL,
    287725000ULL,
    288825000ULL,
    289925000ULL,
    290025000ULL,
    291125000ULL,
    292225000ULL,
    293325000ULL,
    294425000ULL,
    295525000ULL,
    296625000ULL,
    297725000ULL,
    298825000ULL,
    299025000ULL,
    300025000ULL,
    301125000ULL,
    302225000ULL,
    303325000ULL,
    304425000ULL,
    305525000ULL,
    306625000ULL,
    307725000ULL,
    308825000ULL,
    309925000ULL,
    310025000ULL,
    311125000ULL,
    312225000ULL,
    313325000ULL,
    314425000ULL,
    315525000ULL,
    316625000ULL,
    317725000ULL,
    318825000ULL,
    319925000ULL,
    320025000ULL,
    321125000ULL,
    322225000ULL,
    323325000ULL,
    324425000ULL,
    325525000ULL,
    326625000ULL,
    327725000ULL,
    328825000ULL,
    329925000ULL,
    330025000ULL,
    331125000ULL,
    332225000ULL,
    333325000ULL,
    334425000ULL,
    335525000ULL,
    336625000ULL,
    337725000ULL,
    338825000ULL,
    339925000ULL,
    340025000ULL,
    341125000ULL,
    342225000ULL,
    343325000ULL,
    344425000ULL,
    345525000ULL,
    346625000ULL,
    347725000ULL,
    348825000ULL,
    349925000ULL,
    350025000ULL,
    351125000ULL,
    352225000ULL,
    353325000ULL,
    354425000ULL,
    355525000ULL,
    356625000ULL,
    357725000ULL,
    358825000ULL,
    359925000ULL,
    360025000ULL,
    361125000ULL,
    362225000ULL,
    363325000ULL,
    364425000ULL,
    365525000ULL,
    366625000ULL,
    367725000ULL,
    368825000ULL,
    369925000ULL,
    370025000ULL,
    371125000ULL,
    372225000ULL,
    373325000ULL,
    374425000ULL,
    375525000ULL,
    376625000ULL,
    377725000ULL,
    378825000ULL,
    379025000ULL,
    380025000ULL,
    381125000ULL,
    382225000ULL,
    383325000ULL,
    384425000ULL,
    385525000ULL,
    386625000ULL,
    387725000ULL,
    388825000ULL,
    389925000ULL,
    390025000ULL,
    391125000ULL,
    392225000ULL,
    393325000ULL,
    394425000ULL,
    395525000ULL,
    396625000ULL,
    397725000ULL,
    398825000ULL,
    399925000ULL,
    400025000ULL,
    401125000ULL,
    402225000ULL,
    403325000ULL,
    404425000ULL,
    405525000ULL,
    406625000ULL,
    407725000ULL,
    408825000ULL,
    409925000ULL,
    410025000ULL,
    411125000ULL,
    412225000ULL,
    413325000ULL,
    414425000ULL,
    415525000ULL,
    416625000ULL,
    417725000ULL,
    418825000ULL,
    419925000ULL,
    420025000ULL,
    421125000ULL,
    422225000ULL,
    423325000ULL,
    424425000ULL,
    425525000ULL,
    426625000ULL,
    427725000ULL,
    428825000ULL,
    429925000ULL,
    430025000ULL,
    431125000ULL,
    432225000ULL,
    433325000ULL,
    434425000ULL,
    435525000ULL,
    436625000ULL,
    437725000ULL,
    438825000ULL,
    439925000ULL,
    440025000ULL,
    441125000ULL,
    442225000ULL,
    443325000ULL,
    444425000ULL,
    445525000ULL,
    446625000ULL,
    447725000ULL,
    448825000ULL,
    449925000ULL,
    450025000ULL,
    451125000ULL,
    452225000ULL,
    453325000ULL,
    454425000ULL,
    455525000ULL,
    456625000ULL,
    457725000ULL,
    458825000ULL,
    459925000ULL,
    460025000ULL,
    461125000ULL,
    462225000ULL,
    463325000ULL,
    464425000ULL,
    465525000ULL,
    466625000ULL,
    467725000ULL,
    468825000ULL,
    469975000ULL
};
const QVector<quint64> kPowerTestFrequenciesType2FullRangeExtraHz = {
    180025000ULL,
    180525000ULL,
    181125000ULL,
    181625000ULL,
    182225000ULL,
    182725000ULL,
    183025000ULL,
    183525000ULL,
    184125000ULL,
    184625000ULL,
    185225000ULL,
    185725000ULL,
    186025000ULL,
    186525000ULL,
    187125000ULL,
    187625000ULL,
    188225000ULL,
    188725000ULL,
    189025000ULL,
    189525000ULL,
    190125000ULL,
    190625000ULL,
    191225000ULL,
    191725000ULL,
    192025000ULL,
    192525000ULL,
    193125000ULL,
    193625000ULL,
    194225000ULL,
    194725000ULL,
    195025000ULL,
    195525000ULL,
    196125000ULL,
    196625000ULL,
    197225000ULL,
    197725000ULL,
    198025000ULL,
    198525000ULL,
    199125000ULL,
    199625000ULL,
    200225000ULL,
    200725000ULL,
    201025000ULL,
    201525000ULL,
    202125000ULL,
    202625000ULL,
    203225000ULL,
    203725000ULL,
    204025000ULL,
    204525000ULL,
    205125000ULL,
    205625000ULL,
    206225000ULL,
    206725000ULL,
    207025000ULL,
    207525000ULL,
    208125000ULL,
    208625000ULL,
    209225000ULL,
    209725000ULL,
    210025000ULL,
    210525000ULL,
    211125000ULL,
    211625000ULL,
    212225000ULL,
    212725000ULL,
    213025000ULL,
    213525000ULL,
    214125000ULL,
    214625000ULL,
    215225000ULL,
    215725000ULL,
    216025000ULL,
    216525000ULL,
    217125000ULL,
    217625000ULL,
    218225000ULL,
    218725000ULL,
    219325000ULL,
    219925000ULL
};
const QVector<quint64> kPowerTestFrequenciesType3FullRangeExtraHz = {
    470025000ULL,
    471125000ULL,
    472225000ULL,
    473325000ULL,
    474425000ULL,
    475525000ULL,
    476625000ULL,
    477725000ULL,
    478825000ULL,
    479925000ULL,
    480025000ULL,
    481125000ULL,
    482225000ULL,
    483325000ULL,
    484425000ULL,
    485525000ULL,
    486625000ULL,
    487725000ULL,
    488825000ULL,
    489925000ULL,
    490025000ULL,
    491125000ULL,
    492225000ULL,
    493325000ULL,
    494425000ULL,
    495525000ULL,
    496625000ULL,
    497725000ULL,
    498825000ULL,
    499925000ULL,
    500025000ULL,
    501125000ULL,
    502225000ULL,
    503325000ULL,
    504425000ULL,
    505525000ULL,
    506625000ULL,
    507725000ULL,
    508825000ULL,
    509925000ULL,
    510025000ULL,
    511125000ULL,
    512225000ULL,
    513325000ULL,
    514425000ULL,
    515525000ULL,
    516625000ULL,
    517725000ULL,
    518825000ULL,
    519925000ULL
};
const QVector<quint64> kPowerTestFrequenciesType4Hz = {
    520025000ULL,
    534225000ULL,
    548225000ULL,
    551225000ULL,
    567225000ULL,
    572225000ULL,
    589225000ULL,
    593225000ULL,
    606225000ULL,
    615225000ULL,
    624225000ULL,
    630025000ULL,
    641225000ULL,
    657225000ULL,
    662225000ULL,
    679225000ULL,
    683225000ULL,
    696225000ULL,
    705225000ULL,
    720025000ULL,
    728225000ULL,
    731225000ULL,
    747225000ULL,
    752225000ULL,
    769225000ULL,
    773225000ULL,
    786225000ULL,
    795225000ULL,
    804225000ULL,
    818225000ULL,
    821225000ULL,
    837225000ULL,
    847525000ULL,
    859225000ULL,
    863225000ULL,
    876225000ULL,
    885225000ULL,
    894225000ULL,
    908225000ULL,
    911225000ULL,
    927225000ULL,
    932225000ULL,
    949225000ULL,
    953225000ULL,
    965025000ULL,
    975225000ULL,
    984225000ULL,
    998225000ULL,
    1001225000ULL,
    1017225000ULL,
    1022225000ULL,
    1039225000ULL,
    1043225000ULL,
    1056225000ULL,
    1065225000ULL,
    1074225000ULL,
    1088225000ULL,
    1091225000ULL,
    1107225000ULL,
    1117525000ULL,
    1129225000ULL,
    1133225000ULL,
    1146225000ULL,
    1155225000ULL,
    1164225000ULL,
    1178225000ULL,
    1181225000ULL,
    1197225000ULL,
    1202225000ULL,
    1219225000ULL,
    1223225000ULL,
    1236225000ULL,
    1249975000ULL,
    1254225000ULL,
    1268225000ULL,
    1279025000ULL,
    1287225000ULL,
    1292225000ULL,
    1309225000ULL,
    1313225000ULL,
    1326225000ULL,
    1335225000ULL,
    1344225000ULL,
    1358225000ULL,
    1361225000ULL,
    1377225000ULL,
    1382225000ULL,
    1399225000ULL,
    1403225000ULL,
    1416225000ULL,
    1425225000ULL,
    1434225000ULL,
    1448225000ULL,
    1451225000ULL,
    1467225000ULL,
    1472225000ULL,
    1489225000ULL,
    1493225000ULL,
    1506225000ULL,
    1515225000ULL,
    1524225000ULL,
    1538225000ULL,
    1541225000ULL,
    1557225000ULL,
    1562225000ULL,
    1579225000ULL,
    1583225000ULL,
    1596225000ULL,
    1605225000ULL,
    1614225000ULL,
    1628225000ULL,
    1631225000ULL,
    1647225000ULL,
    1652225000ULL,
    1669225000ULL,
    1673225000ULL,
    1686225000ULL,
    1695225000ULL,
    1704225000ULL,
    1718225000ULL,
    1721225000ULL,
    1737225000ULL,
    1749025000ULL,
    1759225000ULL,
    1763225000ULL,
    1776225000ULL,
    1785225000ULL,
    1794225000ULL,
    1808225000ULL,
    1811225000ULL,
    1827225000ULL,
    1832225000ULL,
    1850025000ULL,
    1853225000ULL,
    1866225000ULL,
    1875225000ULL,
    1884225000ULL,
    1898225000ULL,
    1901225000ULL,
    1917225000ULL,
    1922225000ULL,
    1939225000ULL,
    1943225000ULL,
    1956225000ULL,
    1965225000ULL,
    1974225000ULL,
    1988225000ULL,
    1991225000ULL,
    2007225000ULL,
    2012225000ULL,
    2029225000ULL,
    2033225000ULL,
    2046225000ULL,
    2055225000ULL,
    2064225000ULL,
    2078225000ULL,
    2081225000ULL,
    2097225000ULL,
    2100025000ULL,
    2119225000ULL,
    2123225000ULL,
    2136225000ULL,
    2145225000ULL,
    2154225000ULL,
    2168225000ULL,
    2171225000ULL,
    2187225000ULL,
    2192225000ULL,
    2209225000ULL,
    2213225000ULL,
    2226225000ULL,
    2235225000ULL,
    2244225000ULL,
    2258225000ULL,
    2261225000ULL,
    2277225000ULL,
    2282225000ULL,
    2299225000ULL,
    2303225000ULL,
    2316225000ULL,
    2325225000ULL,
    2334225000ULL,
    2348225000ULL,
    2351225000ULL,
    2367225000ULL,
    2372225000ULL,
    2389225000ULL,
    2393225000ULL,
    2406225000ULL,
    2415225000ULL,
    2424225000ULL,
    2438225000ULL,
    2441225000ULL,
    2457225000ULL,
    2462225000ULL,
    2479225000ULL,
    2483225000ULL,
    2499025000ULL
};

QVector<quint64> thinPowerTestFrequencies(const QVector<quint64> &source, int targetCount)
{
    if (targetCount <= 0 || source.isEmpty()) {
        return {};
    }
    if (source.size() <= targetCount) {
        return source;
    }
    QVector<quint64> result;
    result.reserve(targetCount);
    for (int i = 0; i < targetCount; ++i) {
        const int idx = (targetCount == 1)
                            ? 0
                            : (i * (source.size() - 1)) / (targetCount - 1);
        result.append(source.at(idx));
    }
    return result;
}

QVector<quint64> buildPowerTestFrequencyList(int trmType, bool primaryCheck, bool fullRange)
{
    QVector<quint64> freqs;
    switch (trmType) {
    case 2:
        freqs = kPowerTestFrequenciesType2Hz;
        break;
    case 3:
        freqs = kPowerTestFrequenciesType3Hz;
        break;
    case 4:
        freqs = kPowerTestFrequenciesType4Hz;
        break;
    default:
        freqs = kPowerTestFrequenciesType2Hz;
        break;
    }

    // «Полный диапазон»: сначала расширяем список (30–220 / 220–520 МГц),
    // чтобы «Первичная проверка» при обоих чек-боксах прореживала именно полный диапазон.
    if (fullRange) {
        switch (trmType) {
        case 2:
            freqs += kPowerTestFrequenciesType2FullRangeExtraHz;
            break;
        case 3:
            freqs += kPowerTestFrequenciesType3FullRangeExtraHz;
            break;
        default:
            break;
        }
    }

    std::sort(freqs.begin(), freqs.end());
    freqs.erase(std::unique(freqs.begin(), freqs.end()), freqs.end());

    if (primaryCheck) {
        freqs = thinPowerTestFrequencies(freqs, kPowerTestPrimaryCheckFreqCount);
    }
    return freqs;
}

void powerGraphFreqRangeMHzForTrmType(int trmType, bool fullRange, double *xLoMHz, double *xHiMHz)
{
    double xLo = 30.0;
    double xHi = 180.0;
    switch (trmType) {
    case 2:
        xLo = 30.0;
        xHi = fullRange ? 220.0 : 180.0;
        break;
    case 3:
        xLo = 220.0;
        xHi = fullRange ? 520.0 : 470.0;
        break;
    case 4:
        xLo = 520.0;
        xHi = 2500.0;
        break;
    default:
        break;
    }
    if (xLoMHz) {
        *xLoMHz = xLo;
    }
    if (xHiMHz) {
        *xHiMHz = xHi;
    }
}

constexpr const char *kTestProfileResourcePath = ":/profile_active_TEST.tar.gz";
constexpr const char *kTestProfileRemotePath = "/tmp/profile_active_TEST.tar.gz";
constexpr const char *kStationSshUser = "root";
constexpr const char *kStationSshPassword = "zxcvbn";
constexpr const char *kTraktParamRemotePath = "/radio/configs/TraktParam.xml";
constexpr const char *kProfilesRemotePath = "/radio/profiles/Profiles.xml";
constexpr const char *kTemplateProfileRootDirName = "Profile_Active";
constexpr int kDefaultProfileId = 1;

QByteArray generateMinimalProfilesXml(int profId, const QString &profName)
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("Profiles"));
    w.writeTextElement(QStringLiteral("ActiveId"), QString::number(profId));
    w.writeTextElement(QStringLiteral("ProfileNum"), QString::number(1));
    w.writeStartElement(QStringLiteral("Profile_1"));
    w.writeTextElement(QStringLiteral("ProfId"), QString::number(profId));
    w.writeTextElement(QStringLiteral("ProfName"), profName);
    w.writeEndElement();
    w.writeTextElement(QStringLiteral("Version"), QStringLiteral("3"));
    w.writeEndElement();
    w.writeEndDocument();
    return out;
}

QString patchTraktParamProfIdInPlace(const QString &xml, int profId)
{
    QString result = xml;
    int searchFrom = 0;
    const QRegularExpression openRe(QStringLiteral("<Trakt_(\\d+)>"));
    while (true) {
        const QRegularExpressionMatch m = openRe.match(result, searchFrom);
        if (!m.hasMatch()) {
            break;
        }
        const int tagEnd = m.capturedEnd();
        const QString closeToken = QStringLiteral("</Trakt_%1>").arg(m.captured(1));
        const int closePos = result.indexOf(closeToken, tagEnd);
        if (closePos < 0) {
            break;
        }
        const QString block = result.mid(m.capturedStart(), closePos - m.capturedStart());
        if (!block.contains(QStringLiteral("ProfId"))) {
            const QString insert = QStringLiteral("\n\t\t<ProfId> %1 </ProfId>").arg(profId);
            result.insert(tagEnd, insert);
            searchFrom = tagEnd + insert.size();
        } else {
            searchFrom = tagEnd;
        }
    }
    return result;
}

bool stationHasUsableProfileOverSsh(SSHer &ssher, int traktNum, QString *detail)
{
    int exitCode = -1;
    const QString cmd = QStringLiteral(
        "/bin/sh -c 'find /radio/profiles/Profile_Active -path \"*/Trakt_*/Dirs.xml\" 2>/dev/null | wc -l'");
    const QString out = ssher.executeCommand(cmd, &exitCode);
    if (exitCode != 0) {
        if (detail) {
            *detail = QStringLiteral("не удалось проверить наличие профиля на станции");
        }
        return false;
    }
    bool ok = false;
    const int dirCount = out.trimmed().toInt(&ok);
    if (!ok) {
        if (detail) {
            *detail = QStringLiteral("не удалось разобрать ответ проверки профиля");
        }
        return false;
    }
    const int required = traktNum > 0 ? traktNum : 1;
    return dirCount >= required;
}

QString formatHzTriplet(quint64 hz)
{
    const quint64 a = hz / 1000000ULL;
    const quint64 b = (hz / 1000ULL) % 1000ULL;
    const quint64 c = hz % 1000ULL;
    return QStringLiteral("%1.%2.%3")
        .arg(a, 3, 10, QLatin1Char('0'))
        .arg(b, 3, 10, QLatin1Char('0'))
        .arg(c, 3, 10, QLatin1Char('0'));
}

static QString formatHzTriplet4(quint64 hz, QChar padChar = QLatin1Char(' '))
{
    const quint64 a = hz / 1000000ULL;
    const quint64 b = (hz / 1000ULL) % 1000ULL;
    const quint64 c = hz % 1000ULL;
    return QStringLiteral("%1.%2.%3")
        .arg(a, 4, 10, padChar)
        .arg(b, 3, 10, QLatin1Char('0'))
        .arg(c, 3, 10, QLatin1Char('0'));
}

QString formatGroupedWithDots(quint64 value)
{
    const QString digits = QString::number(value);
    QString grouped;
    grouped.reserve(digits.size() + digits.size() / 3);
    for (int i = 0; i < digits.size(); ++i) {
        if (i > 0 && ((digits.size() - i) % 3 == 0)) {
            grouped.append(QLatin1Char('.'));
        }
        grouped.append(digits.at(i));
    }
    return grouped;
}

int truncateRssiFractionalDigit(int16_t rawRssi)
{
    // Станция передаёт RSSI с одной дробной цифрой (x10): отбрасываем её.
    return static_cast<int>(rawRssi) / 10;
}

QString spectrumBwLabelText(int idx)
{
    switch (qBound(0, idx, 3)) {
    case 0:
        return QStringLiteral("2.5 кГц");
    case 1:
        return QStringLiteral("5 кГц");
    case 2:
        return QStringLiteral("10 кГц");
    default:
        return QStringLiteral("25 кГц");
    }
}

QString trmTypeToPpmBaseName(int trmType)
{
    switch (trmType) {
    case 1:
        return QStringLiteral("ДМКВ");
    case 2:
        return QStringLiteral("МВ");
    case 3:
        return QStringLiteral("ДМВ1");
    case 4:
        return QStringLiteral("ДМВ2");
    default:
        return QStringLiteral("—");
    }
}

QStringList stationTractLabelsForSortedTrakts(const QVector<TraktParamEntry> &sorted)
{
    QHash<int, int> typeCount;
    for (const TraktParamEntry &e : sorted) {
        if (e.trmType > 0) {
            ++typeCount[e.trmType];
        }
    }
    QHash<int, int> typeIdx;
    QStringList out;
    for (const TraktParamEntry &e : sorted) {
        const QString base = trmTypeToPpmBaseName(e.trmType);
        if (typeCount.value(e.trmType) > 1) {
            ++typeIdx[e.trmType];
            out.append(QStringLiteral("%1_%2").arg(base).arg(typeIdx[e.trmType]));
        } else {
            out.append(base);
        }
    }
    return out;
}

QString tractCountRussianWord(int count)
{
    const int n10 = count % 10;
    const int n100 = count % 100;
    if (n10 == 1 && n100 != 11) {
        return QStringLiteral("тракт");
    }
    if (n10 >= 2 && n10 <= 4 && (n100 < 10 || n100 >= 20)) {
        return QStringLiteral("тракта");
    }
    return QStringLiteral("трактов");
}

QString parseFwPrintenvVariantValue(const QString &rawOutput)
{
    QString s = rawOutput.trimmed();
    const int nl = s.indexOf(QLatin1Char('\n'));
    if (nl >= 0) {
        s = s.left(nl).trimmed();
    }
    const int eq = s.indexOf(QLatin1Char('='));
    if (eq >= 0) {
        s = s.mid(eq + 1).trimmed();
    }
    return s;
}

QString readStationVariantOverSsh(SSHer &ssher)
{
    int exitCode = -1;
    const QString out = ssher.executeCommand(QStringLiteral("fw_printenv variant"), &exitCode);
    if (out.trimmed().isEmpty()) {
        return QString();
    }
    const QString value = parseFwPrintenvVariantValue(out);
    if (!value.isEmpty()) {
        return value;
    }
    if (exitCode == 0) {
        return QString();
    }
    return QString();
}

QString formatStationVariantLogMessage(const QString &variant)
{
    if (variant.isEmpty()) {
        return QStringLiteral("Вариант исполнения радиостанции: не удалось прочитать (fw_printenv variant)");
    }
    return QStringLiteral("Вариант исполнения радиостанции: %1").arg(variant);
}

QString formatLocalIpForStationConnectionLogMessage(const QString &selfIp)
{
    const QString ip = selfIp.trimmed();
    if (ip.isEmpty()) {
        return QString();
    }
    return QStringLiteral("Для связи с радиостанцией используется локальный IP %1")
        .arg(ip);
}

QString formatStationTractsConfigLogMessage(const QVector<TraktParamEntry> &entries, int traktNum)
{
    QVector<TraktParamEntry> sorted = entries;
    std::sort(sorted.begin(), sorted.end(), [](const TraktParamEntry &a, const TraktParamEntry &b) {
        if (a.trLn != b.trLn) {
            return a.trLn < b.trLn;
        }
        if (a.trmType != b.trmType) {
            return a.trmType < b.trmType;
        }
        return a.trmNr < b.trmNr;
    });
    if (traktNum > 0 && sorted.size() > traktNum) {
        sorted.resize(traktNum);
    }
    const QStringList labels = stationTractLabelsForSortedTrakts(sorted);
    const int count = traktNum > 0 ? traktNum : labels.size();
    if (count <= 0) {
        return QStringLiteral("Конфигурация радиостанции: тракты не найдены в TraktParam.xml");
    }
    return QStringLiteral("Конфигурация радиостанции: %1 %2 — %3")
        .arg(count)
        .arg(tractCountRussianWord(count))
        .arg(labels.join(QStringLiteral(", ")));
}

QStringList ppmLabelsForSortedTrakts(const QVector<TraktParamEntry> &sorted)
{
    QHash<int, int> typeCount;
    for (const TraktParamEntry &e : sorted) {
        if (e.trmType == 1) {
            // Тракты типа 1 (ДМКВ) существуют, но в PPM-управление не входят.
            continue;
        }
        if (e.trmType > 0) {
            ++typeCount[e.trmType];
        }
    }
    QHash<int, int> typeIdx;
    QStringList out;
    for (const TraktParamEntry &e : sorted) {
        if (e.trmType == 1) {
            continue;
        }
        const QString base = trmTypeToPpmBaseName(e.trmType);
        if (typeCount.value(e.trmType) > 1) {
            ++typeIdx[e.trmType];
            out.append(QStringLiteral("%1_%2").arg(base).arg(typeIdx[e.trmType]));
        } else {
            out.append(base);
        }
    }
    return out;
}

int stationNumFromIp(const QString &ip, bool *okOut = nullptr)
{
    bool ok = false;
    const QStringList parts = ip.trimmed().split('.');
    int stationNum = 0;
    if (parts.size() == 4) {
        stationNum = parts[2].toInt(&ok);
    }
    if (okOut) {
        *okOut = ok;
    }
    return ok ? stationNum : 0;
}

int pickOtherStationNum(int currentStationNum)
{
    // Требование: произвольный номер 1..10, не совпадающий с текущей станцией.
    // Делаем детерминированно, чтобы результат был воспроизводим.
    int s = currentStationNum % 10;
    if (s <= 0) {
        s = 1;
    }
    if (s == currentStationNum) {
        s = (s % 10) + 1;
    }
    if (s == currentStationNum) {
        // Если currentStationNum вне 1..10 — выбираем 1.
        s = 1;
    }
    if (s == currentStationNum) {
        s = 2;
    }
    return qBound(1, s, 10);
}

bool recursiveCopyDir(const QString &srcPath, const QString &dstPath, QString *errorText)
{
    const QDir src(srcPath);
    if (!src.exists()) {
        if (errorText) {
            *errorText = QString("Не найдена папка-шаблон: %1").arg(srcPath);
        }
        return false;
    }
    QDir().mkpath(dstPath);

    const QFileInfoList entries = src.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    for (const QFileInfo &fi : entries) {
        const QString srcItem = fi.absoluteFilePath();
        const QString dstItem = QDir(dstPath).filePath(fi.fileName());
        if (fi.isDir()) {
            if (!recursiveCopyDir(srcItem, dstItem, errorText)) {
                return false;
            }
        } else if (fi.isFile()) {
            QFile::remove(dstItem);
            if (!QFile::copy(srcItem, dstItem)) {
                if (errorText) {
                    *errorText = QString("Не удалось скопировать файл %1 -> %2").arg(srcItem, dstItem);
                }
                return false;
            }
        }
    }
    return true;
}

bool parseTraktParamXml(const QByteArray &xml, QVector<TraktParamEntry> *outEntries, int *outTraktNum, QString *errorText)
{
    if (!outEntries) {
        return false;
    }
    outEntries->clear();
    if (outTraktNum) {
        *outTraktNum = 0;
    }

    QXmlStreamReader r(xml);
    int traktNum = 0;
    QString currentTraktBlock;
    TraktParamEntry current;
    bool inTraktBlock = false;

    auto finishCurrent = [&]() {
        if (!inTraktBlock) {
            return;
        }
        if (current.trmType > 0 && current.trLn > 0) {
            outEntries->push_back(current);
        }
        current = TraktParamEntry{};
        currentTraktBlock.clear();
        inTraktBlock = false;
    };

    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            const QStringRef n = r.name();
            if (n == QLatin1String("TraktNum")) {
                const QString t = r.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                bool ok = false;
                traktNum = t.toInt(&ok);
                if (!ok) {
                    traktNum = 0;
                }
                continue;
            }

            if (n.startsWith(QLatin1String("Trakt_"))) {
                finishCurrent();
                inTraktBlock = true;
                currentTraktBlock = n.toString();
                continue;
            }

            if (inTraktBlock) {
                if (n == QLatin1String("TrLN")) {
                    const QString t = r.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                    bool ok = false;
                    const int v = t.toInt(&ok);
                    if (ok) current.trLn = v;
                    continue;
                }
                if (n == QLatin1String("TrmType")) {
                    const QString t = r.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                    bool ok = false;
                    const int v = t.toInt(&ok);
                    if (ok) current.trmType = v;
                    continue;
                }
                if (n == QLatin1String("TrmNr")) {
                    const QString t = r.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                    bool ok = false;
                    const int v = t.toInt(&ok);
                    if (ok) current.trmNr = v;
                    continue;
                }
            }
        } else if (r.isEndElement()) {
            if (inTraktBlock && r.name().toString() == currentTraktBlock) {
                finishCurrent();
            }
        }
    }

    if (r.hasError()) {
        if (errorText) {
            *errorText = QString("Ошибка парсинга TraktParam.xml: %1").arg(r.errorString());
        }
        return false;
    }

    if (outTraktNum) {
        *outTraktNum = traktNum > 0 ? traktNum : outEntries->size();
    }
    return !outEntries->isEmpty();
}

bool patchChannelsXmlSelfAddr(const QString &filePath, const QSet<QString> &channels, int stationNum, QString *errorText)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorText) *errorText = QString("Не удалось открыть %1: %2").arg(filePath, f.errorString());
        return false;
    }
    const QByteArray srcBytes = f.readAll();
    f.close();

    // Не используем XML-парсер: некоторые файлы в шаблонах могут иметь "грязную" кодировку.
    // Теги/числа — ASCII, поэтому делаем замену по тексту.
    QString s = QString::fromLatin1(srcBytes);
    for (const QString &ch : channels) {
        const QRegularExpression re(
            QStringLiteral("(<%1\\b[^>]*>[\\s\\S]*?<SelfAddr>\\s*)(\\d+)(\\s*</SelfAddr>)").arg(QRegularExpression::escape(ch)),
            QRegularExpression::CaseInsensitiveOption);
        s.replace(re, QStringLiteral("\\1%1\\3").arg(stationNum));
    }

    QSaveFile sf(filePath);
    if (!sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorText) *errorText = QString("Не удалось записать %1: %2").arg(filePath, sf.errorString());
        return false;
    }
    sf.write(s.toLatin1());
    if (!sf.commit()) {
        if (errorText) *errorText = QString("Не удалось сохранить %1").arg(filePath);
        return false;
    }
    return true;
}

bool patchDirsXmlStationId(const QString &filePath, const QSet<QString> &dirs, int stationNum, QString *errorText)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorText) *errorText = QString("Не удалось открыть %1: %2").arg(filePath, f.errorString());
        return false;
    }
    const QByteArray srcBytes = f.readAll();
    f.close();

    QString s = QString::fromLatin1(srcBytes);
    for (const QString &dir : dirs) {
        const QRegularExpression re(
            QStringLiteral("(<%1\\b[^>]*>[\\s\\S]*?<StationId>\\s*)(\\d+)(\\s*</StationId>)").arg(QRegularExpression::escape(dir)),
            QRegularExpression::CaseInsensitiveOption);
        s.replace(re, QStringLiteral("\\1%1\\3").arg(stationNum));
    }

    QSaveFile sf(filePath);
    if (!sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorText) *errorText = QString("Не удалось записать %1: %2").arg(filePath, sf.errorString());
        return false;
    }
    sf.write(s.toLatin1());
    if (!sf.commit()) {
        if (errorText) *errorText = QString("Не удалось сохранить %1").arg(filePath);
        return false;
    }
    return true;
}

bool patchSrParsXmlStations(const QString &filePath, int stationNum, int otherStationNum, QString *errorText)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorText) *errorText = QString("Не удалось открыть %1: %2").arg(filePath, f.errorString());
        return false;
    }
    const QByteArray srcBytes = f.readAll();
    f.close();

    QString s = QString::fromLatin1(srcBytes);

    auto replaceInDiap = [&](const QString &diapTag, int val) {
        const QString v = QString::number(val);
        const QRegularExpression reBeg(
            QStringLiteral("(<%1\\b[^>]*>[\\s\\S]*?<StationBeg>\\s*)(\\d+)(\\s*</StationBeg>)").arg(QRegularExpression::escape(diapTag)),
            QRegularExpression::CaseInsensitiveOption);
        s.replace(reBeg, QStringLiteral("\\1%1\\3").arg(v));
        const QRegularExpression reEnd(
            QStringLiteral("(<%1\\b[^>]*>[\\s\\S]*?<StationEnd>\\s*)(\\d+)(\\s*</StationEnd>)").arg(QRegularExpression::escape(diapTag)),
            QRegularExpression::CaseInsensitiveOption);
        s.replace(reEnd, QStringLiteral("\\1%1\\3").arg(v));
    };
    replaceInDiap(QStringLiteral("SrDiap_1"), otherStationNum);
    replaceInDiap(QStringLiteral("SrDiap_2"), stationNum);

    QSaveFile sf(filePath);
    if (!sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorText) *errorText = QString("Не удалось записать %1: %2").arg(filePath, sf.errorString());
        return false;
    }
    sf.write(s.toLatin1());
    if (!sf.commit()) {
        if (errorText) *errorText = QString("Не удалось сохранить %1").arg(filePath);
        return false;
    }
    return true;
}

QByteArray extractElementInnerXml(const QByteArray &xml, const QString &elementName, QString *errorText)
{
    // Возвращает "внутренности" элемента (без внешних тегов), как XML.
    QXmlStreamReader r(xml);
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);
    int depth = 0;
    bool inside = false;

    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            const QString name = r.name().toString();
            if (!inside && name == elementName) {
                inside = true;
                depth = 0;
                continue;
            }
            if (inside) {
                ++depth;
                w.writeStartElement(name);
                for (const auto &a : r.attributes()) {
                    w.writeAttribute(a.name().toString(), a.value().toString());
                }
            }
        } else if (r.isEndElement()) {
            const QString name = r.name().toString();
            if (inside) {
                if (depth == 0 && name == elementName) {
                    inside = false;
                    break;
                }
                w.writeEndElement();
                --depth;
            }
        } else if (inside && r.isCharacters()) {
            w.writeCharacters(r.text().toString());
        } else if (inside && r.isComment()) {
            w.writeComment(r.text().toString());
        }
    }

    if (r.hasError()) {
        if (errorText) *errorText = r.errorString();
        return QByteArray();
    }
    if (!inside && out.isEmpty()) {
        if (errorText) *errorText = QString("Элемент %1 не найден").arg(elementName);
        return QByteArray();
    }
    return out;
}

QString extractTextElement(const QByteArray &xml, const QString &elementName)
{
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement() && r.name().toString() == elementName) {
            return r.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
        }
    }
    return QString();
}

bool generateTraktsXmlFromTemplate(const QString &templateTraktsPath,
                                  const QString &outTraktsPath,
                                  const QVector<TraktParamEntry> &entries,
                                  int totalTrakts,
                                  QString *errorText)
{
    QFile tf(templateTraktsPath);
    if (!tf.open(QIODevice::ReadOnly)) {
        if (errorText) *errorText = QString("Не удалось открыть шаблон Trakts.xml: %1").arg(tf.errorString());
        return false;
    }
    const QByteArray templ = tf.readAll();
    tf.close();

    // Достаём "шаблоны" внутренних частей Trakt_1..Trakt_4 из template Trakts.xml.
    QMap<int, QByteArray> innerByType;
    for (int t = 1; t <= 4; ++t) {
        QString err;
        const QByteArray inner = extractElementInnerXml(templ, QStringLiteral("Trakt_%1").arg(t), &err);
        if (inner.isEmpty()) {
            if (errorText) *errorText = QString("Не удалось извлечь шаблон Trakt_%1 из Trakts.xml: %2").arg(t).arg(err);
            return false;
        }
        innerByType.insert(t, inner);
    }
    const QString versionText = extractTextElement(templ, QStringLiteral("Version"));

    // Сортируем тракты по общему порядковому номеру (TrLN).
    // Это определяет соответствие TrId (и папки Trakt_n) физическому порядку трактов на станции.
    QVector<TraktParamEntry> sorted = entries;
    std::sort(sorted.begin(), sorted.end(), [](const TraktParamEntry &a, const TraktParamEntry &b) {
        if (a.trLn != b.trLn) return a.trLn < b.trLn;
        if (a.trmType != b.trmType) return a.trmType < b.trmType;
        return a.trmNr < b.trmNr;
    });

    // Ограничиваем количеством трактов из TraktNum (если в XML больше).
    if (totalTrakts > 0 && sorted.size() > totalTrakts) {
        sorted.resize(totalTrakts);
    }
    const int trNum = (totalTrakts > 0) ? totalTrakts : sorted.size();

    QByteArray out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("Trakts"));
    w.writeTextElement(QStringLiteral("TrNum"), QString::number(trNum));

    int trId = 1;
    for (const TraktParamEntry &e : sorted) {
        const int t = e.trmType;
        if (!innerByType.contains(t)) {
            continue;
        }
        w.writeStartElement(QStringLiteral("Trakt_%1").arg(trId));

        // Пишем внутренности шаблона, но TrId переопределяем.
        // Внутренности Trakt_* — это XML-фрагмент с несколькими соседними элементами,
        // поэтому оборачиваем в искусственный корень, чтобы QXmlStreamReader не падал
        // с "Extra content at end of document".
        const QByteArray wrapped = QByteArray("<Root>") + innerByType.value(t) + QByteArray("</Root>");
        QXmlStreamReader ir(wrapped);
        while (!ir.atEnd()) {
            ir.readNext();
            if (ir.isStartElement()) {
                const QString name = ir.name().toString();
                if (name == QStringLiteral("Root")) {
                    continue;
                }
                w.writeStartElement(name);
                for (const auto &a : ir.attributes()) {
                    w.writeAttribute(a.name().toString(), a.value().toString());
                }
                if (name == QStringLiteral("TrId")) {
                    ir.readElementText(QXmlStreamReader::SkipChildElements);
                    w.writeCharacters(QString::number(trId));
                    w.writeEndElement();
                }
            } else if (ir.isEndElement()) {
                if (ir.name().toString() == QStringLiteral("Root")) {
                    continue;
                }
                w.writeEndElement();
            } else if (ir.isCharacters() && !ir.isWhitespace()) {
                w.writeCharacters(ir.text().toString());
            }
        }
        if (ir.hasError()) {
            if (errorText) *errorText = QString("Ошибка парсинга шаблона Trakt_%1: %2").arg(t).arg(ir.errorString());
            return false;
        }

        w.writeEndElement(); // Trakt_<id>
        ++trId;
        if (trId > trNum) {
            break;
        }
    }

    if (versionText.isEmpty()) {
        w.writeTextElement(QStringLiteral("Version"), QStringLiteral("0"));
    } else {
        w.writeTextElement(QStringLiteral("Version"), versionText);
    }
    w.writeEndElement(); // Trakts
    w.writeEndDocument();

    QSaveFile sf(outTraktsPath);
    if (!sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorText) *errorText = QString("Не удалось записать Trakts.xml: %1").arg(sf.errorString());
        return false;
    }
    sf.write(out);
    if (!sf.commit()) {
        if (errorText) *errorText = QString("Не удалось сохранить %1").arg(outTraktsPath);
        return false;
    }
    return true;
}

bool rebuildTraktFoldersFromTemplate(const QString &profileRoot,
                                    const QVector<TraktParamEntry> &entries,
                                    int totalTrakts,
                                    int stationNum,
                                    QString *errorText)
{
    QVector<TraktParamEntry> sorted = entries;
    std::sort(sorted.begin(), sorted.end(), [](const TraktParamEntry &a, const TraktParamEntry &b) {
        if (a.trLn != b.trLn) return a.trLn < b.trLn;
        if (a.trmType != b.trmType) return a.trmType < b.trmType;
        return a.trmNr < b.trmNr;
    });
    if (totalTrakts > 0 && sorted.size() > totalTrakts) {
        sorted.resize(totalTrakts);
    }
    const int trNum = (totalTrakts > 0) ? totalTrakts : sorted.size();

    // В шаблонном профиле папки Trakt_1..Trakt_4 — это "эталоны" для типов.
    // Нам нужно создать Trakt_1..Trakt_N (по TrId), при этом исходные шаблоны нельзя удалять,
    // иначе копирование сломается. Поэтому временно переносим их в __tmpl_*.
    QDir root(profileRoot);
    const QString tmplPrefix = QStringLiteral("__tmpl_Trakt_");
    for (int t = 1; t <= 4; ++t) {
        const QString src = root.filePath(QStringLiteral("Trakt_%1").arg(t));
        const QString dst = root.filePath(QStringLiteral("%1%2").arg(tmplPrefix).arg(t));
        if (!QDir(src).exists()) {
            if (errorText) {
                *errorText = QString("Не найдена папка-шаблон: %1").arg(src);
            }
            return false;
        }
        // Если вдруг осталось от прошлого раза — удалим и перезапишем.
        if (QDir(dst).exists()) {
            QDir(dst).removeRecursively();
        }
        if (!root.rename(QStringLiteral("Trakt_%1").arg(t), QStringLiteral("%1%2").arg(tmplPrefix).arg(t))) {
            // fallback: если rename не сработал (например, на разных FS), просто копируем
            if (!recursiveCopyDir(src, dst, errorText)) {
                return false;
            }
            QDir(src).removeRecursively();
        }
    }

    // Удаляем существующие Trakt_* (если были) — кроме __tmpl_*.
    const QStringList old = root.entryList(QStringList() << "Trakt_*", QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : old) {
        // защита от удаления __tmpl_*
        if (name.startsWith(tmplPrefix)) {
            continue;
        }
        QDir(root.filePath(name)).removeRecursively();
    }

    const int otherStation = pickOtherStationNum(stationNum);

    for (int idx = 0; idx < trNum; ++idx) {
        const int trId = idx + 1;
        const int type = sorted.value(idx).trmType;
        const QString srcDir = root.filePath(QStringLiteral("%1%2").arg(tmplPrefix).arg(type));
        const QString dstDir = QDir(profileRoot).filePath(QStringLiteral("Trakt_%1").arg(trId));
        if (!recursiveCopyDir(srcDir, dstDir, errorText)) {
            return false;
        }

        // Патчим файлы внутри папки в зависимости от типа.
        if (type == 2) {
            const QString channels = QDir(dstDir).filePath(QStringLiteral("Channels.xml"));
            if (!patchChannelsXmlSelfAddr(channels,
                                          QSet<QString>() << QStringLiteral("Channel_2") << QStringLiteral("Channel_3"),
                                          stationNum, errorText)) {
                return false;
            }
        } else if (type == 3) {
            const QString channels = QDir(dstDir).filePath(QStringLiteral("Channels.xml"));
            if (!patchChannelsXmlSelfAddr(channels,
                                          QSet<QString>() << QStringLiteral("Channel_2") << QStringLiteral("Channel_3")
                                                          << QStringLiteral("Channel_4") << QStringLiteral("Channel_5"),
                                          stationNum, errorText)) {
                return false;
            }
            const QString dirsXml = QDir(dstDir).filePath(QStringLiteral("Dirs.xml"));
            if (!patchDirsXmlStationId(dirsXml, QSet<QString>() << QStringLiteral("Dir_5"), stationNum, errorText)) {
                return false;
            }
            const QString srPars = QDir(dstDir).filePath(QStringLiteral("SrPars.xml"));
            if (!patchSrParsXmlStations(srPars, stationNum, otherStation, errorText)) {
                return false;
            }
        } else if (type == 4) {
            const QString channels = QDir(dstDir).filePath(QStringLiteral("Channels.xml"));
            if (!patchChannelsXmlSelfAddr(channels,
                                          QSet<QString>() << QStringLiteral("Channel_2") << QStringLiteral("Channel_3")
                                                          << QStringLiteral("Channel_4"),
                                          stationNum, errorText)) {
                return false;
            }
            const QString dirsXml = QDir(dstDir).filePath(QStringLiteral("Dirs.xml"));
            if (!patchDirsXmlStationId(dirsXml, QSet<QString>() << QStringLiteral("Dir_3"), stationNum, errorText)) {
                return false;
            }
            const QString srPars = QDir(dstDir).filePath(QStringLiteral("SrPars.xml"));
            if (!patchSrParsXmlStations(srPars, stationNum, otherStation, errorText)) {
                return false;
            }
        }
    }

    // Убираем временные шаблонные папки из профиля перед упаковкой.
    for (int t = 1; t <= 4; ++t) {
        const QString dst = root.filePath(QStringLiteral("%1%2").arg(tmplPrefix).arg(t));
        if (QDir(dst).exists()) {
            QDir(dst).removeRecursively();
        }
    }
    return true;
}

bool runTar(const QStringList &args, QString *errorText)
{
    QProcess p;
    p.start(QStringLiteral("tar"), args);
    if (!p.waitForFinished(30000)) {
        p.kill();
        p.waitForFinished(2000);
        if (errorText) *errorText = QStringLiteral("Timeout выполнения tar %1").arg(args.join(' '));
        return false;
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        const QString out = QString::fromUtf8(p.readAllStandardOutput());
        const QString err = QString::fromUtf8(p.readAllStandardError());
        if (errorText) *errorText = QString("tar ошибка (exitCode=%1): %2%3").arg(p.exitCode()).arg(out).arg(err);
        return false;
    }
    return true;
}

bool loadTraktParamFromStationOverSsh(SSHer &ssher,
                                     QVector<TraktParamEntry> *outEntries,
                                     int *outTraktNum,
                                     QString *errorText)
{
    if (!outEntries || !outTraktNum) {
        if (errorText) {
            *errorText = QStringLiteral("Внутренняя ошибка: не заданы выходные параметры TraktParam.");
        }
        return false;
    }

    QTemporaryFile traktTmp(QDir::tempPath() + "/TraktParam_XXXXXX.xml");
    traktTmp.setAutoRemove(true);
    if (!traktTmp.open()) {
        if (errorText) {
            *errorText = QString("Не удалось создать временный файл TraktParam.xml: %1").arg(traktTmp.errorString());
        }
        return false;
    }
    const QString traktLocal = traktTmp.fileName();
    traktTmp.close();

    if (!ssher.downloadFile(QString::fromLatin1(kTraktParamRemotePath), traktLocal)) {
        if (errorText) {
            *errorText = ssher.lastError().isEmpty()
                              ? QString("Не удалось скачать %1").arg(QString::fromLatin1(kTraktParamRemotePath))
                              : ssher.lastError();
        }
        return false;
    }
    QFile traktFile(traktLocal);
    if (!traktFile.open(QIODevice::ReadOnly)) {
        if (errorText) {
            *errorText = QString("Не удалось прочитать TraktParam.xml: %1").arg(traktFile.errorString());
        }
        return false;
    }
    const QByteArray traktXml = traktFile.readAll();
    traktFile.close();

    return parseTraktParamXml(traktXml, outEntries, outTraktNum, errorText);
}

bool buildCustomizedProfileArchive(const QString &stationIp,
                                  SSHer &ssher,
                                  const QString &templateTarPath,
                                  const QString &outTarPath,
                                  QString *errorText,
                                  QVector<TraktParamEntry> *outEntriesForUi = nullptr,
                                  int *outTraktNumForUi = nullptr,
                                  const QVector<TraktParamEntry> *traktEntriesIn = nullptr,
                                  int traktNumIn = 0)
{
    bool okStation = false;
    const int stationNum = stationNumFromIp(stationIp, &okStation);
    if (!okStation || stationNum <= 0) {
        if (errorText) *errorText = QString("Не удалось определить номер радиостанции из IP: %1").arg(stationIp);
        return false;
    }

    QVector<TraktParamEntry> traktEntries;
    int traktNum = 0;
    if (traktEntriesIn != nullptr) {
        traktEntries = *traktEntriesIn;
        traktNum = traktNumIn;
    } else if (!loadTraktParamFromStationOverSsh(ssher, &traktEntries, &traktNum, errorText)) {
        return false;
    }

    // 2) Распаковываем шаблонный архив в временную папку
    QTemporaryDir workDir(QDir::tempPath() + "/profile_build_XXXXXX");
    if (!workDir.isValid()) {
        if (errorText) *errorText = QStringLiteral("Не удалось создать временную директорию для сборки профиля.");
        return false;
    }
    QString tarErr;
    if (!runTar(QStringList() << "-xf" << templateTarPath << "-C" << workDir.path(), &tarErr)) {
        if (errorText) *errorText = tarErr;
        return false;
    }

    const QString profileRoot = QDir(workDir.path()).filePath(QString::fromLatin1(kTemplateProfileRootDirName));
    const QString traktsPath = QDir(profileRoot).filePath(QStringLiteral("Trakts.xml"));

    // 3) Пересобираем Trakts.xml
    if (!generateTraktsXmlFromTemplate(traktsPath, traktsPath, traktEntries, traktNum, errorText)) {
        return false;
    }

    // 4) Пересобираем папки Trakt_n
    if (!rebuildTraktFoldersFromTemplate(profileRoot, traktEntries, traktNum, stationNum, errorText)) {
        return false;
    }

    // 5) Упаковываем новый архив (ВАЖНО: без gzip, чтобы tar -xf работал как сейчас)
    if (QFileInfo::exists(outTarPath)) {
        QFile::remove(outTarPath);
    }
    if (!runTar(QStringList() << "-cf" << outTarPath << "-C" << workDir.path() << QString::fromLatin1(kTemplateProfileRootDirName),
                &tarErr)) {
        if (errorText) *errorText = tarErr;
        return false;
    }

    if (outEntriesForUi) {
        *outEntriesForUi = traktEntries;
    }
    if (outTraktNumForUi) {
        *outTraktNumForUi = traktNum;
    }
    return true;
}
} // namespace

static QString ppmErrorCodeToText(int code)
{
    // Значения считаем из enum ppmErrorCodes (см. исходник пульта):
    // ERRCODE_NOERROR=0, ERRCODE_PPM_NOANSWER=1, ERRCODE_RL_WRONGMODE=2, ...
    constexpr int ERRCODE_NOERROR = 0;
    constexpr int ERRCODE_PPM_NOANSWER = 1;
    constexpr int ERRCODE_RL_WRONGMODE = 2;
    constexpr int ERRCODE_OPSES_NODATA = 3;
    constexpr int ERRCODE_PPM_LUM_OVERHEAT = 4;
    constexpr int ERRCODE_PPM_SWR_ERROR = 5;
    constexpr int ERRCODE_PPM_ANT_NOTTUNED = 6;
    constexpr int ERRCODE_PPM_NOWRK = 7;
    constexpr int ERRCODE_PPM_NO = 8;
    constexpr int ERRCODE_RETR_NO = 9;
    constexpr int ERRCODE_PPM_START = 10;

    // Совместимость: иногда "Запуск ПП" прилетает как 0xFFFF (старое/альтернативное кодирование).
    const quint16 rawCode = static_cast<quint16>(static_cast<qint16>(code));
    if (rawCode == 0xFFFFu || code == ERRCODE_PPM_START) {
        return QObject::tr("Запуск ПП");
    }

    switch (code) {
    case ERRCODE_NOERROR:
        return QObject::tr("Норма");
    case ERRCODE_PPM_NOANSWER:
        return QObject::tr("Нет связи с ПП");
    case ERRCODE_RL_WRONGMODE:
        return QObject::tr("Неверный режим");
    case ERRCODE_OPSES_NODATA:
        return QObject::tr("Нет радиоданных");
    case ERRCODE_PPM_LUM_OVERHEAT:
        return QObject::tr("Перегрев ЛУМ");
    case ERRCODE_PPM_SWR_ERROR:
        return QObject::tr("Авария АНТ");
    case ERRCODE_PPM_NOWRK:
        return QObject::tr("Запрет ПРД");
    case ERRCODE_PPM_NO:
        return QObject::tr("ПП не готов");
    case ERRCODE_PPM_ANT_NOTTUNED:
        return QObject::tr("АНТ не настроена");
    case ERRCODE_RETR_NO:
        return QObject::tr("RETR не готов");
    default:
        if (code < 0) {
            return QString();
        }
        return QObject::tr("Код %1").arg(code);
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_deviceController(new DeviceController(this))
    , m_analyzerController(new AnalyzerController(this))
    , m_finder(new FindManager(this))
{
    ui->setupUi(this);

    if (ui->horizontalLayout) {
        ui->horizontalLayout->setStretch(0, 1);
        ui->horizontalLayout->setStretch(1, 1);
    }
    if (ui->verticalLayout_5) {
        // Важно: не растягиваем шапку (frameStation/frameR3).
        // Распределяем высоту только между tabWidget и блоком лога.
        ui->verticalLayout_5->setStretch(0, 0); // header
        ui->verticalLayout_5->setStretch(1, 8); // tabWidget (графики/управление)
        ui->verticalLayout_5->setStretch(2, 3); // лог
    }
    if (ui->logTextEdit) {
        // На старте лог не должен "съедать" область графика.
        ui->logTextEdit->setMinimumHeight(120);
    }
    if (ui->frameStation && ui->frameR3) {
        ui->frameStation->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        ui->frameR3->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    setApplicationLogTextSink([this](const QString &msg) { appendDeviceLogLine(msg); });

    // Для tabFHSS делаем поведение по вертикальному растяжению таким же, как в tabPower:
    // лишняя высота должна уходить в график, а не в нижний блок настроек.
    if (ui->verticalLayout_12) {
        ui->verticalLayout_12->setStretch(0, 1);
        ui->verticalLayout_12->setStretch(1, 0);
    }
    m_uptime.start();
    initRuntimeTimerWidget();
    updateRuntimeTimerDisplay();
    m_runtimeDisplayTimer.setInterval(1000);
    m_runtimeDisplayTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_runtimeDisplayTimer, &QTimer::timeout, this, &MainWindow::updateRuntimeTimerDisplay);
    m_runtimeDisplayTimer.start();
    configureFrameStationHeaderLayout();
    initPpmUiStyle();
    // if (ui->tabWidget) {
    //     if (QTabBar *tabs = ui->tabWidget->tabBar()) {
    //         tabs->setExpanding(true);
    //         tabs->setUsesScrollButtons(false);
    //         tabs->setElideMode(Qt::ElideNone);
    //         QFont tabFont = tabs->font();
    //         tabFont.setFamily(QStringLiteral("Consolas"));
    //         tabFont.setPointSize(10);
    //         tabFont.setItalic(false);
    //         tabFont.setBold(false);
    //         tabs->setFont(tabFont);
    //     }
    // }

    syncHandsFreqLineEdits(static_cast<quint64>(ANALYZER_STREAM_START_HZ_DEFAULT),
                           static_cast<quint64>(ANALYZER_STREAM_STOP_HZ_DEFAULT));
    syncSweepBoundsFromHz(static_cast<quint64>(ANALYZER_STREAM_START_HZ_DEFAULT),
                          static_cast<quint64>(ANALYZER_STREAM_STOP_HZ_DEFAULT));
    initSpectrumSpanCombo();
    syncSpectrumCenterSpanFromRangeHz(static_cast<quint64>(ANALYZER_STREAM_START_HZ_DEFAULT),
                                      static_cast<quint64>(ANALYZER_STREAM_STOP_HZ_DEFAULT),
                                      false);
    connect(ui->pushButtonChangeRange, &QPushButton::clicked, this, &MainWindow::onHandsSpectrumApplyClicked);
    connect(ui->pushButtonSpectrumCenterApply, &QPushButton::clicked, this,
            &MainWindow::onSpectrumCenterSpanApplyClicked);
    if (ui->pushButtonSpectrumCenterApply) {
        ui->pushButtonSpectrumCenterApply->setAutoDefault(false);
        ui->pushButtonSpectrumCenterApply->setDefault(false);
    }

    // Применяем стиль графика сразу после запуска
    initSpectrumPlot();

    connect(m_deviceController, &DeviceController::connected,
            this, &MainWindow::onDeviceConnected);
    connect(m_deviceController, &DeviceController::disconnected,
            this, &MainWindow::onDeviceDisconnected);
    connect(m_deviceController, &DeviceController::logMessage, this, [this](const QString &msg) {
        if (!debug) {
            return;
        }
        onDeviceLogMessage(msg);
    });
    connect(m_deviceController, &DeviceController::errorOccurred,
            this, &MainWindow::onDeviceError);
    connect(m_deviceController, &DeviceController::tractPowerAwaitingAck,
            this, &MainWindow::onTractPowerAwaitingAck);
    connect(m_deviceController, &DeviceController::tractPowerAcknowledged,
            this, &MainWindow::onTractPowerAcknowledged);
    connect(m_deviceController, &DeviceController::tractPowerAckTimeout,
            this, &MainWindow::onTractPowerAckTimeout);
    connect(m_deviceController, &DeviceController::tractPowerIndicationReceived,
            this, &MainWindow::onTractPowerIndicationReceived);
    connect(m_deviceController, &DeviceController::freqRxIndicationReceived,
            this, &MainWindow::onFreqRxIndicationReceived);
    connect(m_deviceController, &DeviceController::freqTxIndicationReceived,
            this, &MainWindow::onFreqTxIndicationReceived);
    connect(m_deviceController, &DeviceController::rssiIndicationReceived,
            this, &MainWindow::onRssiIndicationReceived);
    connect(m_deviceController, &DeviceController::powerLevelIndicationReceived,
            this, &MainWindow::onPowerLevelIndicationReceived);
    connect(m_deviceController, &DeviceController::ppmStatusIndicationReceived,
            this, &MainWindow::onPpmStatusIndicationReceived);
    connect(m_deviceController, &DeviceController::channelReadyIndicationReceived,
            this, &MainWindow::onChannelReadyIndicationReceived);
    connect(m_deviceController, &DeviceController::linkStatusIndicationReceived,
            this, &MainWindow::onLinkStatusIndicationReceived);
    connect(m_deviceController, &DeviceController::workModeIndicationReceived,
            this, &MainWindow::onWorkModeIndicationReceived);
    connect(m_deviceController, &DeviceController::activeDirectionIndicationReceived,
            this, &MainWindow::onActiveDirectionIndicationReceived);
    connect(m_deviceController, &DeviceController::profileSwitchIndicationReceived,
            this, &MainWindow::onProfileSwitchIndicationReceived);

    m_powerTrafficGenerator = new PowerTrafficGenerator(this);
    connect(m_powerTrafficGenerator, &PowerTrafficGenerator::logMessage,
            this, [](const QString &msg) { DEBUG << msg; });
    connect(m_powerTrafficGenerator, &PowerTrafficGenerator::errorOccurred,
            this, &MainWindow::onDeviceError);

    initStatusLedGlow();
    setStationDisconnectedUi();
    setAnalyzerDisconnectedUi();
    if (kAnalyzerRepairBypass) {
        m_analyzerConnected = true;
        m_analyzerDisconnectRecoveryActive = false;
        onDeviceLogMessage(
            QStringLiteral("РЕЖИМ РАЗРАБОТКИ: анализатор недоступен (ремонт), аппаратные блокировки отключены."));
    }
    ui->frameStation->setVisible(true);
    ui->frameR3->setVisible(true);
    onDeviceLogMessage("Приложение запущено. Поиск ethernet-интерфейсов...");

    connect(m_analyzerController, &AnalyzerController::analyzerConnected,
            this, &MainWindow::onAnalyzerConnected);
    connect(m_analyzerController, &AnalyzerController::analyzerDisconnected,
            this, &MainWindow::onAnalyzerDisconnected);
    connect(m_analyzerController, &AnalyzerController::logMessage,
            this, &MainWindow::onAnalyzerLogMessage);
    connect(m_analyzerController, &AnalyzerController::spectrumDataReceived,
            this, &MainWindow::onSpectrumDataReceived);

    // Подключение к анализатору должно начинаться автоматически при старте приложения.
    m_analyzerController->connectToDefaultPort();

    // Поиск интерфейсов/станций должен запускаться при старте программы.
    startAutoDiscovery();

    // Спектр: автозапуск при входе на вкладку tabHands
    connect(ui->tabWidget, &QTabWidget::currentChanged,
            this, &MainWindow::onTabWidgetCurrentChanged);

    m_tabHandsIndex =
        ui->tabWidget->indexOf(ui->tabWidget->findChild<QWidget *>("tabHands",
                                                                  Qt::FindDirectChildrenOnly));
    m_tabPowerIndex =
        ui->tabWidget->indexOf(ui->tabWidget->findChild<QWidget *>("tabPower",
                                                                  Qt::FindDirectChildrenOnly));
    m_tabReceiveIndex =
        ui->tabWidget->indexOf(ui->tabWidget->findChild<QWidget *>("tabRecieve",
                                                                   Qt::FindDirectChildrenOnly));
    m_tabFhssIndex =
        ui->tabWidget->indexOf(ui->tabWidget->findChild<QWidget *>("tabFHSS",
                                                                   Qt::FindDirectChildrenOnly));
    if (m_tabHandsIndex < 0 || m_tabPowerIndex < 0 || m_tabReceiveIndex < 0 || m_tabFhssIndex < 0) {
        for (int i = 0; i < ui->tabWidget->count(); ++i) {
            QWidget *w = ui->tabWidget->widget(i);
            if (!w) {
                continue;
            }
            if (m_tabHandsIndex < 0 && w->objectName() == QStringLiteral("tabHands")) {
                m_tabHandsIndex = i;
            }
            if (m_tabPowerIndex < 0 && w->objectName() == QStringLiteral("tabPower")) {
                m_tabPowerIndex = i;
            }
            if (m_tabReceiveIndex < 0 && w->objectName() == QStringLiteral("tabRecieve")) {
                m_tabReceiveIndex = i;
            }
            if (m_tabFhssIndex < 0 && w->objectName() == QStringLiteral("tabFHSS")) {
                m_tabFhssIndex = i;
            }
        }
    }
    onTabWidgetCurrentChanged(ui->tabWidget->currentIndex());

    if (QPushButton *holdBtn = ui->pushButtonSpectrumMaxHold) {
        holdBtn->setCheckable(true);
        holdBtn->setAutoDefault(false);
        holdBtn->setDefault(false);
        connect(holdBtn, &QPushButton::toggled, this, &MainWindow::onSpectrumMaxHoldToggled);
    }

    initPowerTestingUi();
    initReceiveTestingUi();
    initFhssTestingUi();

    if (QPushButton *savePlotBtn = ui->pushButtonSpectrumSavePlot) {
        savePlotBtn->setAutoDefault(false);
        savePlotBtn->setDefault(false);
        connect(savePlotBtn, &QPushButton::clicked, this, &MainWindow::onSpectrumSavePlotClicked);
    }

    if (QPushButton *toggleLogBtn = ui->pushButtonToggleLog) {
        toggleLogBtn->setAutoDefault(false);
        toggleLogBtn->setDefault(false);
        connect(toggleLogBtn, &QPushButton::clicked, this, &MainWindow::onToggleLogVisibilityClicked);
    }
    updateLogToggleButtonText();

    if (ui->horizontalSliderBW) {
        updateSpectrumBwUi(ui->horizontalSliderBW->value());
        connect(ui->horizontalSliderBW, &QSlider::valueChanged,
                this, &MainWindow::onSpectrumBwSliderChanged);
    }

    if (ui->pushButtonStartTesting) {
        ui->pushButtonStartTesting->setCheckable(false);
        initStartTestingButtonGlow();
        connect(ui->pushButtonStartTesting, &QPushButton::clicked,
                this, &MainWindow::onStartTestingClicked);
    }
    m_spectrumUiTimer.setInterval(33);
    m_spectrumUiTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_spectrumUiTimer, &QTimer::timeout, this, &MainWindow::onSpectrumUiTimer);
    m_fhssUiTimer.setInterval(33);
    m_fhssUiTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_fhssUiTimer, &QTimer::timeout, this, &MainWindow::onFhssUiTimer);

    // По ТЗ: до успешной подготовки профиля кнопка старта должна быть заблокирована.
    setStartTestingButtonEnabled(false);

    m_postRebootWaitTimer.setSingleShot(true);
    m_postRebootWaitTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_postRebootWaitTimer, &QTimer::timeout, this, &MainWindow::onPostRebootWaitTimeout);

    m_postRebootWaitProgressTimer.setInterval(200);
    m_postRebootWaitProgressTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_postRebootWaitProgressTimer, &QTimer::timeout, this, &MainWindow::onPostRebootWaitProgressTick);

    m_postRebootReconnectTimer.setInterval(1000);
    m_postRebootReconnectTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_postRebootReconnectTimer, &QTimer::timeout, this, &MainWindow::onPostRebootReconnectTick);

    m_bkuKernelBootWaitTimer.setSingleShot(true);
    m_bkuKernelBootWaitTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_bkuKernelBootWaitTimer, &QTimer::timeout, this, &MainWindow::onBkuPostKernelBootWaitTimeout);

    m_bkuKernelBootWaitProgressTimer.setInterval(200);
    m_bkuKernelBootWaitProgressTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_bkuKernelBootWaitProgressTimer, &QTimer::timeout, this,
            &MainWindow::onBkuPostKernelBootWaitProgressTick);

    m_postReconnectStationBootProgressTimer.setInterval(200);
    m_postReconnectStationBootProgressTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_postReconnectStationBootProgressTimer, &QTimer::timeout, this,
            &MainWindow::onPostReconnectStationBootProgressTick);
    m_postReconnectStationBootFallbackTimer.setSingleShot(true);
    m_postReconnectStationBootFallbackTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_postReconnectStationBootFallbackTimer, &QTimer::timeout, this,
            &MainWindow::onPostReconnectStationBootFallbackTimeout);

    m_powerTestAutoStopTimer.setSingleShot(true);
    m_powerTestAutoStopTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_powerTestAutoStopTimer, &QTimer::timeout, this, [this]() {
        if (!ui || !ui->pushButtonStartTestingPower || !ui->pushButtonStartTestingPower->isChecked()
            || !m_powerMeasurementRunning) {
            return;
        }
        finishPowerMeasurementStep();
    });
    m_powerTestStepPauseTimer.setSingleShot(true);
    m_powerTestStepPauseTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_powerTestStepPauseTimer, &QTimer::timeout, this, [this]() {
        if (!ui || !ui->pushButtonStartTestingPower || !ui->pushButtonStartTestingPower->isChecked()) {
            return;
        }
        if (!startPowerMeasurementStep()) {
            if (m_powerTestPaused) {
                setPowerTestControlsRunning(true);
                return;
            }
            ui->pushButtonStartTestingPower->setChecked(false);
        }
    });

    m_receiveTestTickTimer.setInterval(1000);
    m_receiveTestTickTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_receiveTestTickTimer, &QTimer::timeout, this, &MainWindow::onReceiveTestTick);
    m_powerTestBeforePowerOnTimer.setSingleShot(true);
    m_powerTestBeforePowerOnTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_powerTestBeforePowerOnTimer, &QTimer::timeout, this, [this]() {
        if (!ui || !ui->pushButtonStartTestingPower || !ui->pushButtonStartTestingPower->isChecked()
            || !m_powerTrafficStartPending) {
            return;
        }
        if (!m_powerTrafficGenerator || !m_deviceController || !m_deviceController->isConnected()) {
            DEBUG << QStringLiteral("ОШИБКА: не удалось начать подачу мощности после паузы (нет подключения).");
            ui->pushButtonStartTestingPower->setChecked(false);
            return;
        }

        m_powerTrafficGenerator->setBindIp(m_deviceController->config().selfIp);
        m_powerTrafficGenerator->setMulticastAddress(m_powerTestMulticastAddress);
        m_powerTrafficGenerator->setMulticastPort(TRAFFIC_DST_PORT);
        m_powerTrafficGenerator->setSourcePort(TRAFFIC_SRC_PORT);
        m_powerTrafficGenerator->setDscp(DSCP_STREAMVOICE);
        m_powerTrafficGenerator->setEcn(ECN_DEFAULT);
        m_powerTrafficGenerator->setPayloadType(RTP_PAYLOAD_TYPE);
        m_powerTrafficGenerator->setTractNumber(m_powerTestTargetTract);

        if (!m_powerTrafficGenerator->start()) {
            m_powerTrafficStartPending = false;
            DEBUG << QStringLiteral("ОШИБКА: не удалось запустить генератор трафика после паузы.");
            ui->pushButtonStartTestingPower->setChecked(false);
            return;
        }

        m_powerTrafficStartPending = false;
        m_powerMeasurementRunning = true;
        m_powerTestAutoStopTimer.start(kPowerTestDurationMs);
        DEBUG << QStringLiteral("▶ Подача мощности включена, идет окно измерения 5 секунд.");
    });

    updateTabWidgetLockState();
}

MainWindow::~MainWindow()
{
    setApplicationLogTextSink({});
    performShutdownCleanup();

    delete ui;
}

QPair<bool, QString> MainWindow::executeCommand(const QString &command) const
{
    QProcess process;
    process.start("/bin/bash", QStringList() << "-c" << command);
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(2000);
        return {false, "Timeout выполнения команды: " + command};
    }
    const QString out = QString::fromUtf8(process.readAllStandardOutput());
    const QString err = QString::fromUtf8(process.readAllStandardError());
    const QString combined = out + err;
    const bool ok = (process.exitStatus() == QProcess::NormalExit) && (process.exitCode() == 0);
    return {ok, combined};
}

void MainWindow::cleanupAddedSelfIp()
{
    if (m_cleanupDone) {
        return;
    }
    m_cleanupDone = true;

    if (m_addedIps.isEmpty()) {
        return;
    }

    // Удаляем в обратном порядке добавления — так удобнее для логов/отладки.
    for (int i = m_addedIps.size() - 1; i >= 0; --i) {
        const AddedIpEntry &e = m_addedIps[i];
        const QString iface = e.iface.trimmed();
        const QString selfIp = e.ip.trimmed();
        const int cidr = e.cidr;

        if (iface.isEmpty() || selfIp.isEmpty() || cidr <= 0) {
            continue;
        }

        QString connectionUuid = e.connectionUuid.trimmed();
        if (connectionUuid.isEmpty()) {
            // Самый надёжный способ — спросить у nmcli активное соединение для DEVICE.
            const QString command = QString("nmcli -t -f UUID,DEVICE connection show --active | grep -F \":%1\" | cut -d':' -f1")
                                        .arg(iface);
            const QPair<bool, QString> result = executeCommand(command);
            connectionUuid = result.second.trimmed().split('\n', Qt::SkipEmptyParts).value(0).trimmed();
            if (!result.first || connectionUuid.isEmpty()) {
                onDeviceLogMessage(QString("Не удалось определить активное соединение для %1, очистка self-IP %2/%3 пропущена.")
                                       .arg(iface, selfIp).arg(cidr));
                continue;
            }
        }

        const QString ipWithMask = QString("%1/%2").arg(selfIp).arg(cidr);
        QString command = QString("nmcli connection modify uuid \"%1\" -ipv4.addresses %2").arg(connectionUuid, ipWithMask);
        QPair<bool, QString> result = executeCommand(command);

        // Если не получилось без sudo — пробуем с sudo (часто профили требуют прав).
        if (!result.first) {
            command = QString("sudo nmcli connection modify uuid \"%1\" -ipv4.addresses %2").arg(connectionUuid, ipWithMask);
            result = executeCommand(command);
        }

        if (!result.first) {
            onDeviceLogMessage(QString("Не удалось удалить self-IP %1 из UUID \"%2\": %3")
                                   .arg(ipWithMask, connectionUuid, result.second.trimmed()));
            continue;
        }

        // Применяем изменения (переподнимаем интерфейс).
        executeCommand(QString("sudo nmcli device disconnect %1").arg(iface));
        executeCommand(QString("sudo nmcli device connect %1").arg(iface));
        onDeviceLogMessage(QString("Удалён добавленный self-IP %1 (интерфейс %2)").arg(ipWithMask, iface));
    }
}

void MainWindow::performShutdownCleanup()
{
    if (m_shutdownCleanupDone) {
        return;
    }
    m_shutdownCleanupDone = true;

    cleanupAddedSelfIp();
}

void MainWindow::runShutdownCleanupWithProgress()
{
    if (ui && ui->progressBar) {
        showStationHeaderCenter(StationHeaderCenter::ProgressBar);
        ui->progressBar->setTextVisible(false);
        ui->progressBar->setRange(0, 0);
        ui->progressBar->setValue(0);
    }
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    performShutdownCleanup();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    runShutdownCleanupWithProgress();
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Qt/Fusion иногда "перекрашивает" popup комбобокса при показе.
    // Поддерживаем фон выпадающих списков без белых полос.
    if (event && event->type() == QEvent::Show) {
        if (watched == ui->modeFHSSComboBox || watched == ui->comboBoxSpectrumSpanMHz) {
            polishComboDropDownSurface(qobject_cast<QComboBox *>(watched));
        }
    }
    if (event && event->type() == QEvent::MouseButtonRelease) {
        QComboBox *openCombo = nullptr;
        if (watched == ui->modeFHSSComboBox) {
            openCombo = ui->modeFHSSComboBox;
        } else if (ui->modeFHSSComboBox && watched == ui->modeFHSSComboBox->lineEdit()) {
            openCombo = ui->modeFHSSComboBox;
        } else if (watched == ui->comboBoxSpectrumSpanMHz) {
            openCombo = ui->comboBoxSpectrumSpanMHz;
        } else if (ui->comboBoxSpectrumSpanMHz && watched == ui->comboBoxSpectrumSpanMHz->lineEdit()) {
            openCombo = ui->comboBoxSpectrumSpanMHz;
        }
        if (openCombo && openCombo->isEnabled()) {
            const auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton && (!openCombo->view() || !openCombo->view()->isVisible())) {
                openCombo->showPopup();
                return true;
            }
        }
    }
    if (watched == ui->plotWidgetPowerGraph && event->type() == QEvent::Leave) {
        hidePowerGraphHoverLabel();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::hidePowerGraphHoverLabel()
{
    if (!m_powerGraphHoverLabel || !ui || !ui->plotWidgetPowerGraph) {
        return;
    }
    if (!m_powerGraphHoverLabel->visible()) {
        return;
    }
    m_powerGraphHoverLabel->setVisible(false);
    ui->plotWidgetPowerGraph->replot(QCustomPlot::rpQueuedReplot);
}

void MainWindow::initPowerGraphHelperRects()
{
    if (!ui || !ui->plotWidgetPowerGraph) {
        return;
    }

    // Переинициализация: убираем старые прямоугольники корректно через QCustomPlot,
    // т.к. items регистрируются в нём автоматически и находятся в его владении.
    for (QCPItemRect *r : m_powerGraphHelperRects) {
        if (!r) {
            continue;
        }
        ui->plotWidgetPowerGraph->removeItem(r);
    }
    m_powerGraphHelperRects.clear();

    auto addRectWithVerticalGradient = [this](double yTop,
                                              double yBottom,
                                              const QVector<QPair<double, QColor>> &stops) {
        if (!ui || !ui->plotWidgetPowerGraph) {
            return;
        }
        const QCPRange xr = ui->plotWidgetPowerGraph->xAxis->range();

        QLinearGradient grad(0, 0, 0, 1);
        grad.setCoordinateMode(QGradient::ObjectBoundingMode);
        for (const auto &s : stops) {
            grad.setColorAt(s.first, s.second);
        }

        QCPItemRect *r = new QCPItemRect(ui->plotWidgetPowerGraph);
        r->setLayer(QStringLiteral("background"));
        r->setClipToAxisRect(true);
        r->setPen(Qt::NoPen);
        r->setBrush(QBrush(grad));

        r->topLeft->setType(QCPItemPosition::ptPlotCoords);
        r->bottomRight->setType(QCPItemPosition::ptPlotCoords);
        r->topLeft->setAxes(ui->plotWidgetPowerGraph->xAxis, ui->plotWidgetPowerGraph->yAxis);
        r->bottomRight->setAxes(ui->plotWidgetPowerGraph->xAxis, ui->plotWidgetPowerGraph->yAxis);
        r->topLeft->setCoords(xr.lower, yTop);
        r->bottomRight->setCoords(xr.upper, yBottom);

        m_powerGraphHelperRects.push_back(r);
    };

    const QColor greenBase(QStringLiteral("#4ade80"));
    const QColor redMuted(QStringLiteral("#5f2f35"));
    const QColor redStrong(QStringLiteral("#991b1b"));

    auto withAlpha = [](QColor c, int a) {
        c.setAlpha(qBound(0, a, 255));
        return c;
    };

    const double centerDbm = m_powerGraphAutoYCenterDbm;

    // Зеленая зона: center±kPowerGraphGreenHalfWidthDbm, максимум по насыщенности в center.
    // Важно: это один прямоугольник с трехточечным градиентом, без видимых границ «полос».
    addRectWithVerticalGradient(centerDbm + kPowerGraphGreenHalfWidthDbm,
                                centerDbm - kPowerGraphGreenHalfWidthDbm,
                                {
                                    // На границах нельзя уходить в "почти ноль",
                                    // иначе при стыковке с красным получится визуальная "пустота".
                                    {0.0, withAlpha(greenBase, 55)},  // верх (минимум)
                                    {0.5, withAlpha(greenBase, 110)}, // центр (максимум)
                                    {1.0, withAlpha(greenBase, 55)}   // низ (минимум)
                                });

    // Красный верх: сразу за зелёной зоной, толщиной kPowerGraphRedBandThicknessDbm
    addRectWithVerticalGradient(centerDbm + kPowerGraphGreenHalfWidthDbm + kPowerGraphRedBandThicknessDbm,
                                centerDbm + kPowerGraphGreenHalfWidthDbm,
                                {
                                    {0.0, redStrong}, // верх
                                    {1.0, redMuted}   // граница с зелёным
                                });

    // Красный низ: сразу за зелёной зоной, толщиной kPowerGraphRedBandThicknessDbm
    addRectWithVerticalGradient(centerDbm - kPowerGraphGreenHalfWidthDbm,
                                centerDbm - kPowerGraphGreenHalfWidthDbm - kPowerGraphRedBandThicknessDbm,
                                {
                                    {0.0, redMuted},  // граница с зелёным
                                    {1.0, redStrong}  // низ
                                });
}

void MainWindow::updatePowerGraphHelperRectsXSpan()
{
    if (!ui || !ui->plotWidgetPowerGraph || m_powerGraphHelperRects.isEmpty()) {
        return;
    }
    const QCPRange xr = ui->plotWidgetPowerGraph->xAxis->range();
    for (QCPItemRect *r : m_powerGraphHelperRects) {
        if (!r) {
            continue;
        }
        const double yTop = r->topLeft->coords().y();
        const double yBottom = r->bottomRight->coords().y();
        r->topLeft->setCoords(xr.lower, yTop);
        r->bottomRight->setCoords(xr.upper, yBottom);
    }
}

void MainWindow::updatePowerGraphScatterLayers()
{
    if (!m_powerGraphTrace || !m_powerGraphScatterOk || !m_powerGraphScatterBad) {
        return;
    }

    m_powerGraphTrace->setData(m_powerGraphFreqsMHz, m_powerGraphAmpsDbm);

    QVector<double> okX;
    QVector<double> okY;
    QVector<double> badX;
    QVector<double> badY;
    const int n = qMin(m_powerGraphFreqsMHz.size(), m_powerGraphAmpsDbm.size());
    okX.reserve(n);
    okY.reserve(n);
    badX.reserve(n);
    badY.reserve(n);
    for (int i = 0; i < n; ++i) {
        const double y = m_powerGraphAmpsDbm.at(i);
        if (powerAmpInsideGreenBand(y, m_powerGraphAutoYCenterDbm)) {
            okX.push_back(m_powerGraphFreqsMHz.at(i));
            okY.push_back(y);
        } else {
            badX.push_back(m_powerGraphFreqsMHz.at(i));
            badY.push_back(y);
        }
    }
    m_powerGraphScatterOk->setData(okX, okY);
    m_powerGraphScatterBad->setData(badX, badY);
}

void MainWindow::onPowerGraphPlotMouseMove(QMouseEvent *event)
{
    if (!event || !ui || !ui->plotWidgetPowerGraph || !m_powerGraphHoverLabel || !m_powerGraphTrace) {
        return;
    }

    const int n = qMin(m_powerGraphFreqsMHz.size(), m_powerGraphAmpsDbm.size());
    if (n <= 0) {
        hidePowerGraphHoverLabel();
        return;
    }

    double bestDist2 = std::numeric_limits<double>::infinity();
    int idx = -1;
    for (int i = 0; i < n; ++i) {
        const double px = ui->plotWidgetPowerGraph->xAxis->coordToPixel(m_powerGraphFreqsMHz.at(i));
        const double py = ui->plotWidgetPowerGraph->yAxis->coordToPixel(m_powerGraphAmpsDbm.at(i));
        const double dx = px - event->pos().x();
        const double dy = py - event->pos().y();
        const double d2 = dx * dx + dy * dy;
        if (d2 < bestDist2) {
            bestDist2 = d2;
            idx = i;
        }
    }

    constexpr double kHitRadiusPx = 14.0;
    if (idx < 0 || bestDist2 > kHitRadiusPx * kHitRadiusPx) {
        hidePowerGraphHoverLabel();
        return;
    }

    const quint64 targetHz =
        (idx >= 0 && idx < m_powerGraphTargetFreqsHz.size()) ? m_powerGraphTargetFreqsHz.at(idx) : 0ULL;
    const quint64 actualHz = static_cast<quint64>(std::llround(m_powerGraphFreqsMHz.at(idx) * 1e6));
    const double pDbm = m_powerGraphAmpsDbm.at(idx);

    const QString targetStr = targetHz ? formatHzTriplet(targetHz) : QStringLiteral("—");
    const QString actualStr = formatHzTriplet(actualHz);

    m_powerGraphHoverLabel->setText(QStringLiteral("%1\n%2\n%3 dBm")
                                        .arg(targetStr)
                                        .arg(actualStr)
                                        .arg(pDbm, 0, 'f', 2));

    const double fMHz = m_powerGraphFreqsMHz.at(idx);

    const double px = ui->plotWidgetPowerGraph->xAxis->coordToPixel(fMHz);
    const double py = ui->plotWidgetPowerGraph->yAxis->coordToPixel(pDbm);
    m_powerGraphHoverLabel->position->setCoords(px + 14.0, py - 12.0);

    if (!m_powerGraphHoverLabel->visible()) {
        m_powerGraphHoverLabel->setVisible(true);
    }
    ui->plotWidgetPowerGraph->replot(QCustomPlot::rpQueuedReplot);
}

void MainWindow::on_actionSettings_triggered()
{
    // Требование: при каждом входе в меню настроек интерфейсы должны
    // заново искаться. Поэтому не передаем initialIfaces и не используем кэш.
    const QStringList freshIfaces = collectEligibleInterfaces();
    const QString preselectedIface = (freshIfaces.size() == 1) ? freshIfaces.value(0) : QString();

    const bool alreadyConnected = (m_deviceController && m_deviceController->isConnected());
    SettingsDialog dialog(this, QStringList(), preselectedIface, QVector<QString>(), alreadyConnected);
    connect(&dialog, &SettingsDialog::stationConnectRequested,
            this, &MainWindow::onStationConnectRequested);
    connect(&dialog, &SettingsDialog::stationScanCompleted, this,
            [this](const QString &iface, const QVector<QString> &rawIps, int stationCount) {
                Q_UNUSED(stationCount);
                m_cachedFoundIpsByIface.insert(iface, rawIps);
                const QMap<int, QString> chosen = chosenStationsBySubnetFromFoundIps(rawIps);
                const int n = chosen.size();
                if (n == 0) {
                    onDeviceLogMessage(QString("Радиостанции на %1 не найдены. Откройте настройки и выберите радиостанцию/интерфейс.")
                                           .arg(iface));
                } else {
                    QStringList stationIpList;
                    stationIpList.reserve(n);
                    for (auto it = chosen.cbegin(); it != chosen.cend(); ++it) {
                        stationIpList.append(it.value());
                    }
                    onDeviceLogMessage(QStringLiteral("Найдено %1 %2: %3")
                                           .arg(n)
                                           .arg(ruStationWord(n))
                                           .arg(stationIpList.join(QStringLiteral(", "))));
                }
            });
    connect(&dialog, &SettingsDialog::stationAutoConnecting, this,
            [this](const QString &stationIp, const QString &iface) {
                onDeviceLogMessage(QString("Автоподключение к радиостанции %1 (интерфейс %2)...").arg(stationIp, iface));
            });
    dialog.exec();
}

void MainWindow::startAutoDiscovery()
{
    QPointer<MainWindow> self(this);
    QtConcurrent::run([self]() {
        const QStringList ifaces = self ? self->collectEligibleInterfaces() : QStringList();
        QMetaObject::invokeMethod(qApp, [self, ifaces]() {
            if (!self) {
                return;
            }
            self->handleDiscoveryFinished(ifaces);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::attemptStationConnectAfterEmergencyUpdate()
{
    if (!m_updateBkuWidget || !m_updateBkuWidget->isAwaitingBootcmdReset()) {
        return;
    }

    if (m_deviceController) {
        m_deviceController->setInactivityWatchdogEnabled(true);
    }

    if (m_deviceController && m_deviceController->isConnected()) {
        if (m_updateBkuWidget) {
            m_updateBkuWidget->notifyStationReachableForPostUpdate();
        }
        return;
    }

    const QString stationIp = m_deviceController ? m_deviceController->config().stationIp.trimmed() : QString();
    attemptStationConnectAfterBkuReboot(stationIp);
}

void MainWindow::cancelBkuKernelBootWait()
{
    m_bkuKernelBootWaitTimer.stop();
    m_bkuKernelBootWaitProgressTimer.stop();
    m_bkuKernelBootWaitStationIp.clear();
    m_bkuKernelBootWaitEmergency = false;
}

void MainWindow::startBkuPostKernelBootWait(const QString &stationIp, bool emergency)
{
    const QString ip = stationIp.trimmed();
    if (!m_bkuUpdateMode || ip.isEmpty()) {
        return;
    }

    cancelBkuKernelBootWait();

    m_bkuKernelBootWaitEmergency = emergency;
    m_bkuKernelBootWaitStationIp = ip;

    if (debug) {
        onDeviceLogMessage(QStringLiteral("Ожидание загрузки ядра радиостанции (%1 с)...")
                               .arg(POST_REBOOT_STATION_DOWN_WAIT_MS / 1000));
    }

    if (m_deviceController && m_deviceController->isConnected()) {
        m_deviceController->disconnectFromDevice();
    }

    if (ui && ui->progressBar) {
        showStationHeaderCenter(StationHeaderCenter::ProgressBar);
        applyStationHeaderProgressBarLayout(true);
        ui->progressBar->setTextVisible(true);
        ui->progressBar->setFormat(QStringLiteral("%p%"));
        ui->progressBar->setRange(0, 100);
        ui->progressBar->setValue(0);
    }
    m_bkuKernelBootWaitElapsed.restart();
    m_bkuKernelBootWaitProgressTimer.start();
    m_bkuKernelBootWaitTimer.start(POST_REBOOT_STATION_DOWN_WAIT_MS);
}

void MainWindow::onBkuPostKernelBootWaitTimeout()
{
    const QString ip = m_bkuKernelBootWaitStationIp.trimmed();
    const bool emergency = m_bkuKernelBootWaitEmergency;
    cancelBkuKernelBootWait();

    if (!m_bkuUpdateMode || ip.isEmpty()) {
        return;
    }

    m_bkuKernelBootWaitProgressTimer.stop();
    if (ui && ui->progressBar) {
        showStationHeaderCenter(StationHeaderCenter::ProgressBar);
        applyStationHeaderProgressBarLayout(true);
        ui->progressBar->setTextVisible(false);
        ui->progressBar->setRange(0, 0);
        ui->progressBar->setValue(0);
    }

    if (m_updateBkuWidget) {
        m_updateBkuWidget->startPostKernelBootSshCheck(ip);
    }

    if (emergency) {
        attemptStationConnectAfterEmergencyUpdate();
        m_emergencyConnectRetryTimer.setInterval(30000);
        if (m_emergencyConnectRetryTimer.parent() == nullptr) {
            m_emergencyConnectRetryTimer.setParent(this);
            connect(&m_emergencyConnectRetryTimer, &QTimer::timeout, this, [this]() {
                if (m_updateBkuWidget && m_updateBkuWidget->isAwaitingBootcmdReset()) {
                    attemptStationConnectAfterEmergencyUpdate();
                } else {
                    m_emergencyConnectRetryTimer.stop();
                }
            });
        }
        m_emergencyConnectRetryTimer.start();
        return;
    }

    attemptStationConnectAfterBkuReboot(ip);
}

void MainWindow::onBkuPostKernelBootWaitProgressTick()
{
    if (m_bkuKernelBootWaitStationIp.trimmed().isEmpty()) {
        m_bkuKernelBootWaitProgressTimer.stop();
        return;
    }
    if (!ui || !ui->progressBar) {
        m_bkuKernelBootWaitProgressTimer.stop();
        return;
    }

    const qint64 elapsed = m_bkuKernelBootWaitElapsed.isValid() ? m_bkuKernelBootWaitElapsed.elapsed() : 0;
    const int percent =
        qBound(0, static_cast<int>((elapsed * 100) / POST_REBOOT_STATION_DOWN_WAIT_MS), 100);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(percent);
}

void MainWindow::attemptStationConnectAfterBkuReboot(const QString &stationIp)
{
    if (!m_bkuUpdateMode) {
        return;
    }

    const QString ip = stationIp.trimmed();
    const QString iface = connectedInterfaceName();
    if (!ip.isEmpty() && !iface.isEmpty()) {
        if (m_deviceController && m_deviceController->isConnected()) {
            const QString currentIp = m_deviceController->config().stationIp.trimmed();
            if (currentIp == ip) {
                if (debug) {
                    onDeviceLogMessage(
                        QStringLiteral("Радиостанция %1: связь UDP установлена, ожидание готовности по SSH...")
                            .arg(ip));
                }
                if (m_updateBkuWidget) {
                    m_updateBkuWidget->notifyStationReachableForPostUpdate();
                }
                return;
            }
            m_deviceController->disconnectFromDevice();
        }
        if (debug) {
            onDeviceLogMessage(QStringLiteral("Подключение к радиостанции после перезагрузки (%1)...").arg(ip));
        }
        onStationConnectRequested(ip, iface);
        return;
    }

    if (debug) {
        onDeviceLogMessage(QStringLiteral("Поиск радиостанции после перезагрузки..."));
    }
    startAutoDiscovery();
}

QStringList MainWindow::collectEligibleInterfaces() const
{
    QStringList result;

    // Аналогично SettingsDialog: исключаем отключенные устройства.
    QSet<QString> nmcliAllowedDevices;
    {
        const QPair<bool, QString> nmcliResult =
            executeCommand("nmcli -t -f DEVICE,STATE device status 2>/dev/null");
        if (nmcliResult.first) {
            const QStringList lines = nmcliResult.second.split('\n', Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                const QString trimmed = line.trimmed();
                const QStringList parts = trimmed.split(':');
                if (parts.size() < 2) {
                    continue;
                }
                const QString deviceName = parts.value(0).trimmed();
                if (deviceName.isEmpty()) {
                    continue;
                }
                const QString state = parts.mid(1).join(':').trimmed();
                const QString s = state.toLower();
                const bool blocked =
                    s.contains("disconnected") ||
                    s.contains("unavailable") ||
                    s.contains("unmanaged");
                if (!blocked) {
                    nmcliAllowedDevices.insert(deviceName);
                }
            }
        }
    }

    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        if (!(interface.flags() & QNetworkInterface::IsUp) ||
            !(interface.flags() & QNetworkInterface::IsRunning) ||
            (interface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        if (interface.hardwareAddress().isEmpty()) {
            continue;
        }

        const QString name = interface.name();
        if (name.startsWith("eth") || name.startsWith("en") /*|| name.startsWith("wlan")*/) {
            if (!nmcliAllowedDevices.isEmpty() && !nmcliAllowedDevices.contains(name)) {
                continue;
            }
            result.push_back(name);
        }
    }
    return result;
}

void MainWindow::handleDiscoveryFinished(const QStringList &ifaces)
{
    if (ifaces.isEmpty()) {
        onDeviceLogMessage("Ethernet-интерфейсы не найдены. Откройте настройки и выберите интерфейс вручную.");
        return;
    }

    const int ifaceCount = ifaces.size();
    onDeviceLogMessage(QStringLiteral("Найдено %1 %2: %3")
                           .arg(ifaceCount)
                           .arg(ruEthernetIfaceWord(ifaceCount))
                           .arg(ifaces.join(QStringLiteral(", "))));

    // Если интерфейс один — сразу ищем станции на нём.
    if (ifaces.size() == 1) {
        const QString iface = ifaces.value(0);
        onDeviceLogMessage(QString("Поиск радиостанций на интерфейсе %1...").arg(iface));

        QPointer<MainWindow> self(this);
        QtConcurrent::run([self, iface]() {
            const QVector<QString> foundIps =
                self && self->m_finder ? self->m_finder->searchStations(iface) : QVector<QString>();
            QMetaObject::invokeMethod(qApp, [self, iface, foundIps]() {
                if (!self) {
                    return;
                }
                self->handleStationsFound(iface, foundIps);
            }, Qt::QueuedConnection);
        });
        return;
    }

    // Интерфейсов несколько — дальнейший выбор/поиск делаем через настройки.
    onDeviceLogMessage("Интерфейсов несколько. Откройте настройки и выберите интерфейс для поиска радиостанции.");
}

void MainWindow::handleStationsFound(const QString &iface, const QVector<QString> &foundIps)
{
    m_cachedFoundIpsByIface.insert(iface, foundIps);

    const QMap<int, QString> chosenBySubnet = chosenStationsBySubnetFromFoundIps(foundIps);

    const int stationCount = chosenBySubnet.size();

    if (stationCount == 0) {
        onDeviceLogMessage(QString("Радиостанции на %1 не найдены. Откройте настройки и выберите радиостанцию/интерфейс.").arg(iface));
        return;
    }

    QStringList stationIpList;
    stationIpList.reserve(stationCount);
    for (auto it = chosenBySubnet.cbegin(); it != chosenBySubnet.cend(); ++it) {
        stationIpList.append(it.value());
    }
    onDeviceLogMessage(QStringLiteral("Найдено %1 %2: %3")
                           .arg(stationCount)
                           .arg(ruStationWord(stationCount))
                           .arg(stationIpList.join(QStringLiteral(", "))));

    // Если по итоговой логике выбора станция ровно одна — подключаемся автоматически.
    if (stationCount == 1) {
        const QString stationIp = chosenBySubnet.cbegin().value();
                onDeviceLogMessage(QString("Автоподключение к радиостанции %1 (интерфейс %2)...").arg(stationIp, iface));
        onStationConnectRequested(stationIp, iface);
        return;
    }

    // Станций несколько — пользователь выберет в настройках.
    onDeviceLogMessage("Радиостанций найдено несколько. Откройте настройки и выберите радиостанцию для подключения.");
}

bool MainWindow::ensureStationIpsConfigured(const QString &interfaceName,
                                            const QString &stationIp,
                                            QString *chosenSelfIp,
                                            QString *errorText) const
{
    const QString activeNetwork = interfaceName.trimmed();
    if (activeNetwork.isEmpty()) {
        if (errorText) *errorText = "Сетевой интерфейс не выбран.";
        return false;
    }

    const QStringList ipParts = stationIp.trimmed().split('.');
    if (ipParts.size() != 4) {
        if (errorText) *errorText = QString("Некорректный IP радиостанции: %1").arg(stationIp);
        return false;
    }
    const QString staNum = ipParts[2];
    const QString linearSubnet = ipParts[3];

    QString command = QString("nmcli -t -f UUID,DEVICE connection show --active | grep -F \":%1\" | cut -d':' -f1")
                          .arg(activeNetwork);
    QPair<bool, QString> result = executeCommand(command);
    if (!result.first || result.second.trimmed().isEmpty()) {
        if (errorText) {
            *errorText = QString("Ошибка: активное сетевое соединение для интерфейса %1 не найдено.")
                             .arg(activeNetwork);
        }
        return false;
    }

    const QString connectionUuid = result.second.trimmed().split('\n', Qt::SkipEmptyParts).value(0).trimmed();
    if (connectionUuid.isEmpty()) {
        if (errorText) *errorText = "Ошибка: UUID активного сетевого соединения пустой.";
        return false;
    }

    command = QString("nmcli -g ipv4.addresses connection show uuid \"%1\"").arg(connectionUuid);
    result = executeCommand(command);
    const QStringList ipList = result.second.split(QRegularExpression("[,/\\s]+"), Qt::SkipEmptyParts);

    const int startRange = (linearSubnet == "193") ? 194 : 2;
    const int endRange = (linearSubnet == "193") ? 255 : 127;

    QSet<QString> usedSubnetIps;
    for (int yCheck = startRange; yCheck <= endRange; ++yCheck) {
        const QString testIP = QString("192.168.%1.%2").arg(staNum).arg(yCheck);
        if (ipList.contains(testIP)) {
            usedSubnetIps.insert(testIP);
        }
    }

    QString addIP;
    const int maxAttempts = 128;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        const int randomHost = (linearSubnet == "193")
                                   ? QRandomGenerator::global()->bounded(194, 255)
                                   : QRandomGenerator::global()->bounded(2, 127);
        const QString candidate = QString("192.168.%1.%2").arg(staNum).arg(randomHost);
        if (!usedSubnetIps.contains(candidate)) {
            addIP = candidate;
            break;
        }
    }

    if (addIP.isEmpty()) {
        if (errorText) {
            *errorText = QString("Не удалось подобрать свободный IP для подсети 192.168.%1.*").arg(staNum);
        }
        return false;
    }

    const int cidr = (linearSubnet == "193") ? 26 : 25;
    command = QString("nmcli connection modify uuid \"%1\" ipv4.method manual +ipv4.addresses %2/%3")
                  .arg(connectionUuid).arg(addIP).arg(cidr);

    result = executeCommand(command);
    if (!result.first) {
        result = executeCommand(QString("sudo %1").arg(command));
    }
    if (!result.first) {
        if (errorText) *errorText = QString("Ошибка при добавлении IP-адреса %1/%2 для подключения UUID \"%3\": %4")
                                        .arg(addIP).arg(cidr).arg(connectionUuid, result.second.trimmed());
        return false;
    }

    // Переподнимаем интерфейс, чтобы адрес применился.
    command = QString("nmcli device disconnect \"%1\"").arg(activeNetwork);
    result = executeCommand(command);
    if (!result.first) {
        result = executeCommand(QString("sudo %1").arg(command));
    }
    if (!result.first) {
        if (errorText) *errorText = QString("Ошибка при перезагрузке сетевого интерфейса %1 (disconnect): %2")
                                        .arg(activeNetwork, result.second.trimmed());
        return false;
    }

    command = QString("nmcli device connect \"%1\"").arg(activeNetwork);
    result = executeCommand(command);
    if (!result.first) {
        result = executeCommand(QString("sudo %1").arg(command));
    }
    if (!result.first) {
        if (errorText) *errorText = QString("Ошибка при перезагрузке сетевого интерфейса %1 (connect): %2")
                                        .arg(activeNetwork, result.second.trimmed());
        return false;
    }

    if (chosenSelfIp) {
        *chosenSelfIp = addIP;
    }

    // После nmcli connect адрес может появиться на интерфейсе с задержкой.
    for (int attempt = 0; attempt < 25; ++attempt) {
        command = QString("ip -4 -o addr show dev %1").arg(activeNetwork);
        result = executeCommand(command);
        if (result.first && result.second.contains(addIP + QLatin1Char('/'))) {
            break;
        }
        QThread::msleep(200);
    }

    return true;
}

void MainWindow::onStationConnectRequested(const QString &stationIp, const QString &interfaceName) {
    const QString ip = stationIp.trimmed();
    const QString iface = interfaceName.trimmed();
    if (ip.isEmpty() || iface.isEmpty()) {
        onDeviceLogMessage("Подключение не выполнено: не выбран IP радиостанции или интерфейс.");
        return;
    }

    // ВАЖНО: сетевую подготовку (nmcli + выбор selfIp) делаем асинхронно,
    // чтобы UI не блокировался и окно настроек могло скрыться сразу после выбора.
    onDeviceLogMessage(QString("Подготовка сетевого подключения (интерфейс %1) к радиостанции %2...").arg(iface, ip));

    QPointer<MainWindow> self(this);
    QtConcurrent::run([self, ip, iface]() {
        QString selfIp;
        QString err;
        const bool ok = self && self->ensureStationIpsConfigured(iface, ip, &selfIp, &err);
        QMetaObject::invokeMethod(qApp, [self, ok, err, ip, iface, selfIp]() {
            if (!self) {
                return;
            }
            if (!ok) {
                self->onDeviceLogMessage(QString("Подключение не выполнено: %1").arg(err));
                return;
            }

            // После подготовки — подключаемся в UI-потоке.
            if (self->m_deviceController && self->m_deviceController->isConnected()) {
                self->m_deviceController->disconnectFromDevice();
            }

            self->setStartTestingButtonEnabled(false);
            self->ui->frameStation->setVisible(true);

            if (self->m_deviceController) {
                if (!selfIp.trimmed().isEmpty()) {
                    self->m_deviceController->setSelfIp(selfIp.trimmed());
                    const QString localIpMsg =
                        formatLocalIpForStationConnectionLogMessage(selfIp.trimmed());
                    if (!localIpMsg.isEmpty()) {
                        self->onDeviceLogMessage(localIpMsg);
                    } else if (debug) {
                        self->onDeviceLogMessage(QString("Выбран self IP контроллера: %1").arg(selfIp.trimmed()));
                    }
                }
                self->m_deviceController->setStationIp(ip);
            }

            if (self->m_updateBkuWidget && self->m_updateBkuWidget->isAwaitingBootcmdReset()) {
                self->m_updateBkuWidget->setStationContext(ip, iface);
                self->m_updateBkuWidget->notifyStationReachableForPostUpdate();
            }

            if (debug) {
                self->onDeviceLogMessage(QString("Запрос подключения к радиостанции %1").arg(ip));
            }

            // Запоминаем для очистки при выходе (может быть несколько станций/несколько добавлений).
            const QStringList parts = ip.split('.');
            const int cidr = (parts.size() == 4 && parts[3] == "193") ? 26 : 25;
            AddedIpEntry entry;
            entry.ip = selfIp.trimmed();
            entry.cidr = cidr;
            entry.iface = iface;
            if (!entry.iface.isEmpty()) {
                const QString cmd = QString("nmcli -t -f UUID,DEVICE connection show --active | grep -F \":%1\" | cut -d':' -f1")
                                        .arg(entry.iface);
                const QPair<bool, QString> res = self->executeCommand(cmd);
                entry.connectionUuid = res.second.trimmed().split('\n', Qt::SkipEmptyParts).value(0).trimmed();
            }

            const bool exists = std::any_of(self->m_addedIps.cbegin(), self->m_addedIps.cend(), [&entry](const AddedIpEntry &e) {
                return e.iface == entry.iface && e.ip == entry.ip && e.cidr == entry.cidr;
            });
            if (!exists && !entry.ip.isEmpty() && entry.cidr > 0 && !entry.iface.isEmpty()) {
                self->m_addedIps.push_back(entry);
            }

            if (self->m_deviceController) {
                self->m_deviceController->connectToDevice();
            }
        }, Qt::QueuedConnection);
    });
}

void MainWindow::onDeviceConnected(const QString &ip) {
    const bool wasInDisconnectRecovery = m_stationDisconnectRecoveryActive;
    m_stationDisconnectRecoveryActive = false;

    setStationConnectedUi();
    ui->frameStation->setVisible(true);
    const QString ipTrimmed = ip.trimmed();
    applyStationHeaderFromIp(ipTrimmed);
    if (debug) {
        onDeviceLogMessage(QString("Успешное подключение к радиостанции: %1").arg(ip));
    }

    if (m_bkuUpdateMode) {
        if (m_updateBkuWidget) {
            m_updateBkuWidget->setStationContext(ipTrimmed, connectedInterfaceName());
            if (m_updateBkuWidget->isUpdateInProgress()) {
                if (debug) {
                    onDeviceLogMessage(
                        QStringLiteral("Радиостанция %1: связь восстановлена, проверка готовности БКУ...")
                            .arg(ipTrimmed));
                }
                m_updateBkuWidget->setStationLinkActive(true);
                m_updateBkuWidget->notifyStationReachableForPostUpdate();
            } else {
                m_updateBkuWidget->setStationLinkActive(true);
            }
        }
        onDeviceLogMessage(QStringLiteral("Радиостанция %1: связь установлена.").arg(ipTrimmed));
        return;
    }

    handleNormalStationConnected(ipTrimmed, wasInDisconnectRecovery);
}

void MainWindow::applyStationHeaderFromIp(const QString &ipTrimmed)
{
    if (ipTrimmed.isEmpty()) {
        return;
    }
    if (m_stationLabelIp != ipTrimmed) {
        m_stationHardwareVariant.clear();
        m_stationLabelFixedText.clear();
        m_stationLabelIp = ipTrimmed;
    }
    bool stationNumOk = false;
    const int stationNum = stationNumFromIp(ipTrimmed, &stationNumOk);
    if (stationNumOk) {
        m_stationLabelNumber = stationNum;
    } else {
        m_stationLabelNumber = -1;
    }
    updateStationLabelText();
}

void MainWindow::handleNormalStationConnected(const QString &ipTrimmed, bool wasInDisconnectRecovery)
{
    onDeviceLogMessage(QStringLiteral("Радиостанция %1: связь установлена.").arg(ipTrimmed));

    // По ТЗ: сразу после подключения получаем TraktParam.xml по SSH и формируем profile_active_TEST.tar.gz.
    // На пустой станции дополнительно готовим полный комплект (реестр профилей) — загрузка по кнопке.
    prepareTestProfileAfterConnect(ipTrimmed);

    // Контроль целостности профиля: если это переподключение после reboot, запускаем проверку.
    if (m_profileIntegrityStage == ProfileIntegrityStage::Reconnecting &&
        !m_profileIntegrityStationIp.trimmed().isEmpty() &&
        ipTrimmed == m_profileIntegrityStationIp.trimmed()) {
        m_postRebootReconnectTimer.stop();
        m_postRebootWaitTimer.stop();
        m_postRebootWaitProgressTimer.stop();
        // После переподключения framePPM скрыт до конца сценария; прогресс — оценка «Ожидание штатной загрузки
        // трактов» (N×8 с), затем по 100% — неопределённый режим до ворот; после startPpmInit — снова busy.
        if (ui) {
            showStationHeaderCenter(StationHeaderCenter::ProgressBar);
        }
        beginPostReconnectStationBootWaitAfterProfileConnect();

        m_profileIntegrityStage = ProfileIntegrityStage::Checking;
        onDeviceLogMessage(QStringLiteral("Успешное подключение радиостанции после перезагрузки"));
        if (debug) {
            onDeviceLogMessage(
                QStringLiteral("Радиостанция снова подключена. Контроль целостности профиля: архивирование и сравнение md5..."));
        }

        const QString stationIp = m_profileIntegrityStationIp.trimmed();
        QPointer<MainWindow> self(this);
        QtConcurrent::run([self, stationIp]() {
            QString err;
            const bool ok = self && self->verifyProfileIntegrityAfterRebootOverSsh(stationIp, &err);
            QMetaObject::invokeMethod(qApp, [self, ok, err]() {
                if (!self) {
                    return;
                }
                if (ok) {
                    self->onDeviceLogMessage(QStringLiteral("Контроль целостности: ОК"));
                    self->onDeviceLogMessage(QStringLiteral("Ожидание штатной загрузки трактов..."));
                    self->m_postReconnectStationBootSshOk = true;
                    self->tryStartPpmInitAfterPostReconnectBootGates();
                } else {
                    self->onDeviceLogMessage(QString("ОШИБКА контроля целостности профиля: %1")
                                           .arg(err.isEmpty() ? QStringLiteral("неизвестная ошибка") : err));
                    self->cancelPostReconnectStationBootWait(true);
                    // Если контроль целостности не прошёл — не трогаем тракты и возвращаем UI в обычное состояние.
                    if (self->ui && self->ui->progressBar) {
                        self->ui->progressBar->setTextVisible(false);
                        self->ui->progressBar->setRange(0, 100);
                        self->ui->progressBar->setValue(0);
                        self->showStationHeaderCenter(StationHeaderCenter::StartButton);
                    }
                }
                if (!self->m_postReconnectStationBootWaitActive) {
                    self->m_profileIntegrityStage = ProfileIntegrityStage::None;
                    self->m_profileIntegrityStationIp.clear();
                }
            }, Qt::QueuedConnection);
        });
    }

    if (wasInDisconnectRecovery && m_profileIntegrityStage == ProfileIntegrityStage::None) {
        onDeviceLogMessage(QStringLiteral("Связь с радиостанцией восстановлена."));
        if (isActivePpmTestingSession()) {
            if (ui && m_ppmPowerStage == PpmPowerSequenceStage::None) {
                showStationHeaderCenter(StationHeaderCenter::FramePpm);
            }
            const bool canInteract = m_deviceController && m_deviceController->isConnected()
                                     && !m_deviceController->isAwaitingTractPowerAck()
                                     && (m_ppmPowerStage == PpmPowerSequenceStage::None);
            setAllPpmRadiosEnabled(canInteract);
            m_externalSwitchProtectionArmed = true;
        }
        requestRecoveryIndicationsAfterReconnect();

        // Если станция после reconnect не прислала новую IND_ERROR (статус не менялся),
        // восстанавливаем отображение из последнего известного кэша статусов.
        int statusTract = selectedPpmTractFromUi();
        if (statusTract <= 0) {
            statusTract = (m_ppmCurrentOnTract > 0) ? m_ppmCurrentOnTract : ppmFirstTractNumber();
        }
        if (statusTract > 0 && m_ppmLastStatusCodeByTract.contains(statusTract)) {
            refreshPpmStatusUiForTract(statusTract);
        }

        // После восстановления связи со станцией power-тест должен продолжаться автоматически.
        const int powerTr = (m_powerTestTargetTract != 0U) ? static_cast<int>(m_powerTestTargetTract)
                                                           : selectedPpmTractFromUi();
        if (m_powerTestPaused && m_powerTestBlockedByStationDisconnect && powerTr > 0) {
            m_powerTestBlockedByStationDisconnect = false;
            m_powerTestBlockedByPpm = false;
            attemptScheduleDelayedPowerTestResume(powerTr);
        }

        if (m_receiveTestRunning && m_receiveTestPaused && m_receiveTestAutoPausedByPpmNotReady
            && m_receiveTestTract > 0) {
            attemptScheduleDelayedReceiveTestResume(m_receiveTestTract);
        }
    }

    updatePowerTestButtonsAccessForSelectedTract();
    updateReceiveTestButtonsAccessForSelectedTract();
    updateFhssTestButtonsAccessForSelectedTract();
    updateTabWidgetLockState();
}

void MainWindow::onDeviceDisconnected() {
    if (m_bkuUpdateMode) {
        if (ui && ui->frameStation) {
            ui->frameStation->setVisible(true);
            ui->frameStation->setStyleSheet(styleSheetDisconnectStation);
        }
        if (ui && ui->labelPixStation) {
            ui->labelPixStation->setPixmap(QPixmap(QStringLiteral(":/led_red.png")));
        }
        if (ui && ui->labelStateStation) {
            ui->labelStateStation->setText(QStringLiteral("Отключена"));
            ui->labelStateStation->setStyleSheet(QStringLiteral("color: #ff5252;"));
        }
        setStatusLedGlowColor(m_stationLedGlowEffect, QStringLiteral("#ef4444"));
        appendDeviceLogLine(QStringLiteral("Потеряна связь с радиостанцией"),
                            QColor(QStringLiteral("#f87171")));
        if (ui->actionBkuUpdate) {
            ui->actionBkuUpdate->setEnabled(true);
        }
        if (m_updateBkuWidget) {
            m_updateBkuWidget->setStationLinkActive(false);
        }
        return;
    }

    clearAllSelfIssuedGuards();
    m_externalSwitchProtectionArmed = false;
    m_stationNeedsProfileRegistrySeed = false;
    m_stationDisconnectRecoveryActive = true;
    ++m_receiveResumeAfterReconnectSerial;

    if (m_postReconnectStationBootWaitActive) {
        cancelPostReconnectStationBootWait(true);
        if (m_profileIntegrityStage != ProfileIntegrityStage::None) {
            m_profileIntegrityStage = ProfileIntegrityStage::None;
            m_profileIntegrityStationIp.clear();
        }
    }

    if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
        m_powerTrafficGenerator->stop();
    }

    const bool powerTestHasStateToPauseOrResume =
        (ui && ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked())
        || m_powerTestPaused
        || (m_powerTestSequenceIndex >= 0 && !m_powerTestSequenceFreqsHz.isEmpty());
    if (powerTestHasStateToPauseOrResume) {
        pausePowerTestForStationDisconnect();
        m_powerTestBlockedByPpm = true;
    } else {
        m_powerTestBlockedByStationDisconnect = false;
        m_powerTestAutoStopTimer.stop();
        m_powerTestStepPauseTimer.stop();
        m_powerTestBeforePowerOnTimer.stop();
        m_powerMeasurementRunning = false;
        m_powerTrafficStartPending = false;
        m_powerStepAmpAccumDbm = 0.0;
        m_powerStepAmpSampleCount = 0;
        m_powerStepBestValid = false;
        m_powerStepBestFreqMHz = 0.0;
        m_powerStepBestAmpDbm = -200.0;
        m_powerTestCurrentFreqHz = 0;
        if (ui && ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked()) {
            QSignalBlocker blocker(ui->pushButtonStartTestingPower);
            ui->pushButtonStartTestingPower->setChecked(false);
        }
        if (ui && ui->pushButtonStartTestingPower) {
            ui->pushButtonStartTestingPower->setText(QStringLiteral("НАЧАТЬ ТЕСТ МОЩНОСТИ"));
        }
    }

    if (ui && ui->plotWidgetMomentSpetrumGraph) {
        ui->plotWidgetMomentSpetrumGraph->xAxis->setLabel(QString());
        ui->plotWidgetMomentSpetrumGraph->replot(QCustomPlot::rpQueuedReplot);
    }
    setEmissionAnimating(false);

    // ППРЧ-тест: при обрыве станции переводим в внешнюю паузу, как при «Нет связи с ПП».
    if (m_fhssRunning || m_fhssDirSwitchPending || m_fhssBlockedByPpm || m_fhssBlockedByAntFault
        || m_fhssBlockedByDirRestore || m_fhssReturnToDefaultDirPending
        || (ui && ui->pushButtonFHSSTestStop && ui->pushButtonFHSSTestStop->isVisible())) {
        if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
            DEBUG << QStringLiteral("⏸ ППРЧ: потеря связи с радиостанцией — остановка RTP/генератора трафика.");
            m_powerTrafficGenerator->stop();
        }
        if (ui && ui->emissionAntennaWidgetFHSS) {
            ui->emissionAntennaWidgetFHSS->stopTransmission();
        }
        ++m_fhssResumeAfterPpmSerial;
        m_fhssBlockedByPpm = true;
        if (ui && ui->pushButtonStartTestingFHSS) {
            ui->pushButtonStartTestingFHSS->setVisible(false);
        }
        if (ui && ui->pushButtonFHSSTestStop) {
            ui->pushButtonFHSSTestStop->setVisible(true);
            ui->pushButtonFHSSTestStop->setEnabled(false);
        }
    }

    pauseReceiveTestForStationDisconnect();

    setStationDisconnectedUi();
    ui->frameStation->setVisible(true);
    appendDeviceLogLine(QStringLiteral("Потеряна связь с радиостанцией"),
                        QColor(QStringLiteral("#f87171")));

    // Кнопка «НАЧАТЬ ТЕСТИРОВАНИЕ» нужна только до входа в режим тестирования (framePPM).
    if (!isActivePpmTestingSession()) {
        setStartTestingButtonEnabled(false);
    }
    updatePowerTestButtonsAccessForSelectedTract();
    updateReceiveTestButtonsAccessForSelectedTract();
    updateFhssTestButtonsAccessForSelectedTract();
    updateTabWidgetLockState();
}

void MainWindow::appendDeviceLogLine(const QString &msg, const QColor &color)
{
    if (!ui || !ui->logTextEdit) {
        return;
    }
    QTextEdit *te = ui->logTextEdit;
    const QString timeStr = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    const QString line = QStringLiteral("[%1] %2").arg(timeStr, msg);

    QTextCursor c(te->document());
    c.movePosition(QTextCursor::End);
    if (c.position() != 0) {
        c.insertBlock();
    }
    QTextCharFormat fmt;
    fmt.setForeground(QBrush(color));
    c.setCharFormat(fmt);
    c.insertText(line);

    te->setTextCursor(c);
    if (QScrollBar *sb = te->verticalScrollBar()) {
        sb->setValue(sb->maximum());
    }
}

void MainWindow::appendDeviceLogLine(const QString &msg)
{
    appendDeviceLogLine(msg, applicationLogColorForMessage(msg));
}

void MainWindow::onDeviceLogMessage(const QString &msg) {
    appendDeviceLogLine(msg);
}

void MainWindow::onDeviceError(const QString &err) {
    if (m_bkuUpdateMode) {
        return;
    }
    if (m_stationDisconnectRecoveryActive || !shouldShowDeviceErrorToOperator(err)) {
        DEBUG << QStringLiteral("Device error (suppressed from operator log): %1").arg(err);
        return;
    }
    onDeviceLogMessage(QString("ОШИБКА: %1").arg(err));
    // Защита от "пропавших" статусных фреймов при ошибках на старте.
    if (ui && ui->frameStation) {
        ui->frameStation->setVisible(true);
    }
    if (ui && ui->frameR3) {
        ui->frameR3->setVisible(true);
    }
}

void MainWindow::onAnalyzerConnected()
{
    const bool wasInDisconnectRecovery = m_analyzerDisconnectRecoveryActive;
    m_analyzerDisconnectRecoveryActive = false;
    m_analyzerConnected = true;
    setAnalyzerConnectedUi();
    if (wasInDisconnectRecovery) {
        onDeviceLogMessage(QStringLiteral("Связь с анализатором восстановлена."));
    } else {
        onDeviceLogMessage(QStringLiteral("Успешное подключение к анализатору."));
    }

    updateTabWidgetLockState();
    if (ui && ui->tabWidget && ui->tabWidget->currentIndex() >= 0) {
        onTabWidgetCurrentChanged(ui->tabWidget->currentIndex());
    } else {
        syncAnalyzerKeepAliveForCurrentTab();
    }

    if (wasInDisconnectRecovery) {
        const int powerTr = (m_powerTestTargetTract != 0U) ? static_cast<int>(m_powerTestTargetTract)
                                                           : selectedPpmTractFromUi();
        if (m_powerTestPaused && m_powerTestBlockedByAnalyzerDisconnect && powerTr > 0) {
            m_powerTestBlockedByAnalyzerDisconnect = false;
            attemptScheduleDelayedPowerTestResume(powerTr);
        }

        if (m_receiveTestRunning && m_receiveTestPaused && m_receiveTestAutoPausedByAnalyzerDisconnect
            && m_receiveTestTract > 0) {
            m_receiveTestAutoPausedByAnalyzerDisconnect = false;
            attemptScheduleDelayedReceiveTestResume(m_receiveTestTract);
        }

        if (m_fhssBlockedByAnalyzerDisconnect && m_fhssTract > 0) {
            m_fhssBlockedByAnalyzerDisconnect = false;
            attemptScheduleDelayedFhssTestResume(m_fhssTract);
        }
    }

    updatePowerTestButtonsAccessForSelectedTract();
    updateReceiveTestButtonsAccessForSelectedTract();
    updateFhssTestButtonsAccessForSelectedTract();
    refreshStartTestingButtonEnabled();
    updateTabWidgetLockState();
}

void MainWindow::onAnalyzerDisconnected(const QString &reason)
{
    Q_UNUSED(reason);
    if (kAnalyzerRepairBypass) {
        // КОСТЫЛЬ_АНАЛИЗАТОР_РЕМОНТ: не переводим тестовые режимы в паузу из-за отсутствия анализатора.
        setAnalyzerDisconnectedUi();
        m_analyzerConnected = true;
        m_analyzerDisconnectRecoveryActive = false;
        updatePowerTestButtonsAccessForSelectedTract();
        updateReceiveTestButtonsAccessForSelectedTract();
        updateFhssTestButtonsAccessForSelectedTract();
        refreshStartTestingButtonEnabled();
        updateTabWidgetLockState();
        return;
    }
    const bool wasAlreadyInRecovery = m_analyzerDisconnectRecoveryActive;
    m_analyzerConnected = false;
    m_analyzerDisconnectRecoveryActive = true;

    stopSpectrumStream();
    m_startSpectrumOnHands = false;

    const bool powerTestHasStateToPauseOrResume =
        (ui && ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked())
        || m_powerTestPaused
        || (m_powerTestSequenceIndex >= 0 && !m_powerTestSequenceFreqsHz.isEmpty());
    if (powerTestHasStateToPauseOrResume) {
        pausePowerTestForAnalyzerDisconnect();
    } else {
        m_powerTestBlockedByAnalyzerDisconnect = false;
    }

    pauseReceiveTestForAnalyzerDisconnect();
    pauseFhssForAnalyzerDisconnect();

    setAnalyzerDisconnectedUi();

    if (!wasAlreadyInRecovery) {
        appendDeviceLogLine(QStringLiteral("Потеряна связь с анализатором"),
                            QColor(QStringLiteral("#f87171")));
    }

    updatePowerTestButtonsAccessForSelectedTract();
    updateReceiveTestButtonsAccessForSelectedTract();
    updateFhssTestButtonsAccessForSelectedTract();
    if (!isActivePpmTestingSession()) {
        refreshStartTestingButtonEnabled();
    }
    updateTabWidgetLockState();
}

void MainWindow::onAnalyzerLogMessage(const QString &msg)
{
    DEBUG << msg;
}

void MainWindow::setStationConnectedUi() {
    ui->frameStation->setStyleSheet(styleSheetConnectStation);
    ui->labelPixStation->setPixmap(QPixmap(":/led_green.png"));
    setStatusLedGlowColor(m_stationLedGlowEffect, QStringLiteral("#22c55e"));
    ui->labelStateStation->setText("Подключена");
    ui->labelStateStation->setStyleSheet("color: #8AE08A;");
    if (ui->actionBkuUpdate) {
        ui->actionBkuUpdate->setEnabled(true);
    }
    if (m_updateBkuWidget && m_deviceController) {
        m_updateBkuWidget->setStationContext(m_deviceController->config().stationIp.trimmed(),
                                             connectedInterfaceName());
    }
}

void MainWindow::updateStationLabelText()
{
    if (!ui || !ui->labelStation) {
        return;
    }
    if (!m_stationLabelFixedText.isEmpty()) {
        ui->labelStation->setText(m_stationLabelFixedText);
        return;
    }
    if (m_stationLabelNumber > 0 && !m_stationHardwareVariant.isEmpty()) {
        m_stationLabelFixedText =
            QStringLiteral("РАДИОСТАНЦИЯ №%1v%2").arg(m_stationLabelNumber).arg(m_stationHardwareVariant);
        ui->labelStation->setText(m_stationLabelFixedText);
        return;
    }
    if (m_stationLabelNumber > 0) {
        ui->labelStation->setText(QStringLiteral("РАДИОСТАНЦИЯ №%1").arg(m_stationLabelNumber));
        return;
    }
    ui->labelStation->setText(QStringLiteral("РАДИОСТАНЦИЯ"));
}

void MainWindow::setStationDisconnectedUi() {
    if (ui && ui->frameStation) {
        ui->frameStation->setVisible(true);
    }
    ui->frameStation->setStyleSheet(styleSheetDisconnectStation);
    ui->labelPixStation->setPixmap(QPixmap(":/led_red.png"));
    setStatusLedGlowColor(m_stationLedGlowEffect, QStringLiteral("#ef4444"));
    ui->labelStateStation->setText("Отключена");
    ui->labelStateStation->setStyleSheet("color: #ff5252;");
    resetPowerReadoutUi();
    // Важно: если тест приёма стоит на автопаузе из-за потери связи со станцией,
    // ранее накопленные результаты полосок НЕ должны обнуляться.
    // Сбрасываем только "живые" текущие readout-поля, которые невалидны без связи.
    const bool keepReceiveAccumulatedResults =
        m_receiveTestRunning && m_receiveTestPaused && m_receiveTestAutoPausedByPpmNotReady;
    if (keepReceiveAccumulatedResults) {
        if (ui->lcdRecieveFreqValue) {
            ui->lcdRecieveFreqValue->display(QStringLiteral("----"));
        }
        if (ui->lcdRecieveRSSIValue) {
            ui->lcdRecieveRSSIValue->display(QStringLiteral("----"));
        }
    } else {
        resetReceiveReadoutUi();
    }
    m_ppmLastWorkModeByTract.clear();
    m_powerLevelCodeByTract.clear();
    m_ppmModeLaunchPendingByTract.clear();
    m_ppmModeLaunchTimedOutByTract.clear();
    m_ppmModeLaunchSinceMsByTract.clear();
    m_ppmExternalDirRecoveryTract = -1;
    m_externalSwitchProtectionArmed = false;
    if (ui) {
        if (shouldKeepStationHeaderProgressVisible()) {
            // Штатный reboot/инициализация: progressBar управляется сценарием теста, не сбрасываем его здесь.
            if (ui->framePPM) {
                ui->framePPM->setVisible(false);
            }
        } else if (isActivePpmTestingSession()) {
            // Уже в режиме тестирования: framePPM остаётся, тракты блокируются до восстановления связи.
            showStationHeaderCenter(StationHeaderCenter::FramePpm);
            setAllPpmRadiosEnabled(false);
        } else {
            showStationHeaderCenter(StationHeaderCenter::StartButton);
        }
    }
    setPpmUpdateLabelVisible(true);
    applyPpmTransmitterLabel(QStringLiteral("—"), PpmStatusStyle::Fault);
    applyPpmModeFrameIdle();
    if (ui->actionBkuUpdate) {
        // Пункт всегда доступен: при обрыве связи нужен вход в режим БКУ (аварийный TFTP).
        ui->actionBkuUpdate->setEnabled(true);
    }
}

void MainWindow::resetPowerReadoutUi()
{
    if (ui->lcdPowerFreqValue) {
        ui->lcdPowerFreqValue->display(QStringLiteral("----"));
    }
    if (ui->lcdPowerRSSIValue) {
        ui->lcdPowerRSSIValue->display(QStringLiteral("----"));
    }
}

void MainWindow::setAnalyzerConnectedUi()
{
    ui->frameR3->setVisible(true);
    ui->frameR3->setStyleSheet(styleSheetConnectAnalyzer);
    ui->labelPixR3->setPixmap(QPixmap(":/led_green.png"));
    setStatusLedGlowColor(m_r3LedGlowEffect, QStringLiteral("#22c55e"));
    ui->labelStateR3->setText("Подключен");
    ui->labelStateR3->setStyleSheet("color: #8AE08A;");
}

void MainWindow::setAnalyzerDisconnectedUi()
{
    if (ui && ui->frameR3) {
        ui->frameR3->setVisible(true);
    }
    clearPowerMomentSpectrumPlot();
    setEmissionAnimating(false);
    ui->frameR3->setStyleSheet(styleSheetDisconnectAnalyzer);
    ui->labelPixR3->setPixmap(QPixmap(":/led_red.png"));
    setStatusLedGlowColor(m_r3LedGlowEffect, QStringLiteral("#ef4444"));
    ui->labelStateR3->setText("Отключен");
    ui->labelStateR3->setStyleSheet("color: #ff5252;");
}

void MainWindow::setTestingUiBusy(bool busy)
{
    if (!ui || !ui->progressBar) {
        return;
    }
    if (busy) {
        showStationHeaderCenter(StationHeaderCenter::ProgressBar);
        ui->progressBar->setRange(0, 0);
        ui->progressBar->setValue(0);
    } else {
        ui->progressBar->setRange(0, 100);
        ui->progressBar->setValue(0);
        showStationHeaderCenter(StationHeaderCenter::StartButton);
        m_externalSwitchProtectionArmed = false;
    }
}

void MainWindow::setStartTestingButtonEnabled(bool enabled)
{
    if (m_bkuUpdateMode) {
        updateBkuStartButtonState();
        return;
    }
    m_startTestingButtonAllowed = enabled;
    refreshStartTestingButtonEnabled();
}

void MainWindow::refreshStartTestingButtonEnabled()
{
    if (m_bkuUpdateMode) {
        return;
    }
    if (!ui || !ui->pushButtonStartTesting) {
        return;
    }
    // КОСТЫЛЬ_АНАЛИЗАТОР_РЕМОНТ: временно разрешаем кнопку без аппаратного анализатора.
    const bool enabled = m_startTestingButtonAllowed && analyzerAvailableForUi(m_analyzerConnected);
    ui->pushButtonStartTesting->setEnabled(enabled);
    if (enabled) {
        startStartTestingButtonGlow();
    } else {
        stopStartTestingButtonGlow();
    }
}

void MainWindow::ensureUpdateBkuUiInitialized()
{
    if (m_updateBkuWidget) {
        return;
    }
    if (!ui || !ui->verticalLayout_5 || !ui->tabWidget) {
        return;
    }

    if (ui->pushButtonStartTesting) {
        m_startTestingNormalText = ui->pushButtonStartTesting->text();
    }

    m_tabWidgetLayoutIndex = ui->verticalLayout_5->indexOf(ui->tabWidget);
    m_updateBkuWidget = new UpdateBkuWidget(this);
    ui->verticalLayout_5->insertWidget(m_tabWidgetLayoutIndex + 1, m_updateBkuWidget);
    ui->verticalLayout_5->setStretch(m_tabWidgetLayoutIndex + 1, 8);
    m_updateBkuWidget->hide();

    m_updateBkuWidget->setEnsureTftpServerIpFn(
        [this](QString *errorText, bool *addressWasAdded, bool strict, bool *networkAddressReady) {
            return configureTftpServerNetwork(errorText, addressWasAdded, strict, networkAddressReady);
        });
    m_updateBkuWidget->setResolveStationIpFn([this]() {
        return m_deviceController ? m_deviceController->config().stationIp.trimmed() : QString();
    });

    connect(m_updateBkuWidget, &UpdateBkuWidget::stationReconnectAfterRebootRequested, this,
            [this](const QString &stationIp) {
                if (m_updateBkuWidget) {
                    m_updateBkuWidget->setStationContext(stationIp, connectedInterfaceName());
                }
                startBkuPostKernelBootWait(stationIp, false);
            });
    connect(m_updateBkuWidget, &UpdateBkuWidget::postEmergencyTftpWaitingStarted, this, [this]() {
        QString stationIp;
        if (m_deviceController) {
            stationIp = m_deviceController->config().stationIp.trimmed();
        }
        if (m_updateBkuWidget) {
            if (!stationIp.isEmpty()) {
                m_updateBkuWidget->setStationContext(stationIp, connectedInterfaceName());
            }
        }
        startBkuPostKernelBootWait(stationIp, true);
    });
    connect(m_updateBkuWidget, &UpdateBkuWidget::deferredTestingInitRequired, this, [this]() {
        m_deferredTestingConnectInit = true;
        m_preparedProfileTar.reset();
        m_preparedProfileStationIp.clear();
        m_stationNeedsProfileRegistrySeed = false;
        m_stationHardwareVariant.clear();
        cancelBkuKernelBootWait();
        m_emergencyConnectRetryTimer.stop();
    });

    connect(m_updateBkuWidget, &UpdateBkuWidget::logMessage, this,
            [this](const QString &message, const QString &color) {
                QColor logColor = applicationLogColorForMessage(message);
                if (color == QStringLiteral("red")) {
                    logColor = QColor(QStringLiteral("#f87171"));
                } else if (color == QStringLiteral("yellow")) {
                    logColor = QColor(QStringLiteral("#fbbf24"));
                } else if (color == QStringLiteral("blue")) {
                    logColor = QColor(QStringLiteral("#60a5fa"));
                } else if (color == QStringLiteral("green")) {
                    logColor = QColor(QStringLiteral("#4ade80"));
                } else if (color == QStringLiteral("gray")) {
                    logColor = QColor(QStringLiteral("#9ca3af"));
                }
                appendDeviceLogLine(message, logColor);
            });
    connect(m_updateBkuWidget, &UpdateBkuWidget::progressChanged, this, [this](int value) {
        if (!m_bkuUpdateMode || !ui->progressBar) {
            return;
        }
        showStationHeaderCenter(StationHeaderCenter::ProgressBar);
        applyStationHeaderProgressBarLayout(true);
        if (value < 0) {
            ui->progressBar->setRange(0, 0);
            ui->progressBar->setValue(0);
            ui->progressBar->setTextVisible(false);
            return;
        }
        ui->progressBar->setRange(0, 100);
        ui->progressBar->setValue(value);
        ui->progressBar->setFormat(QStringLiteral("%p%"));
        ui->progressBar->setTextVisible(true);
    });
    connect(m_updateBkuWidget, &UpdateBkuWidget::updateBusyChanged, this, [this](bool busy) {
        if (!m_bkuUpdateMode) {
            return;
        }
        if (!busy) {
            cancelBkuKernelBootWait();
            m_emergencyConnectRetryTimer.stop();
            if (m_updateBkuWidget && m_deviceController) {
                const QString ip = m_deviceController->config().stationIp.trimmed();
                applyStationHeaderFromIp(ip);
                const QString variant = m_updateBkuWidget->stationVariantForLabel().trimmed();
                if (!variant.isEmpty()) {
                    m_stationHardwareVariant = variant;
                    updateStationLabelText();
                }
                if (m_deviceController->isConnected()) {
                    setStationConnectedUi();
                } else if (!ip.isEmpty()) {
                    m_deviceController->connectToDevice();
                }
            }
        }
        if (busy) {
            showStationHeaderCenter(StationHeaderCenter::ProgressBar);
            applyStationHeaderProgressBarLayout(true);
            if (ui->progressBar) {
                ui->progressBar->setRange(0, 0);
                ui->progressBar->setValue(0);
                ui->progressBar->setTextVisible(false);
            }
        } else {
            applyStationHeaderProgressBarLayout(false);
            if (ui->progressBar) {
                ui->progressBar->setRange(0, 100);
                ui->progressBar->setValue(0);
                ui->progressBar->setFormat(QStringLiteral("%p%"));
                ui->progressBar->setTextVisible(false);
                showStationHeaderCenter(StationHeaderCenter::StartButton);
            }
        }
        updateBkuStartButtonState();
    });
    connect(m_updateBkuWidget, &UpdateBkuWidget::startUpdateButtonEnabledChanged, this,
            [this](bool /*enabled*/) { updateBkuStartButtonState(); });
    connect(m_updateBkuWidget, &UpdateBkuWidget::bkuHeaderButtonStateChanged, this,
            &MainWindow::updateBkuStartButtonState);
}

QString MainWindow::connectedInterfaceName() const
{
    for (auto it = m_addedIps.crbegin(); it != m_addedIps.crend(); ++it) {
        if (!it->iface.isEmpty()) {
            return it->iface;
        }
    }
    return {};
}

QString MainWindow::connectedConnectionUuid() const
{
    const QString interfaceName = connectedInterfaceName();
    if (interfaceName.isEmpty()) {
        return {};
    }

    for (auto it = m_addedIps.crbegin(); it != m_addedIps.crend(); ++it) {
        if (it->iface == interfaceName && !it->connectionUuid.isEmpty()) {
            return it->connectionUuid;
        }
    }

    const QString command =
        QStringLiteral("nmcli -t -f UUID,DEVICE connection show --active | grep -F \":%1\" | cut -d':' -f1")
            .arg(interfaceName);
    const QPair<bool, QString> result = executeCommand(command);
    return result.second.trimmed().split('\n', Qt::SkipEmptyParts).value(0).trimmed();
}

bool MainWindow::tryAssignTftpServerIpOnInterface(const QString &interfaceName, bool *addressWasAdded,
                                                   QString *errorText) const
{
    const QString iface = interfaceName.trimmed();
    if (iface.isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("Сетевой интерфейс не указан.");
        }
        return false;
    }

    QString command = QStringLiteral("ip -4 -o addr show dev %1").arg(iface);
    QPair<bool, QString> result = executeCommand(command);
    if (result.first && result.second.contains(QStringLiteral("192.168.0.15"))) {
        if (addressWasAdded) {
            *addressWasAdded = false;
        }
        return true;
    }

    command = QStringLiteral("sudo ip addr add 192.168.0.15/24 dev %1").arg(iface);
    result = executeCommand(command);
    if (!result.first) {
        if (errorText) {
            *errorText = QStringLiteral("Не удалось назначить 192.168.0.15/24 на %1: %2")
                             .arg(iface, result.second.trimmed());
        }
        return false;
    }

    if (addressWasAdded) {
        *addressWasAdded = true;
    }
    return true;
}

bool MainWindow::configureTftpServerNetwork(QString *errorText, bool *addressWasAdded, bool strict,
                                            bool *networkAddressReady) const
{
    if (addressWasAdded) {
        *addressWasAdded = false;
    }
    if (networkAddressReady) {
        *networkAddressReady = false;
    }

    const QString interfaceName = connectedInterfaceName();
    const QString connectionUuid = connectedConnectionUuid();
    if (!connectionUuid.isEmpty()) {
        QString command =
            QStringLiteral("nmcli -g ipv4.addresses connection show uuid \"%1\"").arg(connectionUuid);
        QPair<bool, QString> result = executeCommand(command);
        const QStringList ipList =
            result.second.split(QRegularExpression(QStringLiteral("[,/\\s]+")), Qt::SkipEmptyParts);
        const bool alreadyConfigured = ipList.contains(QStringLiteral("192.168.0.15"));
        if (alreadyConfigured) {
            if (networkAddressReady) {
                *networkAddressReady = true;
            }
            return true;
        }
        if (ensureTftpServerIpConfigured(errorText)) {
            if (addressWasAdded) {
                *addressWasAdded = true;
            }
            if (networkAddressReady) {
                *networkAddressReady = true;
            }
            return true;
        }
        if (strict) {
            return false;
        }
    } else if (!interfaceName.isEmpty()
               && tryAssignTftpServerIpOnInterface(interfaceName, addressWasAdded, errorText)) {
        if (networkAddressReady) {
            *networkAddressReady = true;
        }
        return true;
    } else if (strict) {
        if (errorText) {
            *errorText = interfaceName.isEmpty()
                             ? QStringLiteral("Сетевой интерфейс не определён.")
                             : QStringLiteral("Активное сетевое соединение не найдено.");
        }
        return false;
    }

    if (!strict) {
        for (const QString &iface : collectEligibleInterfaces()) {
            if (tryAssignTftpServerIpOnInterface(iface, addressWasAdded, nullptr)) {
                if (networkAddressReady) {
                    *networkAddressReady = true;
                }
                return true;
            }
        }
        if (errorText) {
            errorText->clear();
        }
        return true;
    }

    return false;
}

bool MainWindow::ensureTftpServerIpConfigured(QString *errorText) const
{
    const QString interfaceName = connectedInterfaceName();
    if (interfaceName.isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("Сетевой интерфейс не определён.");
        }
        return false;
    }

    const QString connectionUuid = connectedConnectionUuid();
    if (connectionUuid.isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("Активное сетевое соединение не найдено.");
        }
        return false;
    }

    QString command = QStringLiteral("nmcli -g ipv4.addresses connection show uuid \"%1\"").arg(connectionUuid);
    QPair<bool, QString> result = executeCommand(command);
    if (!result.first) {
        if (errorText) {
            *errorText = QStringLiteral("Не удалось прочитать IP-адреса подключения: %1")
                             .arg(result.second.trimmed());
        }
        return false;
    }

    const QStringList ipList =
        result.second.split(QRegularExpression(QStringLiteral("[,/\\s]+")), Qt::SkipEmptyParts);
    if (ipList.contains(QStringLiteral("192.168.0.15"))) {
        return true;
    }

    command = QStringLiteral("nmcli connection modify uuid \"%1\" +ipv4.method manual +ipv4.addresses 192.168.0.15/24")
                  .arg(connectionUuid);
    result = executeCommand(command);
    if (!result.first) {
        result = executeCommand(QStringLiteral("sudo %1").arg(command));
    }
    if (!result.first) {
        if (errorText) {
            *errorText = QStringLiteral("Ошибка при добавлении IP 192.168.0.15/24: %1")
                             .arg(result.second.trimmed());
        }
        return false;
    }

    command = QStringLiteral("nmcli device disconnect \"%1\"").arg(interfaceName);
    result = executeCommand(command);
    if (!result.first) {
        result = executeCommand(QStringLiteral("sudo %1").arg(command));
    }
    if (!result.first) {
        if (errorText) {
            *errorText = QStringLiteral("Ошибка перезагрузки интерфейса %1 (disconnect): %2")
                             .arg(interfaceName, result.second.trimmed());
        }
        return false;
    }

    command = QStringLiteral("nmcli device connect \"%1\"").arg(interfaceName);
    result = executeCommand(command);
    if (!result.first) {
        result = executeCommand(QStringLiteral("sudo %1").arg(command));
    }
    if (!result.first) {
        if (errorText) {
            *errorText = QStringLiteral("Ошибка перезагрузки интерфейса %1 (connect): %2")
                             .arg(interfaceName, result.second.trimmed());
        }
        return false;
    }

    return true;
}

void MainWindow::updateBkuStartButtonState()
{
    if (!m_bkuUpdateMode || !ui->pushButtonStartTesting || !m_updateBkuWidget) {
        return;
    }
    if (m_updateBkuWidget->isUpdateInProgress()) {
        ui->pushButtonStartTesting->setEnabled(false);
        stopStartTestingButtonGlow();
        return;
    }

    const bool emergencyMode = !m_updateBkuWidget->isStationLinkedForUpdate()
                               && !m_updateBkuWidget->isAwaitingPostUpdateUdpLink();
    if (emergencyMode) {
        ui->pushButtonStartTesting->setText(QStringLiteral("Аварийный запуск сервера-TFTP"));
        const bool enabled = m_updateBkuWidget->canStartEmergencyTftp();
        ui->pushButtonStartTesting->setEnabled(enabled);
    } else {
        ui->pushButtonStartTesting->setText(QStringLiteral("ОБНОВИТЬ БКУ"));
        const bool enabled = m_updateBkuWidget->canStartUpdate();
        ui->pushButtonStartTesting->setEnabled(enabled);
    }

    if (ui->pushButtonStartTesting->isEnabled()) {
        startStartTestingButtonGlow();
    } else {
        stopStartTestingButtonGlow();
    }
}

void MainWindow::setBkuUpdateMode(bool enabled)
{
    if (m_bkuUpdateMode == enabled) {
        return;
    }

    if (enabled) {
        ensureUpdateBkuUiInitialized();
        if (!m_updateBkuWidget) {
            onDeviceLogMessage(QStringLiteral("ОШИБКА: не удалось инициализировать режим обновления БКУ."));
            return;
        }
    }

    m_bkuUpdateMode = enabled;

    if (ui->actionBkuUpdate) {
        ui->actionBkuUpdate->setText(enabled ? QStringLiteral("Тестирование")
                                             : QStringLiteral("Обновление БКУ"));
        // В режиме БКУ пункт «Тестирование» всегда доступен (кроме блокировки в on_actionBkuUpdate
        // во время активного обновления). Не отключаем здесь — иначе нельзя выйти из режима.
        ui->actionBkuUpdate->setEnabled(true);
    }

    if (ui->frameR3) {
        ui->frameR3->setVisible(!enabled);
    }

    if (ui->tabWidget) {
        ui->tabWidget->setVisible(!enabled);
    }
    if (m_updateBkuWidget) {
        m_updateBkuWidget->setVisible(enabled);
        if (enabled) {
            suspendTestingSystemsForBkuMode();
            const QString stationIp = m_deviceController ? m_deviceController->config().stationIp.trimmed()
                                                         : QString();
            m_updateBkuWidget->setStationContext(stationIp, connectedInterfaceName());
            const bool stationLinked = m_deviceController && m_deviceController->isConnected();
            m_updateBkuWidget->setStationLinkActive(stationLinked);
            m_updateBkuWidget->activatePanel();
        } else {
            m_updateBkuWidget->deactivatePanel();
            if (m_deviceController) {
                m_deviceController->setInactivityWatchdogEnabled(true);
            }
            updateTabWidgetLockState();
            if (m_deferredTestingConnectInit && m_deviceController && m_deviceController->isConnected()) {
                m_deferredTestingConnectInit = false;
                const QString ip = m_deviceController->config().stationIp.trimmed();
                m_stationDisconnectRecoveryActive = false;
                QTimer::singleShot(0, this, [this, ip]() {
                    // После reboot в режиме БКУ — полная инициализация как при первом подключении.
                    handleNormalStationConnected(ip, false);
                });
            }
        }
    }

    if (ui->pushButtonStartTesting) {
        ui->pushButtonStartTesting->setCheckable(false);
        if (!enabled) {
            ui->pushButtonStartTesting->setText(m_startTestingNormalText);
        }
    }

    if (enabled) {
        updateBkuStartButtonState();
    } else if (m_deviceController && m_deviceController->isConnected() && m_preparedProfileTar
               && m_preparedProfileStationIp == m_deviceController->config().stationIp.trimmed()
               && !m_preparingProfile && !isActivePpmTestingSession()) {
        setStartTestingButtonEnabled(true);
    } else if (!isActivePpmTestingSession()) {
        setStartTestingButtonEnabled(false);
    }
}

bool MainWindow::shouldProcessStationTestingUdp() const
{
    if (!m_bkuUpdateMode) {
        return true;
    }
    return m_updateBkuWidget && m_updateBkuWidget->isAwaitingBootcmdReset();
}

void MainWindow::suspendTestingSystemsForBkuMode()
{
    m_externalSwitchProtectionArmed = false;
    m_stationDisconnectRecoveryActive = false;
    m_ppmExternalDirRecoveryTract = -1;
    m_ppmRestoreDefaultDirPendingByTract.clear();
    m_ppmRestoreDefaultDirInFlightByTract.clear();
    // Долгие SSH-запросы loadStationInfo() блокируют UI-поток: без этого срабатывает
    // ложный таймаут UDP (12 с) и блокируется пункт меню «Тестирование».
    if (m_deviceController) {
        m_deviceController->setInactivityWatchdogEnabled(false);
    }
}

void MainWindow::on_actionBkuUpdate_triggered()
{
    if (m_updateBkuWidget && m_updateBkuWidget->isUpdateInProgress()) {
        onDeviceLogMessage(QStringLiteral("Нельзя переключить режим во время обновления БКУ."));
        return;
    }

    if (!m_bkuUpdateMode) {
        const std::optional<BlocType> connectedBlocType = askConnectedBlocType();
        if (!connectedBlocType.has_value()) {
            return;
        }
        ensureUpdateBkuUiInitialized();
        if (!m_updateBkuWidget) {
            onDeviceLogMessage(QStringLiteral("ОШИБКА: не удалось инициализировать режим обновления БКУ."));
            return;
        }
        m_updateBkuWidget->setConnectedBlocType(*connectedBlocType);
    }

    setBkuUpdateMode(!m_bkuUpdateMode);
}

std::optional<BlocType> MainWindow::askConnectedBlocType()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Подключение"));
    dialog.setModal(true);
    dialog.setStyleSheet(stylesheetChoiceDialog);

    auto *mainLayout = new QVBoxLayout(&dialog);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(16);

    auto *textLabel = new QLabel(QStringLiteral("К какому блоку произведено подключение?"), &dialog);
    textLabel->setWordWrap(true);
    mainLayout->addWidget(textLabel);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    auto *bkuButton = new QPushButton(QStringLiteral("БКУ"), &dialog);
    auto *bkiButton = new QPushButton(QStringLiteral("БКИ"), &dialog);
    auto *buButton = new QPushButton(QStringLiteral("БУ"), &dialog);
    auto *cancelButton = new QPushButton(QStringLiteral("Отмена"), &dialog);
    bkuButton->setDefault(true);
    bkuButton->setAutoDefault(true);

    buttonLayout->addStretch();
    buttonLayout->addWidget(bkuButton);
    buttonLayout->addWidget(bkiButton);
    buttonLayout->addWidget(buButton);
    buttonLayout->addSpacing(12);
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);

    std::optional<BlocType> selected;
    connect(bkuButton, &QPushButton::clicked, &dialog, [&]() {
        selected = BlocType::BKU;
        dialog.accept();
    });
    connect(bkiButton, &QPushButton::clicked, &dialog, [&]() {
        selected = BlocType::BKI;
        dialog.accept();
    });
    connect(buButton, &QPushButton::clicked, &dialog, [&]() {
        selected = BlocType::BU;
        dialog.accept();
    });
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted && selected.has_value()) {
        return selected;
    }
    return std::nullopt;
}

void MainWindow::initStartTestingButtonGlow()
{
    if (!ui || !ui->pushButtonStartTesting || m_startTestingGlowAnimation) {
        return;
    }

    QPushButton *button = ui->pushButtonStartTesting;
    m_startTestingBaseStyleSheet = button->styleSheet();

    m_startTestingGlowEffect = new QGraphicsDropShadowEffect(button);
    m_startTestingGlowEffect->setOffset(0, 0);
    m_startTestingGlowEffect->setBlurRadius(0);
    m_startTestingGlowEffect->setColor(Qt::transparent);
    m_startTestingGlowEffect->setEnabled(false);
    button->setGraphicsEffect(m_startTestingGlowEffect);

    m_startTestingGlowAnimation = new QVariantAnimation(this);
    m_startTestingGlowAnimation->setStartValue(0.0);
    m_startTestingGlowAnimation->setEndValue(1.0);
    m_startTestingGlowAnimation->setDuration(2400);
    m_startTestingGlowAnimation->setLoopCount(-1);
    m_startTestingGlowAnimation->setEasingCurve(QEasingCurve::InOutSine);
    connect(m_startTestingGlowAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) {
                applyStartTestingButtonGlowFrame(value.toReal());
            });
}

void MainWindow::startStartTestingButtonGlow()
{
    if (!ui || !ui->pushButtonStartTesting) {
        return;
    }
    initStartTestingButtonGlow();
    if (!m_startTestingGlowAnimation) {
        return;
    }

    if (m_startTestingGlowEffect) {
        m_startTestingGlowEffect->setEnabled(true);
    }
    if (!m_startTestingBaseStyleSheet.isEmpty()) {
        ui->pushButtonStartTesting->setStyleSheet(m_startTestingBaseStyleSheet);
    }
    if (m_startTestingGlowAnimation->state() != QAbstractAnimation::Running) {
        m_startTestingGlowAnimation->start();
    }
    applyStartTestingButtonGlowFrame(m_startTestingGlowAnimation->currentValue().toReal());
}

void MainWindow::stopStartTestingButtonGlow()
{
    if (m_startTestingGlowAnimation) {
        m_startTestingGlowAnimation->stop();
    }
    if (m_startTestingGlowEffect) {
        m_startTestingGlowEffect->setBlurRadius(0);
        m_startTestingGlowEffect->setColor(Qt::transparent);
        m_startTestingGlowEffect->setEnabled(false);
    }
    if (ui && ui->pushButtonStartTesting && !m_startTestingBaseStyleSheet.isEmpty()) {
        ui->pushButtonStartTesting->setStyleSheet(m_startTestingBaseStyleSheet);
    }
}

void MainWindow::applyStartTestingButtonGlowFrame(qreal progress)
{
    if (!ui || !ui->pushButtonStartTesting || !m_startTestingGlowEffect) {
        return;
    }

    progress = std::max<qreal>(0.0, std::min<qreal>(1.0, progress));
    const qreal wave = 0.5 - 0.5 * std::cos(progress * 2.0 * kPi);
    const int alpha = 215 + qRound(40.0 * wave);

    QColor glow = QColor(QStringLiteral("#22d3ee"));
    glow.setAlpha(alpha);
    m_startTestingGlowEffect->setBlurRadius(38.0 + 22.0 * wave);
    m_startTestingGlowEffect->setColor(glow);
}

void MainWindow::initStatusLedGlow()
{
    if (!ui || m_statusLedGlowAnimation) {
        return;
    }

    auto installGlow = [](QLabel *label, QGraphicsDropShadowEffect **effect) {
        if (!label || *effect) {
            return;
        }
        *effect = new QGraphicsDropShadowEffect(label);
        (*effect)->setOffset(0, 0);
        (*effect)->setBlurRadius(0);
        (*effect)->setColor(Qt::transparent);
        (*effect)->setEnabled(true);
        label->setGraphicsEffect(*effect);
    };

    installGlow(ui->labelPixStation, &m_stationLedGlowEffect);
    installGlow(ui->labelPixR3, &m_r3LedGlowEffect);

    m_statusLedGlowAnimation = new QVariantAnimation(this);
    m_statusLedGlowAnimation->setStartValue(0.0);
    m_statusLedGlowAnimation->setEndValue(1.0);
    m_statusLedGlowAnimation->setDuration(2400);
    m_statusLedGlowAnimation->setLoopCount(-1);
    m_statusLedGlowAnimation->setEasingCurve(QEasingCurve::InOutSine);
    connect(m_statusLedGlowAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) {
                applyStatusLedGlowFrame(value.toReal());
            });

    setStatusLedGlowColor(m_stationLedGlowEffect, QStringLiteral("#ef4444"));
    setStatusLedGlowColor(m_r3LedGlowEffect, QStringLiteral("#ef4444"));
    startStatusLedGlow();
}

void MainWindow::startStatusLedGlow()
{
    initStatusLedGlow();
    if (!m_statusLedGlowAnimation) {
        return;
    }
    if (m_statusLedGlowAnimation->state() != QAbstractAnimation::Running) {
        m_statusLedGlowAnimation->start();
    }
    applyStatusLedGlowFrame(m_statusLedGlowAnimation->currentValue().toReal());
}

void MainWindow::setStatusLedGlowColor(QGraphicsDropShadowEffect *effect, const QString &colorName)
{
    if (!effect) {
        return;
    }
    effect->setProperty("glowColor", colorName);
    effect->setEnabled(true);
    applyStatusLedGlowFrame(m_statusLedGlowAnimation ? m_statusLedGlowAnimation->currentValue().toReal() : 0.0);
}

void MainWindow::applyStatusLedGlowFrame(qreal progress)
{
    progress = std::max<qreal>(0.0, std::min<qreal>(1.0, progress));
    const qreal wave = 0.5 - 0.5 * std::cos(progress * 2.0 * kPi);

    auto applyGlow = [wave](QGraphicsDropShadowEffect *effect) {
        if (!effect) {
            return;
        }
        QColor glow(effect->property("glowColor").toString());
        if (!glow.isValid()) {
            glow = QColor(QStringLiteral("#22d3ee"));
        }
        glow.setAlpha(185 + qRound(55.0 * wave));
        effect->setBlurRadius(22.0 + 16.0 * wave);
        effect->setColor(glow);
    };

    applyGlow(m_stationLedGlowEffect);
    applyGlow(m_r3LedGlowEffect);
}

void MainWindow::startProfileIntegritySequenceAfterReboot(const QString &stationIp)
{
    const QString ip = stationIp.trimmed();
    if (ip.isEmpty()) {
        return;
    }

    m_profileIntegrityStationIp = ip;
    m_profileIntegrityStage = ProfileIntegrityStage::WaitingAfterReboot;

    if (debug) {
        onDeviceLogMessage(QStringLiteral("Контроль целостности профиля: ожидание перезагрузки радиостанции %1 с...")
                               .arg(POST_REBOOT_STATION_DOWN_WAIT_MS / 1000));
    }

    // UI: показываем прогресс 0..100% на время ожидания, затем переключимся в бесконечный режим.
    if (ui && ui->progressBar) {
        showStationHeaderCenter(StationHeaderCenter::ProgressBar);
        ui->progressBar->setTextVisible(true);
        ui->progressBar->setFormat(QStringLiteral("%p%"));
        ui->progressBar->setRange(0, 100);
        ui->progressBar->setValue(0);
    }
    m_postRebootWaitElapsed.restart();
    m_postRebootWaitProgressTimer.start();

    // Контроллер UDP: станция уходит в reboot, переводим соединение в "отключено"
    // и после ожидания начнём периодически слать MOD_START.
    if (m_deviceController && m_deviceController->isConnected()) {
        m_deviceController->disconnectFromDevice();
    }

    m_postRebootReconnectTimer.stop();
    m_postRebootWaitTimer.start(POST_REBOOT_STATION_DOWN_WAIT_MS);
}

void MainWindow::onPostRebootWaitTimeout()
{
    if (m_profileIntegrityStage != ProfileIntegrityStage::WaitingAfterReboot) {
        return;
    }
    if (!m_deviceController) {
        return;
    }
    if (m_profileIntegrityStationIp.trimmed().isEmpty()) {
        return;
    }

    if (debug) {
        onDeviceLogMessage(
            QStringLiteral("Контроль целостности профиля: ожидание завершено, начинаем переподключение (MOD_START)..."));
    }
    m_profileIntegrityStage = ProfileIntegrityStage::Reconnecting;

    // UI: ожидание завершено — переходим в бесконечный режим, пока станция не подключится.
    m_postRebootWaitProgressTimer.stop();
    if (ui && ui->progressBar) {
        showStationHeaderCenter(StationHeaderCenter::ProgressBar);
        ui->progressBar->setTextVisible(false);
        ui->progressBar->setRange(0, 0);
        ui->progressBar->setValue(0);
    }

    // Статус станции по ТЗ при начале переподключения.
    if (ui && ui->labelStateStation) {
        ui->labelStateStation->setText(QStringLiteral("Подключение..."));
    }

    m_deviceController->setStationIp(m_profileIntegrityStationIp);

    // Первый запрос — сразу, дальше периодически.
    m_deviceController->connectToDevice();
    m_postRebootReconnectTimer.start();
}

void MainWindow::onPostRebootWaitProgressTick()
{
    if (m_profileIntegrityStage != ProfileIntegrityStage::WaitingAfterReboot) {
        m_postRebootWaitProgressTimer.stop();
        return;
    }
    if (!ui || !ui->progressBar) {
        m_postRebootWaitProgressTimer.stop();
        return;
    }

    static const qint64 kTotalMs = POST_REBOOT_STATION_DOWN_WAIT_MS;
    const qint64 elapsed = m_postRebootWaitElapsed.isValid() ? m_postRebootWaitElapsed.elapsed() : 0;
    const int percent = qBound(0, static_cast<int>((elapsed * 100) / kTotalMs), 100);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(percent);
}

void MainWindow::onPostRebootReconnectTick()
{
    if (m_profileIntegrityStage != ProfileIntegrityStage::Reconnecting) {
        m_postRebootReconnectTimer.stop();
        return;
    }
    if (!m_deviceController) {
        m_postRebootReconnectTimer.stop();
        return;
    }
    if (m_deviceController->isConnected()) {
        m_postRebootReconnectTimer.stop();
        return;
    }
    m_deviceController->connectToDevice();
}

void MainWindow::beginPostReconnectStationBootWaitAfterProfileConnect()
{
    m_postReconnectStationBootProgressTimer.stop();
    m_postReconnectStationBootFallbackTimer.stop();

    m_postReconnectStationBootWaitActive = true;
    m_postReconnectStationBootSshOk = false;
    m_postReconnectStationBootLastTractOnSeen = false;
    m_postReconnectStationBootFallbackUsed = false;
    m_postReconnectStationBootIndeterminateUi = false;

    const QVector<int> tracts = ppmTractNumbersForUi();
    int tractCount = tracts.size();
    if (tractCount <= 0) {
        tractCount = static_cast<int>(DEFAULT_TRACT_NUM);
    }
    int lastNum = -1;
    for (int t : tracts) {
        if (t > lastNum) {
            lastNum = t;
        }
    }
    if (lastNum <= 0) {
        lastNum = tractCount;
    }
    m_postReconnectStationBootLastTractNum = lastNum;
    m_postReconnectStationBootTargetDurationMs = tractCount * kPostReconnectStationTractBootSecPerTract * 1000;
    if (m_postReconnectStationBootTargetDurationMs <= 0) {
        m_postReconnectStationBootTargetDurationMs = kPostReconnectStationTractBootSecPerTract * 1000;
    }

    if (ui && ui->progressBar) {
        showStationHeaderCenter(StationHeaderCenter::ProgressBar);
        ui->progressBar->setTextVisible(true);
        ui->progressBar->setFormat(QStringLiteral("%p%"));
        ui->progressBar->setRange(0, 100);
        ui->progressBar->setValue(0);
    }
    m_postReconnectStationBootElapsed.restart();
    m_postReconnectStationBootProgressTimer.start();

    const int fallbackMs = qMax(m_postReconnectStationBootTargetDurationMs * 2, 60000);
    m_postReconnectStationBootFallbackTimer.start(fallbackMs);

    if (debug) {
        onDeviceLogMessage(
            QStringLiteral("ППМ после reboot: оценка стартовой загрузки %1×%2 с (индикация ВКЛ тракта %3), "
                           "контроль md5 по SSH параллельно; запас по индикации %4 с.")
                .arg(tractCount)
                .arg(kPostReconnectStationTractBootSecPerTract)
                .arg(m_postReconnectStationBootLastTractNum)
                .arg(fallbackMs / 1000));
    }
}

void MainWindow::cancelPostReconnectStationBootWait(bool restoreProgressBar)
{
    m_postReconnectStationBootProgressTimer.stop();
    m_postReconnectStationBootFallbackTimer.stop();
    m_postReconnectStationBootWaitActive = false;
    m_postReconnectStationBootSshOk = false;
    m_postReconnectStationBootLastTractOnSeen = false;
    m_postReconnectStationBootFallbackUsed = false;
    m_postReconnectStationBootIndeterminateUi = false;

    if (restoreProgressBar && ui && ui->progressBar) {
        ui->progressBar->setTextVisible(false);
        ui->progressBar->setRange(0, 100);
        ui->progressBar->setValue(0);
        showStationHeaderCenter(StationHeaderCenter::StartButton);
    }
}

void MainWindow::tryStartPpmInitAfterPostReconnectBootGates()
{
    if (!m_postReconnectStationBootWaitActive) {
        return;
    }
    if (!m_postReconnectStationBootSshOk) {
        return;
    }
    if (!m_postReconnectStationBootLastTractOnSeen && !m_postReconnectStationBootFallbackUsed) {
        return;
    }
    if (!m_deviceController || !m_deviceController->isConnected()) {
        cancelPostReconnectStationBootWait(true);
        return;
    }

    const bool viaLastTractOnIndication = m_postReconnectStationBootLastTractOnSeen;

    m_postReconnectStationBootProgressTimer.stop();
    m_postReconnectStationBootFallbackTimer.stop();
    m_postReconnectStationBootWaitActive = false;
    m_postReconnectStationBootSshOk = false;
    m_postReconnectStationBootLastTractOnSeen = false;
    m_postReconnectStationBootFallbackUsed = false;
    m_postReconnectStationBootIndeterminateUi = false;

    m_profileIntegrityStage = ProfileIntegrityStage::None;
    m_profileIntegrityStationIp.clear();

    if (viaLastTractOnIndication) {
        onDeviceLogMessage(QStringLiteral("Тракты загружены. Переход к управлению:"));
    }
    startPpmInitAfterIntegrityOk();
}

void MainWindow::onPostReconnectStationBootProgressTick()
{
    if (!m_postReconnectStationBootWaitActive) {
        m_postReconnectStationBootProgressTimer.stop();
        return;
    }
    if (m_postReconnectStationBootIndeterminateUi) {
        return;
    }
    if (!ui || !ui->progressBar) {
        return;
    }
    const qint64 elapsed =
        m_postReconnectStationBootElapsed.isValid() ? m_postReconnectStationBootElapsed.elapsed() : 0;
    const int denom = qMax(1, m_postReconnectStationBootTargetDurationMs);
    const int percent = qBound(0, static_cast<int>((elapsed * 100) / denom), 100);
    if (percent >= 100) {
        m_postReconnectStationBootIndeterminateUi = true;
        m_postReconnectStationBootProgressTimer.stop();
        ui->progressBar->setTextVisible(false);
        ui->progressBar->setRange(0, 0);
        ui->progressBar->setValue(0);
        return;
    }
    ui->progressBar->setTextVisible(true);
    ui->progressBar->setFormat(QStringLiteral("%p%"));
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(percent);
}

void MainWindow::onPostReconnectStationBootFallbackTimeout()
{
    if (!m_postReconnectStationBootWaitActive) {
        return;
    }
    if (!m_postReconnectStationBootLastTractOnSeen) {
        m_postReconnectStationBootFallbackUsed = true;
        onDeviceLogMessage(
            QStringLiteral("ППМ: не получена индикация ВКЛ тракта %1 за запасной интервал; продолжаем инициализацию.")
                .arg(m_postReconnectStationBootLastTractNum));
    }
    tryStartPpmInitAfterPostReconnectBootGates();
}

bool MainWindow::verifyProfileIntegrityAfterRebootOverSsh(const QString &stationIp, QString *errorText)
{
    SSHer ssher;
    ssher.setAllowLegacyAlgorithms(true);

    auto logAsync = [&](const QString &msg) {
        if (!debug) {
            return;
        }
        QMetaObject::invokeMethod(this, [this, msg]() { onDeviceLogMessage(msg); }, Qt::QueuedConnection);
    };

    if (!ssher.connectToHost(stationIp.trimmed(), 22)) {
        if (errorText) {
            *errorText = ssher.lastError().isEmpty() ? QStringLiteral("Не удалось подключиться по SSH.") : ssher.lastError();
        }
        return false;
    }
    if (!ssher.authenticate(QString::fromLatin1(kStationSshUser), QString::fromLatin1(kStationSshPassword))) {
        if (errorText) {
            *errorText = ssher.lastError().isEmpty() ? QStringLiteral("Ошибка SSH аутентификации.") : ssher.lastError();
        }
        return false;
    }

    auto runChecked = [&](const QString &cmd, const QString &step) -> QPair<bool, QString> {
        int exitCode = 0;
        const QString out = ssher.executeCommand(cmd, &exitCode);
        if (exitCode == 0 && out.isEmpty() && !ssher.lastError().isEmpty()) {
            const QString msg = QString("[%1] Ошибка SSH при выполнении: %2\n%3").arg(step, ssher.lastError(), cmd);
            if (errorText) {
                *errorText = msg;
            }
            logAsync(msg);
            return {false, out};
        }
        if (exitCode != 0) {
            const QString details = out.trimmed().isEmpty() ? QStringLiteral("(нет вывода)") : out.trimmed();
            const QString msg = QString("[%1] Ошибка выполнения (exitCode=%2): %3\n%4")
                                    .arg(step)
                                    .arg(exitCode)
                                    .arg(cmd)
                                    .arg(details);
            if (errorText) {
                *errorText = msg;
            }
            logAsync(msg);
            return {false, out};
        }
        if (!out.trimmed().isEmpty()) {
            logAsync(QString("[%1] %2").arg(step, out.trimmed()));
        }
        return {true, out};
    };

    // 1) Архив после reboot.
    if (!runChecked(QStringLiteral(
                        "/bin/bash -lc 'cd /radio/profiles/Profile_Active/ && "
                        "find . -maxdepth 2 -type f \\( -name \"Trakts.xml\" -o -name \"Unions.xml\" -o -path "
                        "\"./Trakt_*/*.xml\" \\) -print0 | xargs -0 tar -cf "
                        "/radio/profile_active_after_reset.tar.gz'"),
                    QStringLiteral("tar after reboot"))
             .first) {
        runChecked(QStringLiteral("rm -f /radio/profile_active_before_reset.tar.gz /radio/profile_active_after_reset.tar.gz"),
                   QStringLiteral("cleanup archives"));
        return false;
    }

    // 2) md5sum и сравнение.
    const auto md5Res = runChecked(
        QStringLiteral("md5sum /radio/profile_active_before_reset.tar.gz /radio/profile_active_after_reset.tar.gz"),
        QStringLiteral("md5sum compare"));
    if (!md5Res.first) {
        runChecked(QStringLiteral("rm -f /radio/profile_active_before_reset.tar.gz /radio/profile_active_after_reset.tar.gz"),
                   QStringLiteral("cleanup archives"));
        return false;
    }

    const QStringList lines = md5Res.second.split('\n', Qt::SkipEmptyParts);
    QString md5Before;
    QString md5After;
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (t.contains("profile_active_before_reset.tar.gz")) {
            md5Before = t.split(' ', Qt::SkipEmptyParts).value(0).trimmed();
        } else if (t.contains("profile_active_after_reset.tar.gz")) {
            md5After = t.split(' ', Qt::SkipEmptyParts).value(0).trimmed();
        }
    }

    // 3) Удаляем оба архива независимо от результата.
    runChecked(QStringLiteral("rm -f /radio/profile_active_before_reset.tar.gz /radio/profile_active_after_reset.tar.gz"),
               QStringLiteral("cleanup archives"));

    if (md5Before.isEmpty() || md5After.isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("Не удалось распарсить вывод md5sum.");
        }
        return false;
    }
    if (md5Before != md5After) {
        if (errorText) {
            *errorText = QString("md5 не совпадает: before=%1 after=%2").arg(md5Before, md5After);
        }
        return false;
    }

    return true;
}

int MainWindow::selectedPpmTractFromUi() const
{
    if (!ui || !ui->framePPM || !m_ppmButtonGroup) {
        return -1;
    }
    const int id = m_ppmButtonGroup->checkedId();
    if (id < 0 || id >= m_ppmTractsSorted.size()) {
        return -1;
    }
    return m_ppmTractsSorted[id];
}

int MainWindow::ppmTrmTypeForTract(int tractNum) const
{
    return m_ppmTrmTypeByTract.value(tractNum, -1);
}

bool MainWindow::isFhssCapableTract(int tractNum) const
{
    const int trmType = ppmTrmTypeForTract(tractNum);
    return trmType >= 2 && trmType <= 4;
}

QString MainWindow::selectedPpmTractDisplayNameFromUi() const
{
    if (!m_ppmButtonGroup) {
        return QString();
    }
    const int id = m_ppmButtonGroup->checkedId();
    if (id < 0) {
        return QString();
    }
    QAbstractButton *btn = m_ppmButtonGroup->button(id);
    if (!btn) {
        return QString();
    }
    const QString text = btn->text().trimmed();
    if (text.isEmpty() || text == QStringLiteral("—")) {
        return QString();
    }
    return text;
}

QString MainWindow::powerTestPowerKindAdjectiveForLog() const
{
    const bool isMin = (m_powerLevelCode == 1);
    return isMin ? QStringLiteral("минимальной") : QStringLiteral("максимальной");
}

QString MainWindow::powerTestTractDisplayNameForLog() const
{
    QString tractName = selectedPpmTractDisplayNameFromUi();
    if (tractName.isEmpty() && m_powerTestTargetTract > 0U) {
        const int idx = m_ppmTractsSorted.indexOf(static_cast<int>(m_powerTestTargetTract));
        if (idx >= 0 && m_ppmButtonGroup) {
            if (QAbstractButton *btn = m_ppmButtonGroup->button(idx)) {
                const QString fallback = btn->text().trimmed();
                if (!fallback.isEmpty() && fallback != QStringLiteral("—")) {
                    tractName = fallback;
                }
            }
        }
    }
    if (tractName.isEmpty() && m_powerTestTargetTract > 0U) {
        tractName = QString::number(m_powerTestTargetTract);
    }
    return tractName.isEmpty() ? QStringLiteral("—") : tractName;
}

QString MainWindow::receiveTestTractDisplayNameForLog() const
{
    QString tractName = selectedPpmTractDisplayNameFromUi();
    if (tractName.isEmpty() && m_receiveTestTract > 0) {
        const int idx = m_ppmTractsSorted.indexOf(m_receiveTestTract);
        if (idx >= 0 && m_ppmButtonGroup) {
            if (QAbstractButton *btn = m_ppmButtonGroup->button(idx)) {
                const QString fallback = btn->text().trimmed();
                if (!fallback.isEmpty() && fallback != QStringLiteral("—")) {
                    tractName = fallback;
                }
            }
        }
    }
    if (tractName.isEmpty() && m_receiveTestTract > 0) {
        tractName = QString::number(m_receiveTestTract);
    }
    return tractName.isEmpty() ? QStringLiteral("—") : tractName;
}

namespace {
constexpr qint64 kPpmModeLaunchTimeoutMs = 30000;

// Коды IND_ERROR, при которых в пульте при включённом тракте рамка остаётся/становится TRAKT_WRK (зелёной).
bool ppmErrorCodeKeepsGreenFrameWhenPowered(int16_t code)
{
    switch (code) {
    case 0:  // ERRCODE_NOERROR
    case 1:  // ERRCODE_PPM_NOANSWER
    case 2:  // ERRCODE_RL_WRONGMODE
    case 4:  // ERRCODE_PPM_LUM_OVERHEAT
    case 5:  // ERRCODE_PPM_SWR_ERROR
    case 6:  // ERRCODE_PPM_ANT_NOTTUNED
    case 7:  // ERRCODE_PPM_NOWRK
    case 8:  // ERRCODE_PPM_NO
    case 9:  // ERRCODE_RETR_NO
        return true;
    default:
        return false;
    }
}

// Как PpmForm::leerrorCode для ERRCODE_PPM_NO в ControlPanelSurs: только красная подпись, без пауз тестов и пр.
bool isPpmErrorStatusLabelOnly(int16_t code)
{
    return code == 8; // ERRCODE_PPM_NO — «ПП не готов»
}

constexpr int TRAKT_WRK = 0;
constexpr int TRAKT_STOP_WRK = 1;
constexpr int TRAKT_WAIT_WRK = 2;
constexpr int TRAKT_ERR_WRK = 3;
constexpr int TRAKT_TX_WRK = 4;
constexpr int TRAKT_RX_WRK = 5;
constexpr int TRAKT_TOUT_RESET = 6;
constexpr int TRAKT_WAIT = 7;
constexpr int TRAKT_START_ON = 8;
constexpr int TRAKT_END_ON = 9;
constexpr int TRAKT_START_OFF = 10;
constexpr int TRAKT_END_OFF = 11;

bool isPpmFramePostReloadWaitState(int state)
{
    return state == TRAKT_END_ON || state == TRAKT_WAIT_WRK || state == TRAKT_TOUT_RESET
           || state == TRAKT_START_ON;
}
}

void MainWindow::applyPpmTransmitterLabel(const QString &statusText, PpmStatusStyle style)
{
    if (!ui || !ui->labelPPMStatus) {
        return;
    }
    ui->labelPPMStatus->setText(statusText);
    switch (style) {
    case PpmStatusStyle::Ok:
        ui->labelPPMStatus->setStyleSheet(stylesheetPPMLabelTxOk);
        break;
    case PpmStatusStyle::Warning:
        ui->labelPPMStatus->setStyleSheet(stylesheetPPMLabelTxWarning);
        break;
    case PpmStatusStyle::Fault:
    default:
        ui->labelPPMStatus->setStyleSheet(stylesheetPPMLabelTxFault);
        break;
    }
    if (ui->labelRecievePPMStatus) {
        ui->labelRecievePPMStatus->setText(ui->labelPPMStatus->text());
        ui->labelRecievePPMStatus->setStyleSheet(ui->labelPPMStatus->styleSheet());
    }
    if (ui->labelPPMStatusFHSS) {
        ui->labelPPMStatusFHSS->setText(ui->labelPPMStatus->text());
        ui->labelPPMStatusFHSS->setStyleSheet(ui->labelPPMStatus->styleSheet());
    }
}

void MainWindow::applyPpmModeFrameIdle()
{
    if (!ui || !ui->framePPMStatus) {
        return;
    }
    setPpmFrameStateForTract(-1, TRAKT_STOP_WRK);
}

void MainWindow::setPpmFrameStateForTract(int tractNum, int state)
{
    if (tractNum > 0) {
        m_ppmFrameStateByTract.insert(tractNum, state);
    }
    const int selected = selectedPpmTractFromUi();
    if (tractNum > 0 && selected > 0 && tractNum != selected) {
        return;
    }
    if (!ui || !ui->framePPMStatus) {
        return;
    }

    QString ppmStyle = stylesheetPPMFrameModeIdle;
    QString recvStyle = stylesheetRecievePPMFrameModeIdle;

    switch (state) {
    case TRAKT_ERR_WRK:
        ppmStyle = stylesheetPPMFrameModeFault;
        recvStyle = stylesheetRecievePPMFrameModeFault;
        break;
    case TRAKT_WRK:
        ppmStyle = stylesheetPPMFrameModeReady;
        recvStyle = stylesheetRecievePPMFrameModeReady;
        break;
    case TRAKT_TX_WRK:
        ppmStyle = stylesheetPPMFrameModeTx;
        recvStyle = stylesheetRecievePPMFrameModeTx;
        break;
    case TRAKT_RX_WRK:
        ppmStyle = stylesheetPPMFrameModeRx;
        recvStyle = stylesheetRecievePPMFrameModeRx;
        break;
    case TRAKT_WAIT:
        ppmStyle = stylesheetPPMFrameModeWait;
        recvStyle = stylesheetRecievePPMFrameModeWait;
        break;
    case TRAKT_WAIT_WRK:
    case TRAKT_END_ON:
    case TRAKT_START_ON:
    case TRAKT_START_OFF:
    case TRAKT_TOUT_RESET:
        ppmStyle = stylesheetPPMFrameModeWaiting;
        recvStyle = stylesheetRecievePPMFrameModeWaiting;
        break;
    case TRAKT_END_OFF:
    case TRAKT_STOP_WRK:
    default:
        ppmStyle = stylesheetPPMFrameModeIdle;
        recvStyle = stylesheetRecievePPMFrameModeIdle;
        break;
    }

    ui->framePPMStatus->setStyleSheet(ppmStyle);
    if (ui->frameRecievePPMStatus) {
        ui->frameRecievePPMStatus->setStyleSheet(recvStyle);
    }
    if (ui->framePPMStatusFHSS) {
        // stylesheetPPMFrameMode* используют селектор #framePPMStatus → подменяем под FHSS-фрейм.
        QString fhssStyle = ppmStyle;
        fhssStyle.replace(QStringLiteral("#framePPMStatus"), QStringLiteral("#framePPMStatusFHSS"));
        ui->framePPMStatusFHSS->setStyleSheet(fhssStyle);
    }

    if (tractNum > 0 && state == TRAKT_WRK) {
        maybeRestoreDefaultDirectionForTract(tractNum);
    }
}

void MainWindow::applyPpmErrorIndicationFrameLikeControlPanel(int tr, int16_t code, int16_t lastCode)
{
    const bool ppm_on = (tr == m_ppmCurrentOnTract);
    const int last_state = m_ppmFrameStateByTract.value(tr, TRAKT_STOP_WRK);

    constexpr int16_t ERRCODE_NOERROR = 0;
    constexpr int16_t ERRCODE_PPM_NOANSWER = 1;
    constexpr int16_t ERRCODE_RL_WRONGMODE = 2;
    constexpr int16_t ERRCODE_OPSES_NODATA = 3;
    constexpr int16_t ERRCODE_PPM_LUM_OVERHEAT = 4;
    constexpr int16_t ERRCODE_PPM_SWR_ERROR = 5;
    constexpr int16_t ERRCODE_PPM_ANT_NOTTUNED = 6;
    constexpr int16_t ERRCODE_PPM_NOWRK = 7;
    constexpr int16_t ERRCODE_PPM_NO = 8;
    constexpr int16_t ERRCODE_RETR_NO = 9;
    constexpr int16_t ERRCODE_PPM_START = 10;
    constexpr int16_t ERRCODE_FREQ_DKMV_MV = 11;
    constexpr int16_t kLegacyPpmStartCode = static_cast<int16_t>(0xFFFF); // как int16_t: -1

    const bool lastWasStartOrUnset = (lastCode == ERRCODE_PPM_START || lastCode == kLegacyPpmStartCode);

    // Пульт: при смене кода, ppm_on и профиле — переход на NOERROR после PPM_START/«не было кода»
    // вызывает ppmPowerStatus(TRAKT_WRK) (ветка CHMOD_SPS_A/…; на стенде считаем профиль всегда «активным»).
    // После labelUpdate/CMD_CURR_DIR_SET тракт кратко в TRAKT_END_ON (жёлтый); при «Норма» после «Авария АНТ»
    // lastCode уже не PPM_START — без ветки post-reload рамка «залипает» до повторного перезапуска.
    if (ppm_on && code == ERRCODE_NOERROR) {
        const int gateFrame = m_ppmFrameStateByTract.value(tr, TRAKT_STOP_WRK);
        if (gateFrame != TRAKT_START_OFF) {
            if (isPpmFramePostReloadWaitState(gateFrame)) {
                setPpmFrameStateForTract(tr, TRAKT_WRK);
            } else if (lastCode != code && lastWasStartOrUnset) {
                setPpmFrameStateForTract(tr, TRAKT_WRK);
            }
        }
    }

    switch (code) {
    case ERRCODE_NOERROR:
        if (!ppm_on) {
            if (last_state == TRAKT_ERR_WRK) {
                setPpmFrameStateForTract(tr, TRAKT_WAIT_WRK);
            }
        }
        break;
    case ERRCODE_PPM_NOANSWER:
        if (!ppm_on) {
            if (last_state == TRAKT_ERR_WRK) {
                setPpmFrameStateForTract(tr, TRAKT_WAIT_WRK);
            }
            break;
        }
        setPpmFrameStateForTract(tr, TRAKT_WRK);
        break;
    case ERRCODE_PPM_START:
    case kLegacyPpmStartCode:
        // В пульте только подпись/стили — ppmPowerStatus не вызывается.
        break;
    case ERRCODE_RL_WRONGMODE:
        if (!ppm_on) {
            if (last_state == TRAKT_ERR_WRK) {
                setPpmFrameStateForTract(tr, TRAKT_WAIT_WRK);
            }
            break;
        }
        setPpmFrameStateForTract(tr, TRAKT_WRK);
        break;
    case ERRCODE_OPSES_NODATA:
        if (!ppm_on) {
            setPpmFrameStateForTract(tr, TRAKT_WRK);
            break;
        }
        break;
    case ERRCODE_PPM_LUM_OVERHEAT:
    case ERRCODE_PPM_SWR_ERROR:
    case ERRCODE_PPM_ANT_NOTTUNED:
    case ERRCODE_PPM_NOWRK:
    case ERRCODE_PPM_NO:
    case ERRCODE_RETR_NO:
        if (!ppm_on) {
            if (last_state == TRAKT_ERR_WRK) {
                setPpmFrameStateForTract(tr, TRAKT_WAIT_WRK);
            }
            break;
        }
        setPpmFrameStateForTract(tr, TRAKT_WRK);
        break;
    case ERRCODE_FREQ_DKMV_MV:
        break;
    default:
        break;
    }
}

void MainWindow::setPpmUpdateLabelVisible(bool visible)
{
    Q_UNUSED(visible);
    if (!ui) {
        return;
    }
    if (ui->labelUpdate) {
        ui->labelUpdate->setVisible(true);
    }
    if (ui->labelRecieveUpdate) {
        ui->labelRecieveUpdate->setVisible(true);
    }
    if (ui->labelUpdateFHSS) {
        ui->labelUpdateFHSS->setVisible(true);
    }
}

void MainWindow::onPpmUpdateClicked()
{
    int tractNum = selectedPpmTractFromUi();
    if (tractNum <= 0) {
        tractNum = (m_ppmCurrentOnTract > 0) ? m_ppmCurrentOnTract : ppmFirstTractNumber();
    }
    if (ui && sender() == ui->labelUpdateFHSS) {
        const QString modeName =
            (ui->modeFHSSComboBox) ? ui->modeFHSSComboBox->currentText().trimmed() : QString();
        sendPpmCurrDirSet(tractNum, fhssExpectedDirIdFromModeCombo(),
                          QStringLiteral("Перезапуск направления %1").arg(
                              modeName.isEmpty() ? QStringLiteral("—") : modeName));
        return;
    }
    sendPpmCurrDirSetDir1(tractNum, QStringLiteral("Перезапуск направления ЧМ50"));
}

bool MainWindow::sendPpmCurrDirSet(int tractNum, uint8_t dirId, const QString &userLogMessage)
{
    if (!m_deviceController || !m_deviceController->isConnected()) {
        onDeviceLogMessage(QStringLiteral("ППМ: нет подключения к радиостанции, установка направления невозможна."));
        return false;
    }
    if (tractNum <= 0) {
        onDeviceLogMessage(QStringLiteral("ППМ: не выбран тракт для установки направления."));
        return false;
    }

    if (!userLogMessage.isEmpty()) {
        onDeviceLogMessage(userLogMessage);
    } else {
        DEBUG << QStringLiteral("ППМ: установка направления тракта %1 (DirId=%2).")
                     .arg(tractNum)
                     .arg(static_cast<int>(dirId));
    }
    if (!m_deviceController->setCurrentDirection(static_cast<uint8_t>(tractNum), dirId)) {
        onDeviceLogMessage(QStringLiteral("ППМ: не удалось отправить команду смены направления (DirId=%1).")
                               .arg(static_cast<int>(dirId)));
        return false;
    }
    armSelfIssuedDirOp(tractNum, dirId);
    armSelfIssuedTractReload(tractNum);
    markPpmModeLaunchStarted(tractNum);
    applyPpmModeFrameForTract(tractNum);
    m_deviceController->requestAllIndications(static_cast<uint8_t>(tractNum));
    return true;
}

bool MainWindow::sendPpmCurrDirSetDir1(int tractNum, const QString &userLogMessage)
{
    // Аналог Station_starter_3 pushButtonReset: CMD_CURR_DIR_SET (0x0501), DirId=1.
    return sendPpmCurrDirSet(tractNum, 1, userLogMessage);
}

namespace {
constexpr qint64 kSelfIssuedDirOpDeadlineMs = 8000;
constexpr qint64 kSelfIssuedTractReloadDeadlineMs = 35000;
}

qint64 MainWindow::uptimeElapsedMs() const
{
    return m_uptime.isValid() ? m_uptime.elapsed() : 0;
}

void MainWindow::initRuntimeTimerWidget()
{
    if (!ui || !ui->menubar) {
        return;
    }

    QFrame *frame = new QFrame(ui->menubar);
    frame->setObjectName(QStringLiteral("frameRuntimeMenu"));
    frame->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    frame->setMinimumSize(160, 21);
    frame->setMaximumHeight(24);
    frame->setStyleSheet(QStringLiteral(R"(
#frameRuntimeMenu {
    background-color: #131f3a;
    border: 1px solid #1e3a8a;
    border-left: 4px solid #38bdf8;
    border-radius: 4px;
    padding: 0px 6px;
    font-family: "Consolas";
}
#labelRuntimeMenu {
    color: #cbd5e1;
    margin-right: 2px;
    letter-spacing: 1px;
}
#lcdRuntimeMenu {
    background-color: #0b1220;
    color: #38bdf8;
    border: 1px solid #1f2a44;
    border-radius: 3px;
    padding: 0px 4px;
    font-family: "Consolas";
    font-weight: bold;
}
)"));

    QHBoxLayout *layout = new QHBoxLayout(frame);
    layout->setContentsMargins(6, 1, 6, 1);
    layout->setSpacing(6);

    QLabel *label = new QLabel(QStringLiteral("ВРЕМЯ РАБОТЫ"), frame);
    label->setObjectName(QStringLiteral("labelRuntimeMenu"));
    QFont labelFont = label->font();
    labelFont.setFamily(QStringLiteral("Consolas"));
    labelFont.setPointSize(9);
    labelFont.setBold(true);
    label->setFont(labelFont);
    layout->addWidget(label);

    m_runtimeLcd = new QLCDNumber(frame);
    m_runtimeLcd->setObjectName(QStringLiteral("lcdRuntimeMenu"));
    m_runtimeLcd->setFrameShape(QFrame::NoFrame);
    m_runtimeLcd->setSegmentStyle(QLCDNumber::Flat);
    m_runtimeLcd->setDigitCount(8);
    m_runtimeLcd->setMinimumSize(82, 18);
    m_runtimeLcd->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(m_runtimeLcd);

    ui->menubar->setCornerWidget(frame, Qt::TopRightCorner);
}

QString MainWindow::formatRuntimeElapsed(qint64 elapsedMs) const
{
    const qint64 totalSeconds = qMax<qint64>(0, elapsedMs / 1000);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds / 60) % 60;
    const qint64 seconds = totalSeconds % 60;

    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

void MainWindow::updateRuntimeTimerDisplay()
{
    if (!m_runtimeLcd) {
        return;
    }

    const QString elapsed = formatRuntimeElapsed(uptimeElapsedMs());
    m_runtimeLcd->setDigitCount(elapsed.size());
    m_runtimeLcd->display(elapsed);
}

uint8_t MainWindow::fhssExpectedDirIdFromModeCombo() const
{
    if (!ui || !ui->modeFHSSComboBox) {
        return 2;
    }
    const int idx = ui->modeFHSSComboBox->currentIndex();
    if (idx < 0) {
        return 2;
    }
    return static_cast<uint8_t>(2 + idx);
}

void MainWindow::armSelfIssuedDirOp(int tractNum, uint8_t expectedDirId)
{
    if (tractNum <= 0) {
        return;
    }
    const qint64 nowMs = uptimeElapsedMs();
    SelfIssuedDirOp op;
    op.expectedDirId = expectedDirId;
    op.deadlineMs = (nowMs >= 0) ? (nowMs + kSelfIssuedDirOpDeadlineMs) : kSelfIssuedDirOpDeadlineMs;
    m_selfIssuedDirOpByTract.insert(tractNum, op);
}

void MainWindow::armSelfIssuedTractReload(int tractNum)
{
    if (tractNum <= 0) {
        return;
    }
    const qint64 nowMs = uptimeElapsedMs();
    m_selfIssuedTractReloadUntilMsByTract.insert(
        tractNum, (nowMs >= 0) ? (nowMs + kSelfIssuedTractReloadDeadlineMs) : kSelfIssuedTractReloadDeadlineMs);
}

void MainWindow::clearSelfIssuedGuardsForTract(int tractNum)
{
    m_selfIssuedDirOpByTract.remove(tractNum);
    m_selfIssuedTractReloadUntilMsByTract.remove(tractNum);
}

void MainWindow::clearAllSelfIssuedGuards()
{
    m_selfIssuedDirOpByTract.clear();
    m_selfIssuedTractReloadUntilMsByTract.clear();
}

void MainWindow::pauseFhssForPpmDisconnect()
{
    ++m_fhssResumeAfterPpmSerial;
    if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
        DEBUG << QStringLiteral("⏸ ППРЧ: Нет связи с ПП — остановка RTP/генератора трафика (пауза теста).");
        m_powerTrafficGenerator->stop();
    }
    if (ui && ui->emissionAntennaWidgetFHSS) {
        ui->emissionAntennaWidgetFHSS->stopTransmission();
    }
    updateTabWidgetLockState();
    setFhssTestControlsRunning(true);
}

void MainWindow::pauseFhssForAnalyzerDisconnect()
{
    if (!(m_fhssRunning || m_fhssDirSwitchPending || m_fhssBlockedByPpm || m_fhssBlockedByAnalyzerDisconnect
          || m_fhssBlockedByAntFault || m_fhssBlockedByDirRestore || m_fhssReturnToDefaultDirPending
          || (ui && ui->pushButtonFHSSTestStop && ui->pushButtonFHSSTestStop->isVisible()))) {
        return;
    }

    if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
        DEBUG << QStringLiteral("⏸ ППРЧ: потеря связи с анализатором — остановка RTP/генератора трафика.");
        m_powerTrafficGenerator->stop();
    }
    if (ui && ui->emissionAntennaWidgetFHSS) {
        ui->emissionAntennaWidgetFHSS->stopTransmission();
    }
    ++m_fhssResumeAfterPpmSerial;
    m_fhssBlockedByAnalyzerDisconnect = true;
    if (ui && ui->pushButtonStartTestingFHSS) {
        ui->pushButtonStartTestingFHSS->setVisible(false);
    }
    if (ui && ui->pushButtonFHSSTestStop) {
        ui->pushButtonFHSSTestStop->setVisible(true);
        ui->pushButtonFHSSTestStop->setEnabled(false);
    }
    updateTabWidgetLockState();
    setFhssTestControlsRunning(true);
}

void MainWindow::pauseFhssForAntennaFault()
{
    ++m_fhssResumeAfterPpmSerial;
    if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
        DEBUG << QStringLiteral("⏸ ППРЧ: Авария АНТ — остановка RTP/генератора трафика (пауза теста).");
        m_powerTrafficGenerator->stop();
    }
    if (ui && ui->emissionAntennaWidgetFHSS) {
        ui->emissionAntennaWidgetFHSS->stopTransmission();
    }
    updateTabWidgetLockState();
    setFhssTestControlsRunning(true);
}

void MainWindow::pauseFhssForExternalDirectionRestore()
{
    ++m_fhssResumeAfterPpmSerial;
    if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
        DEBUG << QStringLiteral("⏹ ППРЧ: внешнее переключение направления — остановка RTP/генератора трафика.");
        m_powerTrafficGenerator->stop();
    }
    m_fhssRunning = false;
    m_fhssBlockedByDirRestore = true;
    m_fhssDirSwitchPending = true;
    if (ui && ui->pushButtonFHSSTestStop) {
        ui->pushButtonFHSSTestStop->setEnabled(false);
    }
    setFhssTestControlsRunning(true);
    DEBUG << QStringLiteral("⏸ ППРЧ: тест на паузе (внешняя смена направления).");
    updateFhssTestButtonsAccessForSelectedTract();
    updateTabWidgetLockState();
}

void MainWindow::beginFhssResumeDirectionCommand(uint8_t dirId)
{
    if (m_fhssTract <= 0 || !m_deviceController || !m_deviceController->isConnected()) {
        return;
    }
    const int tr = m_fhssTract;
    armSelfIssuedDirOp(tr, dirId);
    armSelfIssuedTractReload(tr);
    DEBUG << QStringLiteral("ППРЧ: восстановление направления DirId=%1 (тракт %2)...")
                 .arg(static_cast<int>(dirId))
                 .arg(tr);
    if (!m_deviceController->setCurrentDirection(static_cast<uint8_t>(tr), dirId)) {
        DEBUG << QStringLiteral("ППРЧ: не удалось отправить CMD_CURR_DIR_SET DirId=%1 (тракт %2).")
                     .arg(static_cast<int>(dirId))
                     .arg(tr);
        return;
    }
    m_deviceController->requestAllIndications(static_cast<uint8_t>(tr));
}

void MainWindow::tryFinishFhssReturnToDefaultDirection(int tractNum)
{
    if (!m_fhssReturnToDefaultDirPending || tractNum != m_fhssReturnToDefaultDirTract) {
        return;
    }
    if (m_selfIssuedDirOpByTract.contains(tractNum)) {
        return;
    }
    if (m_ppmLastDirIdByTract.value(tractNum, 0) != 1) {
        return;
    }
    if (m_ppmLastWorkModeByTract.value(tractNum, 0) == 0) {
        return;
    }

    m_fhssReturnToDefaultDirPending = false;
    m_fhssReturnToDefaultDirTract = -1;
    clearSelfIssuedGuardsForTract(tractNum);
    DEBUG << QStringLiteral("ППРЧ: возврат на DirId=1 завершён (тракт %1).").arg(tractNum);

    if (ui && ui->modeFHSSComboBox) {
        ui->modeFHSSComboBox->setEnabled(true);
    }
    updateFhssTestButtonsAccessForSelectedTract();
    updateTabWidgetLockState();
}

void MainWindow::markPpmModeLaunchStarted(int tractNum)
{
    if (tractNum <= 0) {
        return;
    }
    m_ppmModeLaunchPendingByTract.insert(tractNum, true);
    m_ppmModeLaunchTimedOutByTract.remove(tractNum);
    m_ppmModeLaunchSinceMsByTract.insert(tractNum, QDateTime::currentMSecsSinceEpoch());
}

void MainWindow::clearPpmModeLaunchStateForTract(int tractNum)
{
    m_ppmModeLaunchPendingByTract.remove(tractNum);
    m_ppmModeLaunchTimedOutByTract.remove(tractNum);
    m_ppmModeLaunchSinceMsByTract.remove(tractNum);
}

void MainWindow::ensurePpmModeLaunchDeadlineSeeded(int tractNum)
{
    if (tractNum <= 0 || m_ppmModeLaunchSinceMsByTract.contains(tractNum)) {
        return;
    }
    m_ppmModeLaunchSinceMsByTract.insert(tractNum, QDateTime::currentMSecsSinceEpoch());
    m_ppmModeLaunchPendingByTract.insert(tractNum, true);
}

void MainWindow::refreshPpmModeLaunchTimeoutEval(int tractNum)
{
    if (tractNum <= 0 || m_ppmModeLaunchTimedOutByTract.value(tractNum, false)) {
        return;
    }
    const bool hasMode = m_ppmLastWorkModeByTract.contains(tractNum);
    const uint16_t mode = hasMode ? m_ppmLastWorkModeByTract.value(tractNum) : 0;
    if (hasMode && mode != 0) {
        return;
    }
    if (!m_ppmModeLaunchSinceMsByTract.contains(tractNum)) {
        return;
    }
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_ppmModeLaunchSinceMsByTract.value(tractNum);
    if (elapsed >= kPpmModeLaunchTimeoutMs) {
        m_ppmModeLaunchTimedOutByTract.insert(tractNum, true);
    }
}

void MainWindow::applyPpmModeFrameForTract(int tractNum)
{
    if (!ui || !ui->framePPMStatus) {
        return;
    }
    if (tractNum <= 0) {
        setPpmFrameStateForTract(-1, TRAKT_STOP_WRK);
        updatePowerTestButtonsAccessForSelectedTract();
        updateReceiveTestButtonsAccessForSelectedTract();
        return;
    }

    const int state = m_ppmFrameStateByTract.value(tractNum, TRAKT_STOP_WRK);
    setPpmFrameStateForTract(tractNum, state);

    updatePowerTestButtonsAccessForSelectedTract();
    updateReceiveTestButtonsAccessForSelectedTract();
}

void MainWindow::maybeRestoreDefaultDirectionForTract(int tractNum)
{
    if (tractNum <= 0 || !m_deviceController || !m_deviceController->isConnected()) {
        return;
    }
    if (tractNum != m_ppmCurrentOnTract) {
        return;
    }
    if (!m_ppmRestoreDefaultDirPendingByTract.value(tractNum, false)) {
        return;
    }
    if (m_ppmRestoreDefaultDirInFlightByTract.value(tractNum, false)) {
        return;
    }
    const uint8_t dirId = m_ppmLastDirIdByTract.value(tractNum, 1);
    if (dirId == 1) {
        m_ppmRestoreDefaultDirPendingByTract.insert(tractNum, false);
        m_ppmRestoreDefaultDirInFlightByTract.insert(tractNum, false);
        return;
    }
    DEBUG << QStringLiteral("ППМ: внешняя смена направления на тракте %1 (DirId=%2), возвращаю DirId=1.")
                 .arg(tractNum)
                 .arg(static_cast<int>(dirId));
    if (m_deviceController->setCurrentDirection(static_cast<uint8_t>(tractNum), 1)) {
        armSelfIssuedDirOp(tractNum, 1);
        armSelfIssuedTractReload(tractNum);
        m_ppmRestoreDefaultDirInFlightByTract.insert(tractNum, true);
    } else {
        DEBUG << QStringLiteral("ППМ: не удалось отправить команду возврата направления DirId=1 (тракт %1).")
                     .arg(tractNum);
    }
}

void MainWindow::refreshPpmStatusUiForTract(int tractNum)
{
    constexpr int ERRCODE_NOERROR = 0;
    constexpr int ERRCODE_PPM_LUM_OVERHEAT = 4;
    constexpr int ERRCODE_PPM_START = 10;
    constexpr int16_t ERRCODE_PPM_START_LEGACY = static_cast<int16_t>(0xFFFF);

    if (tractNum <= 0 || !m_ppmLastStatusCodeByTract.contains(tractNum)) {
        applyPpmTransmitterLabel(QStringLiteral("—"), PpmStatusStyle::Fault);
        setPpmUpdateLabelVisible(true);
        applyPpmModeFrameForTract(tractNum);
        updatePowerTestButtonsAccessForSelectedTract();
        updateReceiveTestButtonsAccessForSelectedTract();
        updateFhssTestButtonsAccessForSelectedTract();
        return;
    }

    const int16_t code = m_ppmLastStatusCodeByTract.value(tractNum);
    const QString text = ppmErrorCodeToText(code);
    if (text.isEmpty()) {
        applyPpmTransmitterLabel(QStringLiteral("—"), PpmStatusStyle::Fault);
        setPpmUpdateLabelVisible(true);
        applyPpmModeFrameForTract(tractNum);
        updatePowerTestButtonsAccessForSelectedTract();
        updateReceiveTestButtonsAccessForSelectedTract();
        updateFhssTestButtonsAccessForSelectedTract();
        return;
    }

    setPpmUpdateLabelVisible(true);

    if (code == ERRCODE_NOERROR) {
        applyPpmTransmitterLabel(text, PpmStatusStyle::Ok);
    } else if (code == ERRCODE_PPM_LUM_OVERHEAT || code == ERRCODE_PPM_START || code == ERRCODE_PPM_START_LEGACY) {
        applyPpmTransmitterLabel(text, PpmStatusStyle::Warning);
    } else {
        // В т.ч. ERRCODE_PPM_NO («ПП не готов») → labelPPMStatus / labelRecievePPMStatus / labelPPMStatusFHSS.
        applyPpmTransmitterLabel(text, PpmStatusStyle::Fault);
    }
    applyPpmModeFrameForTract(tractNum);
    updatePowerTestButtonsAccessForSelectedTract();
    updateReceiveTestButtonsAccessForSelectedTract();
    updateFhssTestButtonsAccessForSelectedTract();
}

void MainWindow::pausePowerTestForPpmDisconnect()
{
    // Отменяем любой ранее запланированный auto-resume после "Норма".
    ++m_powerResumeAfterPpmSerial;
    m_powerTestBlockedByStationDisconnect = false;

    // Останавливаем таймеры/излучение/генератор, но НЕ сбрасываем последовательность и индекс —
    // чтобы можно было продолжить с той же частоты.
    m_powerTestAutoStopTimer.stop();
    m_powerTestStepPauseTimer.stop();
    m_powerTestBeforePowerOnTimer.stop();
    m_powerMeasurementRunning = false;
    m_powerTrafficStartPending = false;
    setEmissionAnimating(false);

    // Важно: если связь с ПП пропала во время окна измерения, могли успеть накопиться частичные данные шага
    // (лучший bin/амплитуда, аккумуляторы). Их нужно обнулить, чтобы после восстановления "Норма"
    // эта же частота была измерена заново, а не завершилась старыми значениями.
    m_powerStepAmpAccumDbm = 0.0;
    m_powerStepAmpSampleCount = 0;
    m_powerStepBestValid = false;
    m_powerStepBestFreqMHz = 0.0;
    m_powerStepBestAmpDbm = -200.0;

    if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
        if (debug) {
            onDeviceLogMessage(QStringLiteral("⏹ ППМ: Нет связи с ПП — остановка RTP/генератора трафика."));
        }
        m_powerTrafficGenerator->stop();
    }

    m_powerTestPaused = true;

    // Снимаем checked без вызова onPowerTestingToggled(false), чтобы не сбросить прогресс.
    if (ui && ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked()) {
        QSignalBlocker blocker(ui->pushButtonStartTestingPower);
        ui->pushButtonStartTestingPower->setChecked(false);
    }
    if (ui && ui->pushButtonStartTestingPower) {
        ui->pushButtonStartTestingPower->setText(QStringLiteral("НАЧАТЬ ТЕСТ МОЩНОСТИ"));
    }
    updateTabWidgetLockState();

    // UI: переводим контролы теста мощности в "paused" (иконка play на кнопке паузы),
    // чтобы было видно, что тест остановлен внешней причиной и может быть продолжен.
    setPowerTestControlsRunning(true);
}

void MainWindow::pausePowerTestForStationDisconnect()
{
    ++m_powerResumeAfterPpmSerial;
    m_powerTestBlockedByStationDisconnect = true;

    m_powerTestAutoStopTimer.stop();
    m_powerTestStepPauseTimer.stop();
    m_powerTestBeforePowerOnTimer.stop();
    m_powerMeasurementRunning = false;
    m_powerTrafficStartPending = false;
    setEmissionAnimating(false);

    m_powerStepAmpAccumDbm = 0.0;
    m_powerStepAmpSampleCount = 0;
    m_powerStepBestValid = false;
    m_powerStepBestFreqMHz = 0.0;
    m_powerStepBestAmpDbm = -200.0;

    if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
        if (debug) {
            onDeviceLogMessage(QStringLiteral("⏹ ППМ: потеря связи с радиостанцией — остановка RTP/генератора трафика."));
        }
        m_powerTrafficGenerator->stop();
    }

    m_powerTestPaused = true;

    if (ui && ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked()) {
        QSignalBlocker blocker(ui->pushButtonStartTestingPower);
        ui->pushButtonStartTestingPower->setChecked(false);
    }
    if (ui && ui->pushButtonStartTestingPower) {
        ui->pushButtonStartTestingPower->setText(QStringLiteral("НАЧАТЬ ТЕСТ МОЩНОСТИ"));
    }
    updateTabWidgetLockState();

    // Ключевой момент: при обрыве связи держим pause/stop видимыми, start скрытой.
    setPowerTestControlsRunning(true);
}

void MainWindow::pausePowerTestForAnalyzerDisconnect()
{
    ++m_powerResumeAfterPpmSerial;
    m_powerTestBlockedByAnalyzerDisconnect = true;

    m_powerTestAutoStopTimer.stop();
    m_powerTestStepPauseTimer.stop();
    m_powerTestBeforePowerOnTimer.stop();
    m_powerMeasurementRunning = false;
    m_powerTrafficStartPending = false;
    setEmissionAnimating(false);

    m_powerStepAmpAccumDbm = 0.0;
    m_powerStepAmpSampleCount = 0;
    m_powerStepBestValid = false;
    m_powerStepBestFreqMHz = 0.0;
    m_powerStepBestAmpDbm = -200.0;

    if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
        if (debug) {
            onDeviceLogMessage(
                QStringLiteral("⏹ ППМ: потеря связи с анализатором — остановка RTP/генератора трафика."));
        }
        m_powerTrafficGenerator->stop();
    }

    m_powerTestPaused = true;

    if (ui && ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked()) {
        QSignalBlocker blocker(ui->pushButtonStartTestingPower);
        ui->pushButtonStartTestingPower->setChecked(false);
    }
    if (ui && ui->pushButtonStartTestingPower) {
        ui->pushButtonStartTestingPower->setText(QStringLiteral("НАЧАТЬ ТЕСТ МОЩНОСТИ"));
    }
    updateTabWidgetLockState();
    setPowerTestControlsRunning(true);
}

void MainWindow::pausePowerTestForAntennaFault()
{
    // Отменяем любой ранее запланированный auto-resume после "Норма".
    ++m_powerResumeAfterPpmSerial;
    m_powerTestBlockedByStationDisconnect = false;

    // Останавливаем таймеры/излучение/генератор, но НЕ сбрасываем последовательность и индекс —
    // чтобы можно было продолжить с той же частоты.
    m_powerTestAutoStopTimer.stop();
    m_powerTestStepPauseTimer.stop();
    m_powerTestBeforePowerOnTimer.stop();
    m_powerMeasurementRunning = false;
    m_powerTrafficStartPending = false;
    setEmissionAnimating(false);

    // Аналогично разрыву связи: частичные данные шага сбрасываем, чтобы после восстановления "Норма"
    // эта же частота была измерена заново.
    m_powerStepAmpAccumDbm = 0.0;
    m_powerStepAmpSampleCount = 0;
    m_powerStepBestValid = false;
    m_powerStepBestFreqMHz = 0.0;
    m_powerStepBestAmpDbm = -200.0;

    if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
        if (debug) {
            onDeviceLogMessage(QStringLiteral("⏹ ППМ: Авария АНТ — остановка RTP/генератора трафика."));
        }
        m_powerTrafficGenerator->stop();
    }

    m_powerTestPaused = true;

    // Снимаем checked без вызова onPowerTestingToggled(false), чтобы не сбросить прогресс.
    if (ui && ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked()) {
        QSignalBlocker blocker(ui->pushButtonStartTestingPower);
        ui->pushButtonStartTestingPower->setChecked(false);
    }
    if (ui && ui->pushButtonStartTestingPower) {
        ui->pushButtonStartTestingPower->setText(QStringLiteral("НАЧАТЬ ТЕСТ МОЩНОСТИ"));
    }
    updateTabWidgetLockState();

    // UI: paused (иконка play) — внешний фактор, можно продолжить после "Норма".
    setPowerTestControlsRunning(true);
}

void MainWindow::reloadDirectionAfterAntennaFault(int tractNum)
{
    if (tractNum <= 0) {
        return;
    }

    const bool isFhssTarget =
        (m_fhssTract > 0 && tractNum == m_fhssTract && m_fhssBlockedByAntFault);
    const bool isPowerTarget =
        (m_powerTestTargetTract != 0U && tractNum == static_cast<int>(m_powerTestTargetTract)
         && m_powerTestBlockedByAntFault);

    if (isFhssTarget) {
        const QString modeName =
            (ui && ui->modeFHSSComboBox) ? ui->modeFHSSComboBox->currentText().trimmed() : QString();
        sendPpmCurrDirSet(tractNum, fhssExpectedDirIdFromModeCombo(),
                          QStringLiteral("Авария АНТ: перезапуск направления %1").arg(
                              modeName.isEmpty() ? QStringLiteral("—") : modeName));
        return;
    }

    if (isPowerTarget) {
        sendPpmCurrDirSetDir1(tractNum, QStringLiteral("Авария АНТ: перезапуск направления ЧМ50"));
    }
}

void MainWindow::pausePowerTestForDirectionRestore()
{
    ++m_powerResumeAfterPpmSerial;
    m_powerTestBlockedByStationDisconnect = false;

    m_powerTestAutoStopTimer.stop();
    m_powerTestStepPauseTimer.stop();
    m_powerTestBeforePowerOnTimer.stop();
    m_powerMeasurementRunning = false;
    m_powerTrafficStartPending = false;
    setEmissionAnimating(false);

    m_powerStepAmpAccumDbm = 0.0;
    m_powerStepAmpSampleCount = 0;
    m_powerStepBestValid = false;
    m_powerStepBestFreqMHz = 0.0;
    m_powerStepBestAmpDbm = -200.0;

    if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
        if (debug) {
            onDeviceLogMessage(
                QStringLiteral("⏹ ППМ: внешнее переключение направления — остановка RTP/генератора трафика."));
        }
        m_powerTrafficGenerator->stop();
    }

    m_powerTestPaused = true;

    if (ui && ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked()) {
        QSignalBlocker blocker(ui->pushButtonStartTestingPower);
        ui->pushButtonStartTestingPower->setChecked(false);
    }
    if (ui && ui->pushButtonStartTestingPower) {
        ui->pushButtonStartTestingPower->setText(QStringLiteral("НАЧАТЬ ТЕСТ МОЩНОСТИ"));
    }
    updateTabWidgetLockState();

    DEBUG << QStringLiteral("⏸ ППМ: тест мощности на паузе (внешняя смена направления).");
    setPowerTestControlsRunning(true);
}

void MainWindow::syncPpmFrameForDir1IfTransmitterOk(int tractNum,
                                                     bool requireNonZeroWorkMode,
                                                     bool requireDir1)
{
    if (tractNum <= 0 || tractNum != m_ppmCurrentOnTract) {
        return;
    }
    if (requireDir1 && m_ppmLastDirIdByTract.value(tractNum, 1) != 1) {
        return;
    }
    if (requireNonZeroWorkMode && m_ppmLastWorkModeByTract.value(tractNum, 0) == 0) {
        return;
    }
    if (!m_ppmLastStatusCodeByTract.contains(tractNum)) {
        return;
    }
    const int16_t c = m_ppmLastStatusCodeByTract.value(tractNum);
    // Как в пульте при ppm_on: при этих кодах рамка остаётся/становится рабочей (зелёной),
    // в т.ч. «Авария АНТ» — иначе после перезагрузки по labelUpdate остаётся TRAKT_END_ON.
    if (ppmErrorCodeKeepsGreenFrameWhenPowered(c)) {
        setPpmFrameStateForTract(tractNum, TRAKT_WRK);
        updatePowerTestButtonsAccessForSelectedTract();
        updateReceiveTestButtonsAccessForSelectedTract();
    }
}

void MainWindow::attemptScheduleDelayedPowerTestResume(int tr)
{
    const bool isPowerTargetTract = (m_powerTestTargetTract != 0U && tr == static_cast<int>(m_powerTestTargetTract));
    const bool isOnPowerTab =
        (ui && ui->tabWidget && m_tabPowerIndex >= 0 && ui->tabWidget->currentIndex() == m_tabPowerIndex);
    const bool canResumeSequence =
        (m_powerTestSequenceIndex >= 0 && m_powerTestSequenceIndex < m_powerTestSequenceFreqsHz.size()
         && !m_powerTestSequenceFreqsHz.isEmpty());

    if (!isPowerTargetTract || !isOnPowerTab || !m_powerTestPaused || !canResumeSequence || !ui
        || !ui->pushButtonStartTestingPower || ui->pushButtonStartTestingPower->isChecked()) {
        return;
    }
    if (m_powerTestBlockedByPpm || m_powerTestBlockedByAntFault) {
        return;
    }
    if (m_powerTestBlockedByAnalyzerDisconnect || !m_analyzerConnected) {
        return;
    }
    if (m_powerTestBlockedByDirRestore) {
        if (m_ppmLastDirIdByTract.value(tr, 1) != 1) {
            return;
        }
    }
    if (selectedPpmTractFromUi() != tr || !isPpmTractReadyForPowerTest(tr)) {
        return;
    }

    m_powerTestBlockedByDirRestore = false;
    constexpr int kResumeDelayMs = 5000;
    const quint64 serial = ++m_powerResumeAfterPpmSerial;

    setPowerTestControlsRunning(true);

    QTimer::singleShot(kResumeDelayMs, this, [this, serial, tr]() {
        if (serial != m_powerResumeAfterPpmSerial) {
            return;
        }
        if (!ui || !ui->tabWidget || m_tabPowerIndex < 0 || ui->tabWidget->currentIndex() != m_tabPowerIndex) {
            return;
        }
        if (m_powerTestTargetTract == 0U || tr != static_cast<int>(m_powerTestTargetTract)) {
            return;
        }
        if (selectedPpmTractFromUi() != tr || !isPpmTractReadyForPowerTest(tr)) {
            updatePowerTestButtonsAccessForSelectedTract();
            return;
        }
        if (m_powerTestBlockedByPpm || m_powerTestBlockedByAntFault || m_powerTestBlockedByDirRestore
            || m_powerTestBlockedByAnalyzerDisconnect || !m_analyzerConnected || !m_powerTestPaused) {
            return;
        }
        if (m_powerTestSequenceFreqsHz.isEmpty() || m_powerTestSequenceIndex < 0
            || m_powerTestSequenceIndex >= m_powerTestSequenceFreqsHz.size()) {
            return;
        }
        if (!ui->pushButtonStartTestingPower || ui->pushButtonStartTestingPower->isChecked()) {
            return;
        }

        setPowerTestControlsRunning(false);
        ui->pushButtonStartTestingPower->setChecked(true);
    });
}

bool MainWindow::isPpmTractReadyForPowerTest(int tractNum) const
{
    if (tractNum <= 0) {
        return false;
    }

    // 1) По ТЗ: старт/продолжение теста мощности разрешены только для IND_ERROR="Норма" или "Перегрев ЛУМ".
    if (!m_ppmLastStatusCodeByTract.contains(tractNum)) {
        return false;
    }
    const int16_t code = m_ppmLastStatusCodeByTract.value(tractNum);
    const QString statusText = ppmErrorCodeToText(code);
    if (statusText != QStringLiteral("Норма") && statusText != QStringLiteral("Перегрев ЛУМ")) {
        return false;
    }

    // 2) По ТЗ: рамка framePPMStatus должна быть зелёной (TRAKT_WRK).
    const bool powered = (tractNum == m_ppmCurrentOnTract);
    if (!powered) {
        return false;
    }
    if (m_ppmFrameStateByTract.value(tractNum, TRAKT_STOP_WRK) != TRAKT_WRK) {
        return false;
    }

    return true;
}

void MainWindow::updatePowerTestButtonsAccessForSelectedTract()
{
    if (!ui) {
        return;
    }
    // Строго учитываем текущий тракт, выбранный в framePPM.
    const int selected = selectedPpmTractFromUi();
    const bool connected = (m_deviceController && m_deviceController->isConnected());
    const bool allow = connected && m_analyzerConnected && isPpmTractReadyForPowerTest(selected);
    const bool hasPausedPowerTestSession =
        m_powerTestPaused
        || (m_powerTestSequenceIndex >= 0 && !m_powerTestSequenceFreqsHz.isEmpty());
    const bool isPowerTestTargetTractSelected =
        (m_powerTestTargetTract == 0U) || (selected == static_cast<int>(m_powerTestTargetTract));
    // При «Авария АНТ» пауза остаётся заблокированной, но «Стоп» должен быть доступен.
    const bool allowStopDespiteAntFault =
        connected && m_powerTestBlockedByAntFault && hasPausedPowerTestSession
        && isPowerTestTargetTractSelected;
    const bool allowStop = allow || allowStopDespiteAntFault;

    if (ui->pushButtonStartTestingPower) {
        ui->pushButtonStartTestingPower->setEnabled(allow);
    }
    if (ui->pushButtonPowerTestPause) {
        ui->pushButtonPowerTestPause->setEnabled(allow);
    }
    if (ui->pushButtonPowerTestStop) {
        ui->pushButtonPowerTestStop->setEnabled(allowStop);
    }
    updatePowerLevelRadioButtonsEnabled();
}

void MainWindow::updateReceiveTestButtonsAccessForSelectedTract()
{
    if (!ui) {
        return;
    }
    // Строго учитываем текущий тракт, выбранный в framePPM.
    const int selected = selectedPpmTractFromUi();
    const bool connected = (m_deviceController && m_deviceController->isConnected());
    const bool allow = connected && m_analyzerConnected && isPpmTractReadyForPowerTest(selected);

    if (ui->pushButtonStartTestingRecieve) {
        ui->pushButtonStartTestingRecieve->setEnabled(allow);
    }
    if (ui->pushButtonRecieveTestPause) {
        ui->pushButtonRecieveTestPause->setEnabled(allow);
    }
    if (ui->pushButtonRecieveTestStop) {
        ui->pushButtonRecieveTestStop->setEnabled(allow);
    }
}

void MainWindow::stopReceiveTestIfTractNotReady(int tractNum)
{
    // Совместимость: оставляем имя метода, но поведение делаем как "пауза", а не полный stop/reset.
    pauseReceiveTestForPpmNotReady(tractNum);
}

void MainWindow::pauseReceiveTestForPpmNotReady(int tractNum)
{
    if (!ui || !m_receiveTestRunning) {
        return;
    }
    if (tractNum <= 0 || m_receiveTestTract <= 0) {
        return;
    }
    if (m_receiveTestTract != tractNum) {
        return; // защита от статусов/режимов чужого тракта
    }
    if (m_ppmLastStatusCodeByTract.contains(tractNum)
        && isPpmErrorStatusLabelOnly(m_ppmLastStatusCodeByTract.value(tractNum))) {
        return; // «ПП не готов» — только подпись, тест приёма не трогаем
    }

    // Если тракт снова готов — при авто-паузе автоматически продолжаем.
    if (isPpmTractReadyForPowerTest(tractNum) && m_analyzerConnected) {
        updateReceiveTestButtonsAccessForSelectedTract();

        if (m_receiveTestPaused && m_receiveTestAutoPausedByPpmNotReady) {
            m_receiveTestPaused = false;
            m_receiveTestAutoPausedByPpmNotReady = false;
            m_receiveTestAutoPausedByAnalyzerDisconnect = false;
            setReceiveTestControlsRunning(false); // иконка pause

            resumeReceiveLevelTestAfterPause();
            updateReceiveResultStripsVisibility();
            DEBUG << QStringLiteral("▶ Тест приёма продолжен: тракт %1 снова готов (текущий уровень перезапущен).")
                         .arg(tractNum);
        }
        updateTabWidgetLockState();
        return;
    }

    // Неготов: переводим тест в paused. Текущий уровень сразу сбрасывается; при возобновлении стартует заново.
    if (!m_receiveTestPaused) {
        m_receiveTestPaused = true;
        m_receiveTestAutoPausedByPpmNotReady = true;
        m_receiveTestTickTimer.stop();
        if (m_analyzerController) {
            // Вне зависимости от фазы безопасно выключаем генератор.
            m_analyzerController->setGenerator(m_receiveTestFreqHz, /*state*/ 0, m_receiveTestPow);
        }
        setEmissionAnimating(false);
        restartInterruptedReceiveLevelTest();
        setReceiveTestControlsRunning(true); // иконка play
        updateReceiveResultStripsVisibility();
        DEBUG << QStringLiteral("⏸ Тест приёма на паузе: тракт %1 не готов (статус/режим).").arg(tractNum);
    }

    updateReceiveTestButtonsAccessForSelectedTract();
    updateTabWidgetLockState();
}

void MainWindow::pauseReceiveTestForStationDisconnect()
{
    ++m_receiveResumeAfterReconnectSerial;
    if (!ui || !m_receiveTestRunning) {
        return;
    }

    if (!m_receiveTestPaused) {
        m_receiveTestPaused = true;
        m_receiveTestAutoPausedByPpmNotReady = true;
        m_receiveTestTickTimer.stop();
        if (m_analyzerController) {
            m_analyzerController->setGenerator(m_receiveTestFreqHz, /*state*/ 0, m_receiveTestPow);
        }
        setEmissionAnimating(false);
        restartInterruptedReceiveLevelTest();
        setReceiveTestControlsRunning(true); // иконка play
        updateReceiveResultStripsVisibility();
        DEBUG << QStringLiteral("⏸ Тест приёма на паузе: потеря связи с радиостанцией.");
    }

    updateReceiveTestButtonsAccessForSelectedTract();
    updateTabWidgetLockState();
}

void MainWindow::pauseReceiveTestForAnalyzerDisconnect()
{
    ++m_receiveResumeAfterReconnectSerial;
    if (!ui || !m_receiveTestRunning) {
        return;
    }

    if (!m_receiveTestPaused) {
        m_receiveTestPaused = true;
        m_receiveTestAutoPausedByPpmNotReady = true;
        m_receiveTestAutoPausedByAnalyzerDisconnect = true;
        m_receiveTestTickTimer.stop();
        if (m_analyzerController) {
            m_analyzerController->setGenerator(m_receiveTestFreqHz, /*state*/ 0, m_receiveTestPow);
        }
        setEmissionAnimating(false);
        restartInterruptedReceiveLevelTest();
        setReceiveTestControlsRunning(true);
        updateReceiveResultStripsVisibility();
        DEBUG << QStringLiteral("⏸ Тест приёма на паузе: потеря связи с анализатором.");
    } else if (!m_receiveTestAutoPausedByAnalyzerDisconnect) {
        m_receiveTestAutoPausedByAnalyzerDisconnect = true;
    }

    updateReceiveTestButtonsAccessForSelectedTract();
    updateTabWidgetLockState();
}

void MainWindow::attemptScheduleDelayedReceiveTestResume(int tractNum)
{
    if (tractNum <= 0) {
        return;
    }
    const quint64 serial = ++m_receiveResumeAfterReconnectSerial;
    constexpr int kResumeDelayMs = 1200;

    auto tryResume = [this, tractNum](quint64 expectedSerial) {
        if (expectedSerial != m_receiveResumeAfterReconnectSerial) {
            return false;
        }
        if (!m_receiveTestRunning || !m_receiveTestPaused || !m_receiveTestAutoPausedByPpmNotReady) {
            return true;
        }
        if (!m_analyzerConnected) {
            return false;
        }
        if (!m_deviceController || !m_deviceController->isConnected()) {
            return false;
        }
        pauseReceiveTestForPpmNotReady(tractNum);
        return !(m_receiveTestRunning && m_receiveTestPaused && m_receiveTestAutoPausedByPpmNotReady);
    };

    QTimer::singleShot(kResumeDelayMs, this, [this, serial, tractNum, tryResume]() {
        if (tryResume(serial)) {
            return;
        }
        // Вторая попытка после повторного запроса индикаций для тракта.
        if (m_deviceController && m_deviceController->isConnected() && tractNum > 0 && tractNum <= 255) {
            m_deviceController->requestAllIndications(static_cast<uint8_t>(tractNum));
        }
        const quint64 serial2 = ++m_receiveResumeAfterReconnectSerial;
        QTimer::singleShot(kResumeDelayMs, this, [this, serial2, tryResume]() {
            tryResume(serial2);
        });
    });
}

void MainWindow::resetPowerTestUiForNewTractSelection(int targetTract)
{
    if (!ui) {
        return;
    }

    // При смене тракта UI теста мощности всегда должен вернуться в исходное состояние,
    // даже если тест был "поставлен на паузу" без checked (например, из-за "Нет связи с ПП").
    ++m_powerResumeAfterPpmSerial; // отменяем возможный отложенный auto-resume
    // Если тест реально запущен — останавливаем штатно (через onPowerTestingToggled(false)),
    // чтобы гарантированно остановить таймеры/стрим/генератор.
    if (ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked()) {
        ui->pushButtonStartTestingPower->setChecked(false);
    }
    setPowerTestControlsIdle();
    if (ui->pushButtonStartTestingPower) {
        ui->pushButtonStartTestingPower->setText(QStringLiteral("НАЧАТЬ ТЕСТ МОЩНОСТИ"));
        if (ui->pushButtonStartTestingPower->isChecked()) {
            QSignalBlocker blocker(ui->pushButtonStartTestingPower);
            ui->pushButtonStartTestingPower->setChecked(false);
        }
    }

    // Сбросим признаки "паузы/продолжения" от предыдущего тракта.
    m_powerTestPaused = false;
    m_powerTestBlockedByPpm = false;
    m_powerTestBlockedByStationDisconnect = false;
    m_powerTestBlockedByAnalyzerDisconnect = false;
    m_powerTestBlockedByDirRestore = false;
    m_powerTestTargetTract = 0U;
    m_powerTestTargetTrmType = -1;
    m_powerTestCurrentFreqRetryCount = 0;
    m_powerTestCurrentFreqHz = 0;
    // При переключении тракта на power-вкладке хотим видеть моментный спектр на "первой" частоте теста.
    // Важно: вычисляем частоту по targetTract, а не по m_ppmCurrentOnTract (он обновляется позже).
    {
        quint64 defaultHz = 0;
        const int trmType = m_ppmTrmTypeByTract.value(targetTract, -1);
        switch (trmType) {
        case 3:
            defaultHz = static_cast<quint64>(kPowerTestStartFreqType3Hz);
            break;
        case 4:
            defaultHz = static_cast<quint64>(kPowerTestStartFreqType4Hz);
            break;
        case 2:
        default:
            defaultHz = static_cast<quint64>(kPowerTestStartFreqHz);
            break;
        }
        m_powerMomentDisplayFreqHz = defaultHz;
    }

    // Моментный спектр: при смене тракта очищаем старые данные и ставим подпись/окно под новую стартовую частоту.
    if (ui->plotWidgetMomentSpetrumGraph) {
        ui->plotWidgetMomentSpetrumGraph->xAxis->setLabel(formatHzTriplet(m_powerMomentDisplayFreqHz));
        if (m_powerMomentTraces.liveTrace) {
            m_powerMomentTraces.liveTrace->data()->clear();
        }
        if (m_powerMomentTraces.fillBaselineGraph) {
            m_powerMomentTraces.fillBaselineGraph->data()->clear();
        }
        const double centerMHz = static_cast<double>(m_powerMomentDisplayFreqHz) * 1e-6;
        ui->plotWidgetMomentSpetrumGraph->xAxis->setRange(centerMHz - kPowerTestMomentHalfWindowMHz,
                                                          centerMHz + kPowerTestMomentHalfWindowMHz);
        ui->plotWidgetMomentSpetrumGraph->yAxis->setRange(-125.0, 0.0);
        ui->plotWidgetMomentSpetrumGraph->replot(QCustomPlot::rpQueuedReplot);
    }

    // Обнулим график мощности и выставим диапазон под TrmType выбранного тракта.
    const int trmType = m_ppmTrmTypeByTract.value(targetTract, -1);
    const double centerDbm = currentPowerGraphCenterDbm(targetTract);
    const bool fullRange = ui->checkPowerFullRange && ui->checkPowerFullRange->isChecked();
    double xLo = 30.0;
    double xHi = 180.0;
    powerGraphFreqRangeMHzForTrmType(trmType, fullRange, &xLo, &xHi);
    // Чтобы график не прилипал к оси Y и последняя точка не сливалась с правой границей — даём запас по X.
    xLo = qMax(0.0, xLo - 2.0);
    xHi = xHi + 2.0;

    m_powerGraphFreqsMHz.clear();
    m_powerGraphAmpsDbm.clear();
    m_powerGraphTargetFreqsHz.clear();
    // Дефолтный масштаб Y держим всегда; расширяем только по приходящим точкам.
    m_powerGraphAutoYInitialized = true;
    m_powerGraphAutoYCenterDbm = centerDbm;
    if (m_powerGraphTrace) {
        m_powerGraphTrace->data()->clear();
    }
    if (m_powerGraphScatterOk) {
        m_powerGraphScatterOk->data()->clear();
    }
    if (m_powerGraphScatterBad) {
        m_powerGraphScatterBad->data()->clear();
    }
    if (ui->plotWidgetPowerGraph) {
        QSignalBlocker bx(ui->plotWidgetPowerGraph->xAxis);
        (void)bx;
        ui->plotWidgetPowerGraph->xAxis->setRange(xLo, xHi);
        // Дефолтный масштаб мощности: границы "красной зоны".
        // Дальше диапазон может только расширяться по мере прихода точек.
        ui->plotWidgetPowerGraph->yAxis->setRange(centerDbm - kPowerGraphInitialYHalfRangeDbm,
                                                  centerDbm + kPowerGraphInitialYHalfRangeDbm);
        initPowerGraphHelperRects();
        updatePowerGraphHelperRectsXSpan();
        ui->plotWidgetPowerGraph->replot(QCustomPlot::rpQueuedReplot);
    }

    // Анализатор: перестраиваем окно под стартовую частоту (для моментного спектра).
    if (m_analyzerController && m_powerMomentDisplayFreqHz > 0) {
        const quint64 halfSpanHz = kPowerTestAnalyzerSpanHz / 2ULL;
        const quint64 sweepStartHz = (m_powerMomentDisplayFreqHz > halfSpanHz)
                                         ? (m_powerMomentDisplayFreqHz - halfSpanHz)
                                         : 1ULL;
        const quint64 sweepStopHz = m_powerMomentDisplayFreqHz + halfSpanHz;
        m_analyzerController->setSpectrumRange(sweepStartHz, sweepStopHz);
        syncSweepBoundsFromHz(sweepStartHz, sweepStopHz);
    }

    // На случай, если UI был переведён в paused (play/stop) без checked — принудительно возвращаем стартовую кнопку.
    setPowerTestControlsIdle();
}

void MainWindow::applyHandsDefaultsForTract(int tractNum)
{
    if (!ui) {
        return;
    }
    if (!ui->lineEditSpectrumCenterMHz || !ui->comboBoxSpectrumSpanMHz || !ui->lineEditFreqStart || !ui->lineEditFreqStop) {
        return;
    }

    quint64 centerHz = 0;
    quint64 startHz = 0;
    quint64 stopHz = 0;

    // Значения по ТЗ завязаны на выбранный тракт (через TrmType: 2/3/4).
    const int trmType = m_ppmTrmTypeByTract.value(tractNum, -1);
    switch (trmType) {
    case 3:
        centerHz = 345025000ULL;
        startHz = 220000000ULL;
        stopHz = 470000000ULL;
        break;
    case 4:
        centerHz = 520025000ULL;
        startHz = 520000000ULL;
        stopHz = 2500000000ULL;
        break;
    case 2:
    default:
        centerHz = 80025000ULL;
        startHz = 30000000ULL;
        stopHz = 180000000ULL;
        break;
    }

    // UI: центр/спан и диапазон рук.
    ui->lineEditSpectrumCenterMHz->setText(formatHzTriplet4(centerHz));
    const int idx0_5 = ui->comboBoxSpectrumSpanMHz->findData(0.5);
    if (idx0_5 >= 0) {
        ui->comboBoxSpectrumSpanMHz->setCurrentIndex(idx0_5);
    }
    ui->lineEditFreqStart->setText(formatHzTriplet4(startHz));
    ui->lineEditFreqStop->setText(formatHzTriplet4(stopHz));
}

void MainWindow::applyHandsAnalyzerCenterSpan05FromUi()
{
    if (!ui || !m_analyzerController) {
        return;
    }
    if (!ui->lineEditSpectrumCenterMHz || !ui->comboBoxSpectrumSpanMHz) {
        return;
    }
    quint64 centerHz = 0;
    if (!parseTripletLineToHz(ui->lineEditSpectrumCenterMHz->text(), &centerHz) || centerHz == 0) {
        return;
    }
    const int idx0_5 = ui->comboBoxSpectrumSpanMHz->findData(0.5);
    if (idx0_5 >= 0 && ui->comboBoxSpectrumSpanMHz->currentIndex() != idx0_5) {
        ui->comboBoxSpectrumSpanMHz->setCurrentIndex(idx0_5);
    }

    quint64 startHz = 0;
    quint64 stopHz = 0;
    // span фиксирован 0.5 МГц
    if (!spectrumBandFromCenterSpanMHz(static_cast<double>(centerHz) * 1e-6, 0.5, &startHz, &stopHz, nullptr)) {
        return;
    }
    // Применяем диапазон строго под центр (без авто-подстройки сетки).
    m_spectrumGridAlignPending = false;
    m_spectrumGridAlignAttemptsLeft = 0;
    applySpectrumRangeHz(startHz, stopHz, true, true, &centerHz);
}

void MainWindow::onPpmStatusIndicationReceived(uint8_t tractNum, int16_t code)
{
    if (!shouldProcessStationTestingUdp()) {
        return;
    }
    constexpr int ERRCODE_NOERROR = 0;
    constexpr int ERRCODE_PPM_NOANSWER = 1; // "Нет связи с ПП"
    constexpr int ERRCODE_PPM_LUM_OVERHEAT = 4;
    constexpr int ERRCODE_PPM_SWR_ERROR = 5; // "Авария АНТ" (антенная авария / SWR)
    constexpr int ERRCODE_PPM_START = 10;
    constexpr int16_t ERRCODE_PPM_START_LEGACY = static_cast<int16_t>(0xFFFF);

    const bool isOnPowerTab =
        (ui && ui->tabWidget && m_tabPowerIndex >= 0 && ui->tabWidget->currentIndex() == m_tabPowerIndex);
    const bool isOnFhssTab =
        (ui && ui->tabWidget && m_tabFhssIndex >= 0 && ui->tabWidget->currentIndex() == m_tabFhssIndex);
    // "Авария АНТ" обрабатываем на tabPower и tabFHSS; на остальных вкладках игнорируем.
    if (code == ERRCODE_PPM_SWR_ERROR && !isOnPowerTab && !isOnFhssTab) {
        return;
    }

    const int tr = static_cast<int>(tractNum);
    const int16_t lastCode = m_ppmLastStatusCodeByTract.value(tr, static_cast<int16_t>(-1));

    applyPpmErrorIndicationFrameLikeControlPanel(tr, code, lastCode);
    m_ppmLastStatusCodeByTract[tr] = code;

    const bool isOk = (code == ERRCODE_NOERROR);
    const bool isWarningForPowerPause =
        (code == ERRCODE_PPM_LUM_OVERHEAT || code == ERRCODE_PPM_START || code == ERRCODE_PPM_START_LEGACY);
    const bool isFault = (!isOk && !isWarningForPowerPause && code >= 0);
    const bool isDisconnect = (code == ERRCODE_PPM_NOANSWER);
    const bool isAntennaFault = (code == ERRCODE_PPM_SWR_ERROR);
    const bool antennaFaultJustArrived = isAntennaFault && (lastCode != ERRCODE_PPM_SWR_ERROR);

    if (isDisconnect) {
        clearPpmModeLaunchStateForTract(tr);
    }

    // Если во время теста мощности пришёл "Нет связи с ПП" — ставим тест на паузу
    // (без сброса прогресса), а при восстановлении "Норма" — автоматически продолжаем.
    const bool isPowerTargetTract = (m_powerTestTargetTract != 0U && tr == static_cast<int>(m_powerTestTargetTract));
    const bool powerTestHasStateToPauseOrResume =
        (ui && ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked())
        || m_powerTestPaused
        || (m_powerTestSequenceIndex >= 0 && !m_powerTestSequenceFreqsHz.isEmpty());
    if (isFault) { // все ошибки, кроме warning-кодов
        // Power-тест "ставим на паузу" по:
        // - "Нет связи с ПП" (код 1)
        // - "Авария АНТ" (код 5)
        if (isDisconnect || isAntennaFault) {
            if (isPowerTargetTract && powerTestHasStateToPauseOrResume) {
                // Требование 1: RTP/трафик на станцию тоже должен прекратиться.
                // Требование 2: тест должен "поставиться на паузу" и уметь продолжить.
                if (isDisconnect) {
                    pausePowerTestForPpmDisconnect();
                } else {
                    pausePowerTestForAntennaFault();
                }
            }

            // Блокировка имеет смысл только если тест реально активен/имеет состояние для продолжения.
            if (isDisconnect) {
                m_powerTestBlockedByPpm = (isPowerTargetTract && powerTestHasStateToPauseOrResume);
            } else {
                m_powerTestBlockedByAntFault = (isPowerTargetTract && powerTestHasStateToPauseOrResume);
            }

            const int selected = selectedPpmTractFromUi();
            if (ui && ui->pushButtonStartTestingPower && (selected == tr || isPowerTargetTract)
                && ui->pushButtonStartTestingPower->isChecked()) {
                QSignalBlocker blocker(ui->pushButtonStartTestingPower);
                ui->pushButtonStartTestingPower->setChecked(false);
            }
        }
    } else if (isOk) { // "Норма"
        const bool wasBlockedByPpm = m_powerTestBlockedByPpm;
        m_powerTestBlockedByPpm = false;
        const bool wasBlockedByAntFault = m_powerTestBlockedByAntFault;
        m_powerTestBlockedByAntFault = false;

        if (wasBlockedByPpm || wasBlockedByAntFault || m_powerTestBlockedByDirRestore) {
            attemptScheduleDelayedPowerTestResume(tr);
        }
    }

    // tabFHSS: «пауза/возобновление» ППРЧ-теста по ошибкам 1/5 — независимо от текущей вкладки.
    // Условие fhssHasStateToPauseOrResume гарантирует, что мы реагируем только на «свой» тракт,
    // на котором сейчас идёт/паузится ППРЧ-тест.
    const bool isFhssTargetTract = (m_fhssTract > 0 && tr == m_fhssTract);
    const bool fhssHasStateToPauseOrResume =
        isFhssTargetTract && (m_fhssRunning || m_fhssDirSwitchPending
                              || m_fhssBlockedByPpm || m_fhssBlockedByAntFault || m_fhssBlockedByDirRestore
                              || (ui && ui->pushButtonFHSSTestStop && ui->pushButtonFHSSTestStop->isVisible()));
    if (isFault && (isDisconnect || isAntennaFault)) {
        if (fhssHasStateToPauseOrResume) {
            if (isDisconnect) {
                pauseFhssForPpmDisconnect();
            } else {
                pauseFhssForAntennaFault();
            }
        }

        // Блокировку фиксируем только если была активная сессия для pause/resume.
        if (isDisconnect) {
            m_fhssBlockedByPpm = fhssHasStateToPauseOrResume;
        } else {
            m_fhssBlockedByAntFault = fhssHasStateToPauseOrResume;
        }
        updateFhssTestButtonsAccessForSelectedTract();
    } else if (isOk && isFhssTargetTract) {
        const bool wasBlockedByPpm = m_fhssBlockedByPpm;
        const bool wasBlockedByAntFault = m_fhssBlockedByAntFault;
        const bool wasBlockedByDirRestore = m_fhssBlockedByDirRestore;
        m_fhssBlockedByPpm = false;
        m_fhssBlockedByAntFault = false;
        updateFhssTestButtonsAccessForSelectedTract();
        if (wasBlockedByPpm || wasBlockedByAntFault || wasBlockedByDirRestore) {
            attemptScheduleDelayedFhssTestResume(tr);
        }
    }

    if (antennaFaultJustArrived) {
        reloadDirectionAfterAntennaFault(tr);
    }

    // Если во время теста приёма тракт стал неготов (ошибка/не тот статус) — ставим тест на паузу.
    // «ПП не готов» — только красная подпись (как в пульте), без вмешательства в тесты.
    if (!isPpmErrorStatusLabelOnly(code)) {
        stopReceiveTestIfTractNotReady(tr);
    }

    const int selected = selectedPpmTractFromUi();
    if (selected <= 0 || selected != tr) {
        // На вкладке ППРЧ выбранный тракт может быть не синхронизирован с framePPM,
        // но статус для активного FHSS тракта всё равно нужно отрисовать.
        if (isOnFhssTab && m_fhssTract > 0 && tr == m_fhssTract) {
            refreshPpmStatusUiForTract(tr);
        }
        updateTabWidgetLockState();
        return;
    }

    refreshPpmStatusUiForTract(tr);
    updateTabWidgetLockState();
}

void MainWindow::onActiveDirectionIndicationReceived(uint8_t tractNum, uint8_t dirId)
{
    if (!shouldProcessStationTestingUdp()) {
        return;
    }
    const int tr = static_cast<int>(tractNum);
    if (tr <= 0) {
        return;
    }
    m_ppmLastDirIdByTract.insert(tr, dirId);

    const qint64 nowMs = uptimeElapsedMs();

    // Подтверждение нашей CMD_CURR_DIR_SET: до первого совпадения с expected промежуточные DirId не «внешние».
    if (m_selfIssuedDirOpByTract.contains(tr)) {
        SelfIssuedDirOp &op = m_selfIssuedDirOpByTract[tr];
        if (nowMs > op.deadlineMs) {
            m_selfIssuedDirOpByTract.remove(tr);
        } else if (dirId == op.expectedDirId) {
            m_selfIssuedDirOpByTract.remove(tr);
            if (m_ppmExternalDirRecoveryTract == tr) {
                m_ppmExternalDirRecoveryTract = -1;
            }
            m_ppmRestoreDefaultDirPendingByTract.insert(tr, false);
            m_ppmRestoreDefaultDirInFlightByTract.insert(tr, false);
        } else {
            return;
        }
    }

    const uint8_t fhssExp = fhssExpectedDirIdFromModeCombo();

    // ППРЧ: дождались выбранного в modeFHSSComboBox DirId → запуск потока.
    if (m_fhssDirSwitchPending && tr == m_fhssTract && dirId == fhssExp) {
        m_fhssDirSwitchPending = false;
        m_fhssBlockedByDirRestore = false;
        if (ui) {
            if (ui->pushButtonFHSSTestStop) {
                ui->pushButtonFHSSTestStop->setEnabled(true);
            }
        }
        QTimer::singleShot(0, this, [this]() { startFhssTransmission(); });
        return;
    }

    // Внешняя смена направления во время активного ППРЧ (не совпадает с выбором в комбобоксе).
    if (isFhssTestActive() && tr == m_fhssTract && dirId != fhssExp) {
        if (!(m_fhssBlockedByDirRestore && m_fhssDirSwitchPending)) {
            DEBUG << QStringLiteral("ППРЧ: внешнее переключение направления (тракт %1, DirId=%2, ожидался DirId=%3).")
                         .arg(tr)
                         .arg(static_cast<int>(dirId))
                         .arg(static_cast<int>(fhssExp));
            pauseFhssForExternalDirectionRestore();
            beginFhssResumeDirectionCommand(fhssExp);
        }
        return;
    }

    if (dirId == 1) {
        if (m_ppmExternalDirRecoveryTract == tr) {
            m_ppmExternalDirRecoveryTract = -1;
        }
        m_ppmRestoreDefaultDirPendingByTract.insert(tr, false);
        m_ppmRestoreDefaultDirInFlightByTract.insert(tr, false);
        // Повторный выбор DirId=1: IND_ERROR может не прийти повторно; без гейта по IND_WORKMODE
        // кратковременный TRAKT_WRK от IND_ACTIVEDIR перебивается TRAKT_END_ON от IND_TRAKT_* и «залипает» жёлтым.
        syncPpmFrameForDir1IfTransmitterOk(tr, true);
        attemptScheduleDelayedPowerTestResume(tr);
        tryFinishFhssReturnToDefaultDirection(tr);
        return;
    }
    if (tr != m_ppmCurrentOnTract) {
        return;
    }
    if (!m_externalSwitchProtectionArmed) {
        return;
    }

    DEBUG << QStringLiteral("ППМ: обнаружено внешнее переключение направления (тракт %1, DirId=%2).")
                 .arg(tr)
                 .arg(static_cast<int>(dirId));
    m_ppmExternalDirRecoveryTract = tr;
    m_ppmRestoreDefaultDirPendingByTract.insert(tr, true);
    m_ppmRestoreDefaultDirInFlightByTract.insert(tr, false);

    const bool isPowerTargetTract =
        (m_powerTestTargetTract != 0U && tr == static_cast<int>(m_powerTestTargetTract));
    const bool powerTestHasStateToPauseOrResume =
        (ui && ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked())
        || m_powerTestPaused
        || (m_powerTestSequenceIndex >= 0 && !m_powerTestSequenceFreqsHz.isEmpty());
    if (isPowerTargetTract && powerTestHasStateToPauseOrResume) {
        pausePowerTestForDirectionRestore();
        m_powerTestBlockedByDirRestore = true;
    }

    maybeRestoreDefaultDirectionForTract(tr);
}

void MainWindow::onProfileSwitchIndicationReceived(uint8_t profileId, uint8_t phase)
{
    if (!shouldProcessStationTestingUdp()) {
        return;
    }
    Q_UNUSED(phase);

    // Реагируем только на "боевой" режим после старта тестирования:
    // до этого profile switch может быть частью наших штатных действий.
    if (!m_externalSwitchProtectionArmed) {
        return;
    }
    if (!ui || !ui->framePPM || !ui->framePPM->isVisible()) {
        return;
    }
    if (shouldKeepStationHeaderProgressVisible()) {
        return;
    }

    restorePreStartStateAfterExternalProfileSwitch(profileId);
}

void MainWindow::onWorkModeIndicationReceived(uint8_t tractNum, uint16_t mode)
{
    if (!shouldProcessStationTestingUdp()) {
        return;
    }
    const int tr = static_cast<int>(tractNum);
    const bool wasModeLaunchPending = m_ppmModeLaunchPendingByTract.value(tr, false);
    m_ppmLastWorkModeByTract.insert(tr, mode);

    if (mode != 0) {
        clearPpmModeLaunchStateForTract(tr);
        // После появления ненулевого IND_WORKMODE тракт может остаться в TRAKT_END_ON до следующего IND_ERROR/ACTIVEDIR.
        syncPpmFrameForDir1IfTransmitterOk(tr, true);
        // Аналог ControlPanel: при завершении загрузки рабочего режима "Норма"/"Перегрев ЛУМ"
        // должны возвращать зелёную рамку и для сложных режимов с DirId != 1
        // (ТМО, ТМО ППРЧ, СР ППРЧ), даже если IND_ERROR не переотправился.
        if (wasModeLaunchPending) {
            syncPpmFrameForDir1IfTransmitterOk(tr, true, false);
        }
        tryFinishFhssReturnToDefaultDirection(tr);
    }

    const int selected = selectedPpmTractFromUi();
    if (selected <= 0 || selected != tr) {
        stopReceiveTestIfTractNotReady(tr);
        return;
    }

    applyPpmModeFrameForTract(tr);
    stopReceiveTestIfTractNotReady(tr);
}

void MainWindow::onChannelReadyIndicationReceived(uint8_t tractNum, uint8_t linkStatus)
{
    if (!shouldProcessStationTestingUdp()) {
        return;
    }
    const int tr = static_cast<int>(tractNum);
    if (tr <= 0) {
        return;
    }
    m_ppmLastLinkStatusByTract.insert(tr, linkStatus);

    // Маппинг как в ControlPanelSurs::PpmForm::update_status_wrk по sps_param.linkStatusIndicator:
    // ВАЖНО: в пульте этот индикатор красит "строку статуса"/иконки TX/RX,
    // но НЕ основной frame тракта (ui->frame). Поэтому тут используем его только
    // для визуализации факта TX (emissionAntennaWidget*), а цвет рамок PPM
    // оставляем по состояниям тракта (TRAKT_WRK/WAIT_WRK/ERR/STOP), которые приходят
    // из IND_ERROR/IND_WORKMODE/IND_TRAKT_*.
    const bool isRx = (linkStatus == 1 || linkStatus == 4 || linkStatus == 12 || linkStatus == 16 || linkStatus == 17);
    const bool isTx = (linkStatus == 2 || linkStatus == 3 || linkStatus == 7 || linkStatus == 20 || linkStatus == 21);
    const bool isWait = (linkStatus == 15 || linkStatus == 22);

    // Привязка показа/скрытия "антенны излучения" к реальному TX (сиреневая рамка в пульте).
    const bool relevantTract =
        (tr == m_ppmCurrentOnTract) || (tr == m_fhssTract);
    if (!relevantTract || !ui) {
        return;
    }

    const bool powerTestActive =
        // Тест мощности: виджет (ножка) должен быть виден сразу после старта и до stop/finish.
        (ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked()) || m_powerTestPaused
        || (m_powerTestSequenceIndex >= 0 && !m_powerTestSequenceFreqsHz.isEmpty());
    const bool fhssTestActive = isFhssTestActive();

    // ВАЖНО: FHSS-виджет используем ТОЛЬКО для ППРЧ-теста.
    // Иначе, находясь на tabFHSS, можно "подцепить" TX индикацию обычного тракта (m_ppmCurrentOnTract)
    // и снова сделать emissionAntennaWidgetFHSS видимым после Stop.
    const bool routeToFhssWidget = fhssTestActive || (tr == m_fhssTract);
    if (routeToFhssWidget) {
        if (ui->emissionAntennaWidgetFHSS) {
            // По ТЗ: «ножка/излучатель» на вкладке ППРЧ допустима ТОЛЬКО для режима «МПР».
            // На прочих режимах гасим анимацию и держим виджет скрытым, даже при isTx из железа.
            if (!isFhssModeMpr()) {
                ui->emissionAntennaWidgetFHSS->stopTransmission();
                ui->emissionAntennaWidgetFHSS->setVisible(false);
            } else {
                if (isTx) {
                    ui->emissionAntennaWidgetFHSS->startTransmission();
                } else {
                    ui->emissionAntennaWidgetFHSS->stopTransmission();
                }
                // Видимость "ножки" отделена от анимации: во время теста держим видимой.
                ui->emissionAntennaWidgetFHSS->setVisible(fhssTestActive || isTx);
            }
        }
    } else {
        if (ui->emissionAntennaWidget) {
            if (isTx) {
                ui->emissionAntennaWidget->startTransmission();
            } else {
                ui->emissionAntennaWidget->stopTransmission();
            }
            // Видимость "ножки" отделена от анимации: во время теста держим видимой.
            ui->emissionAntennaWidget->setVisible(powerTestActive || isTx);

            // Синхронизируем флаг анимации излучения с фактическим состоянием виджета:
            // используется для показа зелёной подписи пика dBm на plotWidgetMomentSpetrumGraph.
            m_emissionAnimating = isTx;
            if (!isTx && m_powerMomentPeakLabel && m_powerMomentPeakLabel->visible()) {
                m_powerMomentPeakLabel->setVisible(false);
                if (ui->plotWidgetMomentSpetrumGraph) {
                    ui->plotWidgetMomentSpetrumGraph->replot(QCustomPlot::rpQueuedReplot);
                }
            }
        }
    }

    Q_UNUSED(isRx)
    Q_UNUSED(isWait)
}

void MainWindow::onLinkStatusIndicationReceived(uint8_t tractNum, uint16_t val)
{
    if (!shouldProcessStationTestingUdp()) {
        return;
    }
    // В пульте linkStatusIndicator берётся так: snmp_val & 0xFF.
    // Поэтому используем младший байт как "state" (0..22 и т.п.).
    const uint8_t state = static_cast<uint8_t>(val & 0xFFu);
    onChannelReadyIndicationReceived(tractNum, state);
}

void MainWindow::configureFrameStationHeaderLayout()
{
    if (!ui || !ui->horizontalLayout_2) {
        return;
    }

    // labelStation | stretch | framePPM / button / progressBar | stretch | status | led
    ui->horizontalLayout_2->setStretch(1, 1);
    ui->horizontalLayout_2->setStretch(5, 1);

    if (ui->framePPM) {
        ui->framePPM->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    }
    if (ui->pushButtonStartTesting) {
        ui->pushButtonStartTesting->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    if (ui->progressBar) {
        ui->progressBar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
}

void MainWindow::applyStationHeaderProgressBarLayout(bool expanded)
{
    if (!ui || !ui->horizontalLayout_2 || !ui->progressBar) {
        return;
    }

    if (expanded) {
        ui->progressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        ui->progressBar->setMinimumWidth(120);
        ui->progressBar->setMaximumWidth(QWIDGETSIZE_MAX);
        ui->horizontalLayout_2->setStretch(1, 0);
        ui->horizontalLayout_2->setStretch(4, 1);
        ui->horizontalLayout_2->setStretch(5, 0);
        return;
    }

    ui->progressBar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    ui->progressBar->setMinimumWidth(280);
    ui->progressBar->setMaximumWidth(280);
    ui->horizontalLayout_2->setStretch(1, 1);
    ui->horizontalLayout_2->setStretch(4, 0);
    ui->horizontalLayout_2->setStretch(5, 1);
}

void MainWindow::showStationHeaderCenter(StationHeaderCenter center)
{
    if (!ui) {
        return;
    }
    if (ui->pushButtonStartTesting) {
        if (center == StationHeaderCenter::StartButton && ui->pushButtonStartTesting->isChecked()) {
            QSignalBlocker blocker(ui->pushButtonStartTesting);
            ui->pushButtonStartTesting->setChecked(false);
        }
        ui->pushButtonStartTesting->setVisible(center == StationHeaderCenter::StartButton);
    }
    if (ui->progressBar) {
        ui->progressBar->setVisible(center == StationHeaderCenter::ProgressBar);
    }
    if (ui->framePPM) {
        ui->framePPM->setVisible(center == StationHeaderCenter::FramePpm);
    }
    updateMenubarVisibility();
}

void MainWindow::updateMenubarVisibility()
{
    if (!ui || !ui->menubar) {
        return;
    }
    ui->menubar->setVisible(!isActivePpmTestingSession());
}

bool MainWindow::shouldKeepStationHeaderProgressVisible() const
{
    if (m_profileIntegrityStage != ProfileIntegrityStage::None) {
        return true;
    }
    if (m_ppmPowerStage != PpmPowerSequenceStage::None) {
        return true;
    }
    if (m_postReconnectStationBootWaitActive) {
        return true;
    }
    return false;
}

bool MainWindow::isActivePpmTestingSession() const
{
    if (m_ppmPowerStage != PpmPowerSequenceStage::None) {
        return true;
    }
    if (m_ppmCurrentOnTract > 0) {
        return true;
    }
    return ui && ui->framePPM && ui->framePPM->isVisible();
}

void MainWindow::initPpmUiStyle()
{
    if (!ui->framePPM) {
        return;
    }
    showStationHeaderCenter(StationHeaderCenter::StartButton);

    m_ppmButtonGroup = new QButtonGroup(this);
    m_ppmButtonGroup->setExclusive(true);
    if (ui->radioPPM1 && ui->radioPPM2) {
        m_ppmButtonGroup->addButton(ui->radioPPM1, 0);
        m_ppmButtonGroup->addButton(ui->radioPPM2, 1);
    }

    connect(m_ppmButtonGroup,
            &QButtonGroup::idClicked,
            this,
            &MainWindow::onPpmRadioClicked);

    if (ui->labelUpdate) {
        ui->labelUpdate->setCursor(Qt::PointingHandCursor);
        connect(ui->labelUpdate, &QPushButton::clicked, this, &MainWindow::onPpmUpdateClicked);
        ui->labelUpdate->setVisible(true);
    }
    if (ui->labelRecieveUpdate) {
        ui->labelRecieveUpdate->setCursor(Qt::PointingHandCursor);
        connect(ui->labelRecieveUpdate, &QPushButton::clicked, this, &MainWindow::onPpmUpdateClicked);
        ui->labelRecieveUpdate->setVisible(true);
    }
    if (ui->labelUpdateFHSS) {
        ui->labelUpdateFHSS->setCursor(Qt::PointingHandCursor);
        connect(ui->labelUpdateFHSS, &QPushButton::clicked, this, &MainWindow::onPpmUpdateClicked);
        ui->labelUpdateFHSS->setVisible(true);
    }

    // Начальные стили подписей и рамок ППМ заданы в mainwindow.ui.
    if (ui->labelPPMStatus) {
        ui->labelPPMStatus->setText(QStringLiteral("—"));
    }
    if (ui->labelRecievePPMStatus) {
        ui->labelRecievePPMStatus->setText(QStringLiteral("—"));
    }
    if (ui->labelPPMStatusFHSS) {
        ui->labelPPMStatusFHSS->setText(QStringLiteral("—"));
    }
}

void MainWindow::applyTraktParamToPpmUi(const QVector<TraktParamEntry> &entries, int traktNum)
{
    m_ppmTrmTypeByTract.clear();
    m_maxTrLn = 0;

    if (!ui->framePPM || !ui->radioPPM1 || !ui->radioPPM2 || !m_ppmButtonGroup) {
        return;
    }

    QHBoxLayout *hLay = qobject_cast<QHBoxLayout *>(ui->framePPM->layout());
    if (!hLay) {
        return;
    }

    QVector<TraktParamEntry> sorted = entries;
    std::sort(sorted.begin(), sorted.end(), [](const TraktParamEntry &a, const TraktParamEntry &b) {
        if (a.trLn != b.trLn) {
            return a.trLn < b.trLn;
        }
        if (a.trmType != b.trmType) {
            return a.trmType < b.trmType;
        }
        return a.trmNr < b.trmNr;
    });
    if (traktNum > 0 && sorted.size() > traktNum) {
        sorted.resize(traktNum);
    }

    // В протоколе управления трактами станция ожидает не "TrLN", а TrId (1..N),
    // то есть порядковый номер тракта в конфигурации (после сортировки по TrLN).
    // При этом индикации и TraktParam.xml оперируют TrLN, который может быть не 1..N.
    // Поэтому для UI/команд PPM используем TrId, а TrLN остаётся только для построения подписей.
    QHash<int, int> trIdByTrLn; // TrLN -> TrId (1..N)
    trIdByTrLn.reserve(sorted.size());
    for (int i = 0; i < sorted.size(); ++i) {
        const TraktParamEntry &e = sorted.at(i);
        m_maxTrLn = qMax(m_maxTrLn, e.trLn);
        if (e.trLn > 0) {
            trIdByTrLn.insert(e.trLn, i + 1);
            m_ppmTrmTypeByTract.insert(i + 1, e.trmType);
        }
    }
    // Для PPM-управления исключаем тракты с TrmType==1 (ДМКВ),
    // но сохраняем их существование в общей конфигурации (они влияют на TrLN).
    QVector<TraktParamEntry> uiSorted;
    uiSorted.reserve(sorted.size());
    for (const TraktParamEntry &e : sorted) {
        if (e.trmType == 1) {
            continue;
        }
        uiSorted.push_back(e);
    }
    const int uiTrNum = uiSorted.size();
    const QStringList labels = ppmLabelsForSortedTrakts(sorted);
    const int radioCount = qMax(uiTrNum, 2);

    m_ppmTractsSorted.clear();
    for (int i = 0; i < uiTrNum && i < uiSorted.size(); ++i) {
        const int trLn = uiSorted.at(i).trLn;
        const int trId = trIdByTrLn.value(trLn, -1);
        m_ppmTractsSorted.push_back(trId);
    }
    while (m_ppmTractsSorted.size() < radioCount) {
        // Если управляемых трактов меньше, чем кнопок в UI — оставляем "пустые" места.
        m_ppmTractsSorted.push_back(-1);
    }

    for (QRadioButton *ex : m_ppmExtraRadios) {
        m_ppmButtonGroup->removeButton(ex);
        hLay->removeWidget(ex);
        delete ex;
    }
    m_ppmExtraRadios.clear();

    auto setRadioText = [&](QRadioButton *rb, int idx) {
        if (!rb) {
            return;
        }
        const bool hasTract = (idx >= 0 && idx < m_ppmTractsSorted.size() && m_ppmTractsSorted.at(idx) > 0);
        if (!hasTract) {
            rb->setText(QStringLiteral("—"));
            rb->setEnabled(false);
            return;
        }
        // Подписи строим по исходному отсортированному списку, но с фильтрацией TrmType==1 внутри функции.
        if (idx < labels.size()) {
            rb->setText(labels.at(idx));
        } else {
            rb->setText(QStringLiteral("ППМ%1").arg(idx + 1));
        }
        rb->setEnabled(true);
    };

    setRadioText(ui->radioPPM1, 0);
    setRadioText(ui->radioPPM2, 1);
    const QString radioOffStyle = ui->radioPPM1 ? ui->radioPPM1->styleSheet() : QString();

    for (int i = 2; i < radioCount; ++i) {
        QRadioButton *rb = new QRadioButton(ui->framePPM);
        rb->setFont(ui->radioPPM1->font());
        if (!radioOffStyle.isEmpty()) {
            rb->setStyleSheet(radioOffStyle);
        }
        setRadioText(rb, i);
        hLay->addWidget(rb);
        m_ppmExtraRadios.append(rb);
    }

    const QList<QAbstractButton *> prev = m_ppmButtonGroup->buttons();
    for (QAbstractButton *b : prev) {
        m_ppmButtonGroup->removeButton(b);
    }
    m_ppmButtonGroup->addButton(ui->radioPPM1, 0);
    m_ppmButtonGroup->addButton(ui->radioPPM2, 1);
    for (int i = 0; i < m_ppmExtraRadios.size(); ++i) {
        m_ppmButtonGroup->addButton(m_ppmExtraRadios[i], 2 + i);
    }
    if (ui->radioPPM1) {
        ui->radioPPM1->setChecked(false);
    }
}

QVector<int> MainWindow::ppmTractNumbersForUi() const
{
    if (!m_ppmTractsSorted.isEmpty()) {
        QVector<int> out;
        out.reserve(m_ppmTractsSorted.size());
        for (int t : m_ppmTractsSorted) {
            if (t > 0) {
                out.push_back(t);
            }
        }
        return out;
    }
    if (!m_ppmButtonGroup) {
        return {};
    }
    const int n = m_ppmButtonGroup->buttons().size();
    QVector<int> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        out.push_back(i + 1);
    }
    return out;
}

int MainWindow::ppmFirstTractNumber() const
{
    const QVector<int> tracts = ppmTractNumbersForUi();
    return tracts.isEmpty() ? -1 : tracts.first();
}

void MainWindow::setPpmRadioUiState(int id, bool isOn, bool checked)
{
    if (!m_ppmButtonGroup) {
        return;
    }
    QAbstractButton *b = m_ppmButtonGroup->button(id);
    QRadioButton *rb = qobject_cast<QRadioButton *>(b);
    if (!rb) {
        return;
    }
    rb->setStyleSheet(isOn ? styleSheetPpmRadioON : styleSheetPpmRadioOFF);
    QSignalBlocker blocker(rb);
    rb->setChecked(checked);
}

void MainWindow::setAllPpmRadiosEnabled(bool enabled)
{
    if (!m_ppmButtonGroup) {
        return;
    }
    const QList<QAbstractButton *> buttons = m_ppmButtonGroup->buttons();
    for (QAbstractButton *b : buttons) {
        if (b) {
            b->setEnabled(enabled);
        }
    }
}

void MainWindow::stopAllTestsForPpmRecovery()
{
    if (!ui) {
        return;
    }

    // Тест мощности: принудительный полный stop/reset независимо от текущего sub-state.
    if (ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked()) {
        ui->pushButtonStartTestingPower->setChecked(false);
    } else {
        onPowerTestingToggled(false);
    }
    m_powerTestPaused = false;
    setPowerTestControlsIdle();

    // Тест приёма: всегда полный stop/reset.
    if (m_receiveTestRunning) {
        tearDownReceiveTest(true);
    } else {
        setReceiveTestControlsIdle();
        resetReceiveReadoutUi();
        setEmissionAnimating(false);
    }
}

void MainWindow::restorePreStartStateAfterExternalProfileSwitch(uint8_t profileId)
{
    clearAllSelfIssuedGuards();
    m_externalSwitchProtectionArmed = false;
    m_ppmExternalDirRecoveryTract = -1;
    m_ppmRestoreDefaultDirPendingByTract.clear();
    m_ppmRestoreDefaultDirInFlightByTract.clear();

    stopAllTestsForPpmRecovery();
    setFhssTestControlsIdle(true);

    const int tractForReset =
        (m_ppmCurrentOnTract > 0) ? m_ppmCurrentOnTract : std::max(selectedPpmTractFromUi(), ppmFirstTractNumber());
    if (tractForReset > 0) {
        resetPowerTestUiForNewTractSelection(tractForReset);
        resetReceiveTestUiForNewTractSelection(tractForReset);
    } else {
        resetPowerReadoutUi();
        resetReceiveReadoutUi();
    }

    m_ppmCurrentOnTract = -1;
    m_ppmPendingTargetOnTract = -1;
    m_ppmSwitchNeedsPostUpdate = false;
    m_ppmPowerStage = PpmPowerSequenceStage::None;
    m_ppmPowerSeqIndex = 0;
    m_ppmIgnoreExternalPowerOffTract = -1;
    m_ppmIgnoreExternalPowerOffUntilMs = 0;

    setAllPpmRadiosEnabled(false);
    const QList<QAbstractButton *> buttons = m_ppmButtonGroup ? m_ppmButtonGroup->buttons() : QList<QAbstractButton *>();
    for (QAbstractButton *button : buttons) {
        const int id = m_ppmButtonGroup->id(button);
        setPpmRadioUiState(id, false, false);
    }

    showStationHeaderCenter(StationHeaderCenter::StartButton);
    if (ui && ui->progressBar) {
        ui->progressBar->setRange(0, 100);
        ui->progressBar->setValue(0);
    }

    updatePowerTestButtonsAccessForSelectedTract();
    updateReceiveTestButtonsAccessForSelectedTract();
    updateFhssTestButtonsAccessForSelectedTract();
    updateTabWidgetLockState();

    Q_UNUSED(profileId);
    onDeviceLogMessage(QStringLiteral(
        "Обнаружена смена активного профиля на радиостанции. Для перевода радиостанции в режим тестирования "
        "нажмите \"НАЧАТЬ ТЕСТИРОВАНИЕ\""));
}

void MainWindow::onTractPowerIndicationReceived(uint8_t tractNum, bool isOn)
{
    if (!shouldProcessStationTestingUdp()) {
        return;
    }
    const int tr = static_cast<int>(tractNum);
    if (tr <= 0) {
        return;
    }
    if (m_postReconnectStationBootWaitActive && isOn && tr == m_postReconnectStationBootLastTractNum) {
        m_postReconnectStationBootLastTractOnSeen = true;
        tryStartPpmInitAfterPostReconnectBootGates();
    }
    setPpmFrameStateForTract(tr, isOn ? TRAKT_END_ON : TRAKT_END_OFF);
    if (isOn) {
        // Индикация «тракт включён» ставит TRAKT_END_ON (жёлтое ожидание). При внешнем повторе DirId=1
        // IND_ERROR часто не дублируется — возвращаем TRAKT_WRK, когда режим уже загружен.
        syncPpmFrameForDir1IfTransmitterOk(tr, true);
    }

    // Если выключение инициировали мы (например, при штатном переключении тракта),
    // не считаем это "внешним" событием даже при гонках сигналов.
    if (!isOn && tr == m_ppmIgnoreExternalPowerOffTract) {
        const qint64 nowMs = m_uptime.isValid() ? m_uptime.elapsed() : 0;
        if (nowMs >= 0 && nowMs < m_ppmIgnoreExternalPowerOffUntilMs) {
            return;
        }
    }

    // Пока ждём загрузку DirId=1 после внешней смены направления — любые индикации вкл/выкл тракта
    // считаем штатными (как при внутреннем переключении), без лога «внешнее» и без защиты восстановления.
    // То же для хвоста перезагрузки тракта после наших CMD_CURR_DIR_SET / «Обновить».
    const qint64 nowTractMs = m_uptime.isValid() ? m_uptime.elapsed() : 0;
    const bool suppressSelfTractReload =
        (tr > 0 && m_selfIssuedTractReloadUntilMsByTract.contains(tr) && nowTractMs >= 0
         && nowTractMs <= m_selfIssuedTractReloadUntilMsByTract.value(tr));

    // Пока ждём завершения стартовой последовательности станции после reboot (см. beginPostReconnectStationBootWait…),
    // индикации ВКЛ трактов 1…N — штатные, иначе сработает «внешнее включение» и уйдут конкурирующие CMD_TRACT_CONTROL.
    const bool suppressExternalTractProtection =
        !m_externalSwitchProtectionArmed || (m_ppmExternalDirRecoveryTract >= 0) || suppressSelfTractReload
        || m_postReconnectStationBootWaitActive;

    // Логи "как в Station_starter_3": фиксируем внешнее включение/выключение тракта.
    // Это помогает отличать переключение режима от переключения тракта по последовательности событий.
    if (!suppressExternalTractProtection && m_deviceController && m_deviceController->isConnected()
        && !m_deviceController->isAwaitingTractPowerAck()
        && m_ppmPowerStage == PpmPowerSequenceStage::None) {
        DEBUG << QStringLiteral("ППМ: обнаружено внешнее %1 тракта: tr=%2 (подтверждено)")
                     .arg(isOn ? QStringLiteral("включение") : QStringLiteral("выключение"))
                     .arg(tr);
    }

    // Внешнее включение неактивного тракта: принудительно выключаем (защита от обхода ПО).
    const bool tractKnownForPpm =
        (tr > 0 && (m_ppmTractsSorted.isEmpty() || m_ppmTractsSorted.contains(tr)));
    if (!suppressExternalTractProtection && tractKnownForPpm && m_deviceController
        && m_deviceController->isConnected() && !m_deviceController->isAwaitingTractPowerAck()
        && m_ppmPowerStage == PpmPowerSequenceStage::None && isOn && tr != m_ppmCurrentOnTract) {
        if (!m_deviceController->setTractControl(static_cast<uint8_t>(tr), false, true)) {
            DEBUG << QStringLiteral("ППМ: не удалось отправить выключение тракта %1 (внешнее включение).")
                         .arg(tr);
        }
    }

    // Ветка восстановления нужна только когда "наш" активный тракт выключили извне.
    if (isOn || tr != m_ppmCurrentOnTract) {
        return;
    }

    // Не вмешиваемся в штатные сценарии init/switch и ожидание ACK от наших команд.
    if (!m_deviceController || !m_deviceController->isConnected()) {
        return;
    }
    if (m_deviceController->isAwaitingTractPowerAck() || m_ppmPowerStage != PpmPowerSequenceStage::None) {
        return;
    }

    if (suppressExternalTractProtection) {
        return;
    }

    DEBUG << QStringLiteral("ППМ: получено внешнее выключение активного тракта %1, запускаю восстановление.")
                 .arg(tr);

    stopAllTestsForPpmRecovery();

    // Как при обычном переключении тракта: обнуляем график мощности/подготовку UI,
    // чтобы не оставались точки от предыдущего состояния.
    resetPowerTestUiForNewTractSelection(tr);
    resetPowerReadoutUi();

    // Состояние текущего тракта стало OFF по внешнему событию.
    const int offIdx = m_ppmTractsSorted.indexOf(tr);
    if (offIdx >= 0) {
        setPpmRadioUiState(offIdx, false, false);
    }
    clearPpmModeLaunchStateForTract(tr);
    m_ppmCurrentOnTract = -1;

    // По ТЗ: во время восстановления держим бесконечный progressBar и скрываем PPM.
    if (ui && ui->progressBar) {
        showStationHeaderCenter(StationHeaderCenter::ProgressBar);
        ui->progressBar->setTextVisible(false);
        ui->progressBar->setRange(0, 0);
        ui->progressBar->setValue(0);
    }

    // Новый сценарий: сразу повторно включаем именно тот тракт, с которым работали.
    m_ppmPendingTargetOnTract = tr;
    m_ppmSwitchNeedsPostUpdate = true;
    m_ppmPowerStage = PpmPowerSequenceStage::SwitchOnTarget;
    setAllPpmRadiosEnabled(false);
    updateTabWidgetLockState(); // переведёт на tabHands и заблокирует остальные вкладки

    if (!m_deviceController->setTractControl(static_cast<uint8_t>(tr), true, true)) {
        onDeviceLogMessage(QStringLiteral("ОШИБКА: не удалось отправить команду повторного включения тракта %1.").arg(tr));
        m_ppmPowerStage = PpmPowerSequenceStage::None;
        m_ppmPendingTargetOnTract = -1;
        m_ppmSwitchNeedsPostUpdate = false;
        if (ui->progressBar) {
            ui->progressBar->setRange(0, 100);
            ui->progressBar->setValue(0);
            showStationHeaderCenter(StationHeaderCenter::FramePpm);
        }
        updateTabWidgetLockState();
    }
}

void MainWindow::onTractPowerAwaitingAck(uint8_t tractNum, bool enable)
{
    if (!shouldProcessStationTestingUdp()) {
        return;
    }
    onDeviceLogMessage(QStringLiteral("Управление трактом: Тракт=%1, %2")
                           .arg(static_cast<int>(tractNum))
                           .arg(enable ? QStringLiteral("ВКЛ") : QStringLiteral("ВЫКЛ")));
    setPpmFrameStateForTract(static_cast<int>(tractNum), enable ? TRAKT_START_ON : TRAKT_START_OFF);
    setAllPpmRadiosEnabled(false);
    updateTabWidgetLockState();
    updatePowerTestButtonsAccessForSelectedTract();
}

void MainWindow::onTractPowerAcknowledged(uint8_t tractNum, bool isOn)
{
    if (!shouldProcessStationTestingUdp()) {
        return;
    }
    // Сбрасываем счётчик повторов для этой операции.
    {
        const quint32 key = (static_cast<quint32>(tractNum) << 1) | (isOn ? 1u : 0u);
        m_tractPowerAckRetries.remove(key);
    }

    const int idx = m_ppmTractsSorted.indexOf(static_cast<int>(tractNum));
    if (idx >= 0) {
        const bool checked = isOn && (static_cast<int>(tractNum) == m_ppmCurrentOnTract);
        setPpmRadioUiState(idx, isOn, checked);
    }

    const int tr = static_cast<int>(tractNum);
    if (isOn) {
        markPpmModeLaunchStarted(tr);
    } else {
        clearPpmModeLaunchStateForTract(tr);
    }
    setPpmFrameStateForTract(tr, isOn ? TRAKT_END_ON : TRAKT_END_OFF);

    // Завершение init-последовательности: показываем PPM только после подтверждения
    // включения первого тракта.
    if (m_ppmPowerStage == PpmPowerSequenceStage::InitFirstOnWaitAck &&
        isOn && static_cast<int>(tractNum) == m_ppmCurrentOnTract) {
        m_ppmPowerStage = PpmPowerSequenceStage::None;
        // После штатной инициализации трактов (после reboot по кнопке "НАЧАТЬ ТЕСТИРОВАНИЕ")
        // снова включаем защиту от внешних переключений.
        m_externalSwitchProtectionArmed = true;
        if (ui && ui->progressBar) {
            ui->progressBar->setRange(0, 100);
            ui->progressBar->setValue(0);
            showStationHeaderCenter(StationHeaderCenter::FramePpm);
        }
        // По ТЗ: при первом появлении PPM выставляем стартовые значения tabHands по выбранному тракту.
        applyHandsDefaultsForTract(m_ppmCurrentOnTract);
        // При первом появлении PPM (и переходе к tabPower) график мощности должен
        // сразу иметь фиксированный масштаб по оси Y по границам "красной зоны".
        // Дальше диапазон может только расширяться, если приходит точка вне границ.
        if (ui && ui->plotWidgetPowerGraph) {
            applyPowerGraphCenterScale();
        }
        refreshPpmStatusUiForTract(m_ppmCurrentOnTract);
    }

    continuePpmInitSequence();
    continuePpmSwitchSequence();

    const bool canInteract = m_deviceController && m_deviceController->isConnected()
                             && !m_deviceController->isAwaitingTractPowerAck()
                             && (m_ppmPowerStage == PpmPowerSequenceStage::None);
    setAllPpmRadiosEnabled(canInteract);
    updateTabWidgetLockState();
}

void MainWindow::onTractPowerAckTimeout(uint8_t tractNum, bool expectedOn)
{
    if (!shouldProcessStationTestingUdp()) {
        return;
    }
    onDeviceLogMessage(QString("Таймаут ожидания подтверждения %1 тракта %2")
                           .arg(expectedOn ? QStringLiteral("включения") : QStringLiteral("выключения"))
                           .arg(tractNum));

    // Важно: IND_TRAKT_*_SE может прийти ПОСЛЕ таймаута.
    // Если это было наше внутреннее выключение (например, при переключении тракта),
    // то поздняя индикация OFF не должна трактоваться как "внешнее выключение" и
    // запускать восстановление тракта обратно.
    if (!expectedOn) {
        m_ppmIgnoreExternalPowerOffTract = static_cast<int>(tractNum);
        const qint64 nowMs = m_uptime.isValid() ? m_uptime.elapsed() : 0;
        // Делаем окно больше таймаута ACK, чтобы перехватить позднее подтверждение станции.
        m_ppmIgnoreExternalPowerOffUntilMs = (nowMs >= 0) ? (nowMs + 6000) : 0;
    }
    setPpmFrameStateForTract(static_cast<int>(tractNum), TRAKT_WAIT_WRK);

    // По требованию: при таймауте один раз повторяем команду.
    if (m_deviceController && m_deviceController->isConnected() &&
        m_ppmPowerStage != PpmPowerSequenceStage::None) {
        const quint32 key = (static_cast<quint32>(tractNum) << 1) | (expectedOn ? 1u : 0u);
        const int tries = m_tractPowerAckRetries.value(key, 0);
        if (tries < 1) {
            m_tractPowerAckRetries.insert(key, tries + 1);
            onDeviceLogMessage(QString("Повтор команды %1 тракта %2 (попытка 2)")
                                   .arg(expectedOn ? QStringLiteral("включения") : QStringLiteral("выключения"))
                                   .arg(tractNum));
            // awaitAck=true, чтобы снова ждать IND_TRAKT_*_SE
            m_deviceController->setTractControl(tractNum, expectedOn, true);
            // Оставляем UI/состояние последовательности как есть, чтобы не зависнуть в блокировке.
            setAllPpmRadiosEnabled(false);
            updateTabWidgetLockState();
            return;
        }
    }

    const bool wasSwitching = (m_ppmPowerStage == PpmPowerSequenceStage::SwitchOffCurrent ||
                               m_ppmPowerStage == PpmPowerSequenceStage::SwitchOnTarget);
    m_ppmPowerStage = PpmPowerSequenceStage::None;
    m_ppmPowerSeqIndex = 0;
    m_ppmPendingTargetOnTract = -1;
    m_ppmSwitchNeedsPostUpdate = false;

    const bool canInteract = m_deviceController && m_deviceController->isConnected()
                             && !m_deviceController->isAwaitingTractPowerAck();
    setAllPpmRadiosEnabled(canInteract);

    // Если таймаут случился во время ручного переключения — возвращаем UI.
    if (wasSwitching) {
        if (ui && ui->progressBar) {
            ui->progressBar->setRange(0, 100);
            ui->progressBar->setValue(0);
            showStationHeaderCenter(StationHeaderCenter::FramePpm);
        }
        // Возвращаем checked на текущем включенном тракте (если известен).
        const int curIdx = m_ppmTractsSorted.indexOf(m_ppmCurrentOnTract);
        if (curIdx >= 0) {
            setPpmRadioUiState(curIdx, true, true);
        }
        updateTabWidgetLockState();
        return;
    }

    // Если были в сценарии после reboot — прогрессбар убираем, а PPM оставляем скрытым.
    if (ui && ui->progressBar && ui->progressBar->minimum() == 0 && ui->progressBar->maximum() == 0
        && !shouldKeepStationHeaderProgressVisible()) {
        ui->progressBar->setRange(0, 100);
        ui->progressBar->setValue(0);
        showStationHeaderCenter(StationHeaderCenter::StartButton);
    }

    // На всякий случай: если зависли со скрытым PPM вне reboot-сценария — возвращаем управление.
    if (ui && ui->framePPM && !ui->framePPM->isVisible() && m_ppmCurrentOnTract > 0
        && !shouldKeepStationHeaderProgressVisible()) {
        showStationHeaderCenter(StationHeaderCenter::FramePpm);
    }
    updateTabWidgetLockState();
}

void MainWindow::onPpmRadioClicked(int id)
{
    if (!m_deviceController || !m_deviceController->isConnected()) {
        return;
    }
    if (!m_ppmButtonGroup) {
        return;
    }
    if (m_deviceController->isAwaitingTractPowerAck() || m_ppmPowerStage != PpmPowerSequenceStage::None) {
        // Возвращаем UI к текущему включенному тракту.
        const int curIdx = m_ppmTractsSorted.indexOf(m_ppmCurrentOnTract);
        if (curIdx >= 0) {
            setPpmRadioUiState(curIdx, true, true);
        }
        return;
    }
    if (id < 0 || id >= m_ppmTractsSorted.size()) {
        return;
    }
    const int targetTract = m_ppmTractsSorted.at(id);
    if (targetTract <= 0) {
        return;
    }
    // Требование (tabFHSS): если щёлкнули по тракту, который уже включен (зелёный),
    // ничего не делаем — не сбрасываем диапазоны/запросы анализатора и не трогаем UI.
    // Иначе можно словить «схлопывание» диапазона до 0.5 МГц из хвостовых кадров sweep.
    if (targetTract == m_ppmCurrentOnTract) {
        const int curIdx = m_ppmTractsSorted.indexOf(m_ppmCurrentOnTract);
        if (curIdx >= 0) {
            setPpmRadioUiState(curIdx, true, true);
        }
        return;
    }

    // При смене выбранного тракта в PPM — полностью сбрасываем вкладку/график мощности под новый тракт,
    // чтобы не оставалось "продолжить тест" от предыдущего тракта.
    resetPowerTestUiForNewTractSelection(targetTract);
    // Тест приёма: останавливаем/очищаем результаты и подставляем частоты под новый тракт.
    resetReceiveTestUiForNewTractSelection(targetTract);
    // LCD/прочие readout-ы возвращаем в исходное состояние.
    resetPowerReadoutUi();
    // По ТЗ: начальные значения tabHands зависят от выбранного тракта.
    applyHandsDefaultsForTract(targetTract);

    // tabFHSS: при смене тракта сбрасываем ППРЧ-UI в дефолт (и очищаем maxhold),
    // чтобы не оставалось «замороженных» следов от предыдущего тракта.
    ++m_fhssResumeAfterPpmSerial;
    setFhssTestControlsIdle(true);
    m_fhssKeepMaxHoldUntilNextStart = false;
    m_fhssMaxHoldTract = -1;
    updateFhssModeComboForTract(targetTract);
    applyFhssXAxisForTract(targetTract);
    updateFhssRangeLcdForTract(targetTract);

    // Перерисовываем статус для выбранного тракта (если уже получали IND_ERROR).
    refreshPpmStatusUiForTract(targetTract);

    clearAllSelfIssuedGuards();
    startPpmSwitchToTract(targetTract);
}

bool MainWindow::shouldUpdatePowerReadoutForTract(uint8_t tractNum) const
{
    if (!ui || !ui->framePPM || !ui->framePPM->isVisible()) {
        return false;
    }
    if (m_ppmCurrentOnTract <= 0) {
        return false;
    }
    return static_cast<int>(tractNum) == m_ppmCurrentOnTract;
}

int MainWindow::resolveTabHandsIndex() const
{
    if (!ui || !ui->tabWidget) {
        return -1;
    }
    if (m_tabHandsIndex >= 0 && m_tabHandsIndex < ui->tabWidget->count()) {
        return m_tabHandsIndex;
    }
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
        QWidget *w = ui->tabWidget->widget(i);
        if (w && w->objectName() == QStringLiteral("tabHands")) {
            return i;
        }
    }
    return -1;
}

bool MainWindow::isPreTestingHandsOnlyPhase() const
{
    if (isActivePpmTestingSession()) {
        return false;
    }
    const bool ppmReady = (ui && ui->framePPM && ui->framePPM->isVisible() && m_ppmCurrentOnTract > 0);
    const bool switchingWithProgress =
        (ui && ui->progressBar && ui->progressBar->isVisible()
         && (m_ppmPowerStage == PpmPowerSequenceStage::SwitchOffCurrent
             || m_ppmPowerStage == PpmPowerSequenceStage::SwitchOffOthersBeforeOn
             || m_ppmPowerStage == PpmPowerSequenceStage::SwitchOnTarget));
    return !ppmReady || switchingWithProgress;
}

void MainWindow::leaveTabHandsIfBlocked()
{
    if (!ui || !ui->tabWidget || m_analyzerConnected) {
        return;
    }
    const int handsTabIndex = resolveTabHandsIndex();
    if (handsTabIndex < 0) {
        return;
    }
    // До «НАЧАТЬ ТЕСТИРОВАНИЕ» при выключенном анализаторе остаёмся на tabHands (вкладка заблокирована).
    if (isPreTestingHandsOnlyPhase()) {
        if (ui->tabWidget->currentIndex() != handsTabIndex) {
            ui->tabWidget->setCurrentIndex(handsTabIndex);
        }
        return;
    }
    if (ui->tabWidget->currentIndex() != handsTabIndex) {
        return;
    }

    int targetIndex = m_lastUnlockedTabIndex;
    if (targetIndex < 0 || targetIndex == handsTabIndex || targetIndex >= ui->tabWidget->count()
        || !ui->tabWidget->isTabEnabled(targetIndex)) {
        targetIndex = -1;
        for (int i = 0; i < ui->tabWidget->count(); ++i) {
            if (i != handsTabIndex && ui->tabWidget->isTabEnabled(i)) {
                targetIndex = i;
                break;
            }
        }
    }
    if (targetIndex >= 0 && targetIndex != handsTabIndex) {
        ui->tabWidget->setCurrentIndex(targetIndex);
    }
}

void MainWindow::applyAnalyzerHandsTabBlock()
{
    if (!ui || !ui->tabWidget || m_analyzerConnected) {
        return;
    }
    const int handsTabIndex = resolveTabHandsIndex();
    if (handsTabIndex < 0) {
        return;
    }
    ui->tabWidget->setTabEnabled(handsTabIndex, false);
    leaveTabHandsIfBlocked();
}

void MainWindow::updateTabWidgetLockState()
{
    if (!ui || !ui->tabWidget) {
        return;
    }

    if (m_stationDisconnectRecoveryActive) {
        for (int i = 0; i < ui->tabWidget->count(); ++i) {
            ui->tabWidget->setTabEnabled(i, true);
        }
        m_tabWidgetWasLocked = false;
        applyAnalyzerHandsTabBlock();
        return;
    }

    int handsTabIndex = resolveTabHandsIndex();
    if (handsTabIndex >= 0) {
        m_tabHandsIndex = handsTabIndex;
    }

    if (handsTabIndex < 0) {
        // Если вкладка "tabHands" не найдена, не рискуем блокировать все вкладки.
        for (int i = 0; i < ui->tabWidget->count(); ++i) {
            ui->tabWidget->setTabEnabled(i, true);
        }
        applyAnalyzerHandsTabBlock();
        return;
    }

    const bool powerTestRunningChecked =
        (ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked());
    // Пауза из-за тракта (ПП/АНТ/внешнее направление): вкладки держим как во время теста, пока статус не разрулен.
    const bool powerTestPausedByTractHold =
        m_powerTestPaused
        && (m_powerTestBlockedByPpm || m_powerTestBlockedByAntFault || m_powerTestBlockedByDirRestore
            || m_powerTestBlockedByAnalyzerDisconnect);
    const bool powerTestLocksToPowerTab = powerTestRunningChecked || powerTestPausedByTractHold;

    // Тест ППРЧ: пока активен (включая ожидание DirId=2 и внешнюю паузу по «Нет связи с ПП»/«Авария АНТ»),
    // блокируем все остальные вкладки и удерживаем пользователя на tabFHSS.
    // Делаем это ПЕРЕД проверкой powerTestLocksToPowerTab, чтобы внешняя пауза ППРЧ-теста
    // (m_fhssBlockedByPpm/m_fhssBlockedByAntFault) не «перепрыгивала» на tabPower.
    if (isFhssTestActive()) {
        int fhssTabIndex = m_tabFhssIndex;
        if (fhssTabIndex < 0 || fhssTabIndex >= ui->tabWidget->count()) {
            for (int i = 0; i < ui->tabWidget->count(); ++i) {
                QWidget *w = ui->tabWidget->widget(i);
                if (w && w->objectName() == QStringLiteral("tabFHSS")) {
                    fhssTabIndex = i;
                    break;
                }
            }
        }
        if (fhssTabIndex >= 0 && fhssTabIndex < ui->tabWidget->count()) {
            m_tabFhssIndex = fhssTabIndex;
            for (int i = 0; i < ui->tabWidget->count(); ++i) {
                ui->tabWidget->setTabEnabled(i, i == fhssTabIndex);
            }
            if (ui->tabWidget->currentIndex() != fhssTabIndex) {
                ui->tabWidget->setCurrentIndex(fhssTabIndex);
            }
            m_tabWidgetWasLocked = true;
            applyAnalyzerHandsTabBlock();
            return;
        }
    }

    // Во время теста мощности (tabPower) по ТЗ блокируем ВСЕ остальные вкладки,
    // чтобы их логика не вмешивалась в режим чередующихся запросов анализатора.
    if (powerTestLocksToPowerTab) {
        int powerTabIndex = m_tabPowerIndex;
        if (powerTabIndex < 0 || powerTabIndex >= ui->tabWidget->count()) {
            for (int i = 0; i < ui->tabWidget->count(); ++i) {
                QWidget *w = ui->tabWidget->widget(i);
                if (w && w->objectName() == QStringLiteral("tabPower")) {
                    powerTabIndex = i;
                    break;
                }
            }
        }
        if (powerTabIndex >= 0 && powerTabIndex < ui->tabWidget->count()) {
            m_tabPowerIndex = powerTabIndex;
        }

        // Важно: при приходе «Нет связи с ПП»/«Авария АНТ» во время работы на другой вкладке
        // (например, tabRecieve) не перебрасываем пользователя на tabPower — остаёмся на текущей вкладке,
        // но блокируем переключение вкладок, чтобы не ломать режим анализатора/теста.
        int keepIndex = ui->tabWidget->currentIndex();
        if (keepIndex < 0 || keepIndex >= ui->tabWidget->count()) {
            keepIndex = powerTabIndex;
        }
        for (int i = 0; i < ui->tabWidget->count(); ++i) {
            ui->tabWidget->setTabEnabled(i, i == keepIndex);
        }
        m_tabWidgetWasLocked = true;
        applyAnalyzerHandsTabBlock();
        return;
    }

    // Тест приёма: на время работы (и при автопаузе тракта) — только tabRecieve, как у теста мощности.
    // Исключение: ручная пауза без автопаузы тракта — можно переключить вкладку.
    const bool receiveTestLocksToReceiveTab =
        m_receiveTestRunning
        && (!m_receiveTestPaused || m_receiveTestAutoPausedByPpmNotReady
            || m_receiveTestAutoPausedByAnalyzerDisconnect);
    if (receiveTestLocksToReceiveTab) {
        int receiveTabIndex = m_tabReceiveIndex;
        if (receiveTabIndex < 0 || receiveTabIndex >= ui->tabWidget->count()) {
            for (int i = 0; i < ui->tabWidget->count(); ++i) {
                QWidget *w = ui->tabWidget->widget(i);
                if (w && w->objectName() == QStringLiteral("tabRecieve")) {
                    receiveTabIndex = i;
                    break;
                }
            }
        }
        if (receiveTabIndex >= 0 && receiveTabIndex < ui->tabWidget->count()) {
            m_tabReceiveIndex = receiveTabIndex;
            for (int i = 0; i < ui->tabWidget->count(); ++i) {
                ui->tabWidget->setTabEnabled(i, i == receiveTabIndex);
            }
            if (ui->tabWidget->currentIndex() != receiveTabIndex) {
                ui->tabWidget->setCurrentIndex(receiveTabIndex);
            }
            m_tabWidgetWasLocked = true;
            applyAnalyzerHandsTabBlock();
            return;
        }
    }

    const bool ppmReady = (ui->framePPM && ui->framePPM->isVisible() && m_ppmCurrentOnTract > 0);
    const bool switchingWithProgress =
        (ui->progressBar && ui->progressBar->isVisible() &&
         (m_ppmPowerStage == PpmPowerSequenceStage::SwitchOffCurrent ||
          m_ppmPowerStage == PpmPowerSequenceStage::SwitchOffOthersBeforeOn ||
          m_ppmPowerStage == PpmPowerSequenceStage::SwitchOnTarget));
    const bool lockNonHandsTabs = !ppmReady || switchingWithProgress;

    if (!lockNonHandsTabs && !m_tabWidgetWasLocked) {
        const int cur = ui->tabWidget->currentIndex();
        if (cur >= 0) {
            m_lastUnlockedTabIndex = cur;
        }
    }

    for (int i = 0; i < ui->tabWidget->count(); ++i) {
        if (i == handsTabIndex) {
            ui->tabWidget->setTabEnabled(i, m_analyzerConnected);
            continue;
        }
        ui->tabWidget->setTabEnabled(i, !lockNonHandsTabs);
    }

    if (lockNonHandsTabs && ui->tabWidget->currentIndex() != handsTabIndex) {
        const int cur = ui->tabWidget->currentIndex();
        if (cur >= 0 && cur != handsTabIndex) {
            m_lastUnlockedTabIndex = cur;
        }
        ui->tabWidget->setCurrentIndex(handsTabIndex);
        m_tabWidgetWasLocked = true;
        applyAnalyzerHandsTabBlock();
        return;
    }

    if (!lockNonHandsTabs && m_tabWidgetWasLocked
        && ui->tabWidget->currentIndex() == handsTabIndex
        && !(m_receiveTestRunning && m_receiveTestPaused)) {
        // После разблокировки из режима «только tabHands пока PPM не готов» — перейти на tabPower.
        // Не вызывать при снятии блокировки «только tabRecieve» (ручная пауза теста приёма): пользователь
        // остаётся на tabRecieve, но m_tabWidgetWasLocked ещё true — раньше ошибочно срабатывал этот переход.
        int targetIndex = m_tabPowerIndex;
        if (targetIndex < 0 || targetIndex >= ui->tabWidget->count()) {
            for (int i = 0; i < ui->tabWidget->count(); ++i) {
                QWidget *w = ui->tabWidget->widget(i);
                if (w && w->objectName() == QStringLiteral("tabPower")) {
                    targetIndex = i;
                    break;
                }
            }
        }
        if (targetIndex >= 0 && targetIndex < ui->tabWidget->count()) {
            m_tabPowerIndex = targetIndex;
        }
        if (targetIndex >= 0 && targetIndex < ui->tabWidget->count() && ui->tabWidget->isTabEnabled(targetIndex)) {
            ui->tabWidget->setCurrentIndex(targetIndex);
            if (QWidget *w = ui->tabWidget->widget(targetIndex)) {
                w->setFocus(Qt::OtherFocusReason);
            }
        }
    }

    m_tabWidgetWasLocked = lockNonHandsTabs;
    applyAnalyzerHandsTabBlock();
}

void MainWindow::requestRecoveryIndicationsAfterReconnect()
{
    if (!m_deviceController || !m_deviceController->isConnected()) {
        return;
    }

    QVector<int> tracts;
    auto addUniqueTract = [&tracts](int tr) {
        if (tr <= 0 || tr > 255 || tracts.contains(tr)) {
            return;
        }
        tracts.push_back(tr);
    };

    addUniqueTract(selectedPpmTractFromUi());
    addUniqueTract(m_ppmCurrentOnTract);
    addUniqueTract(static_cast<int>(m_powerTestTargetTract));
    addUniqueTract(m_receiveTestTract);
    addUniqueTract(m_fhssTract);
    if (tracts.isEmpty()) {
        addUniqueTract(ppmFirstTractNumber());
    }

    for (int tr : tracts) {
        m_deviceController->requestAllIndications(static_cast<uint8_t>(tr));
    }
}

namespace {

struct RxGenLevel {
    int dbm;
    quint8 pow;
    const char *title;
};

constexpr RxGenLevel kRxLevels[] = {
    {  -8, static_cast<quint8>(0x30), "-8"  },
    { -11, static_cast<quint8>(0x31), "-11" },
    { -14, static_cast<quint8>(0x32), "-14" },
    { -17, static_cast<quint8>(0x33), "-17" },
    { -20, static_cast<quint8>(0x34), "-20" },
    { -23, static_cast<quint8>(0x35), "-23" },
    { -26, static_cast<quint8>(0x36), "-26" },
    { -29, static_cast<quint8>(0x37), "-29" },
};
constexpr int kRxLevelsCount = static_cast<int>(sizeof(kRxLevels) / sizeof(kRxLevels[0]));
constexpr int kRxLevelDurationMs = 5000;

// Частоты теста приёма (tabRecieve) зависят от TrmType выбранного тракта ППМ (framePPM).
const QVector<quint64> kRxTestFrequenciesTrmType2Hz = {
    30025000ULL, 34025000ULL, 38525000ULL, 45525000ULL, 52225000ULL, 62525000ULL, 72525000ULL,
    85025000ULL, 95025000ULL, 118525000ULL, 137025000ULL, 157025000ULL, 179975000ULL
};
const QVector<quint64> kRxTestFrequenciesTrmType3Hz = {
    220025000ULL, 270025000ULL, 300025000ULL, 340025000ULL, 380025000ULL, 440025000ULL, 469975000ULL
};
const QVector<quint64> kRxTestFrequenciesTrmType4Hz = {
    520025000ULL, 630025000ULL, 720025000ULL, 847525000ULL, 965025000ULL,
    1117525000ULL, 1249975000ULL, 1850025000ULL, 2100025000ULL, 2499025000ULL
};

static QVector<quint64> receiveTestFrequenciesHzForTrmType(int trmType)
{
    switch (trmType) {
    case 3: return kRxTestFrequenciesTrmType3Hz;
    case 4: return kRxTestFrequenciesTrmType4Hz;
    case 2: return kRxTestFrequenciesTrmType2Hz;
    default: return kRxTestFrequenciesTrmType2Hz;
    }
}

/** Связки указателей по дереву виджетов разметки ReceiveResultStrip.ui. */
static ReceiveResultStripUi bindingsForReceiveStripRoot(QFrame *root)
{
    ReceiveResultStripUi W;
    W.frame = root;
    W.baselineValue = nullptr;
    W.freqTestLcd = root->findChild<QLCDNumber *>(QStringLiteral("labelRecieveFreqTest"));
    W.rssiValue = root->findChild<QLabel *>(QStringLiteral("labelRecieveRSSIValue"));
    W.levelIndicators[0] = root->findChild<QWidget *>(QStringLiteral("labelRecieve225LvlM8"));
    W.levelIndicators[1] = root->findChild<QWidget *>(QStringLiteral("labelRecieve225LvlM11"));
    W.levelIndicators[2] = root->findChild<QWidget *>(QStringLiteral("labelRecieve225LvlM14"));
    W.levelIndicators[3] = root->findChild<QWidget *>(QStringLiteral("labelRecieve225LvlM17"));
    W.levelIndicators[4] = root->findChild<QWidget *>(QStringLiteral("labelRecieve225LvlM20"));
    W.levelIndicators[5] = root->findChild<QWidget *>(QStringLiteral("labelRecieve225LvlM23"));
    W.levelIndicators[6] = root->findChild<QWidget *>(QStringLiteral("labelRecieve225LvlM26"));
    W.levelIndicators[7] = root->findChild<QWidget *>(QStringLiteral("labelRecieve225LvlM29"));
    W.resultValue = root->findChild<QLabel *>(QStringLiteral("labelRecieveResultValue"));
    W.statusTestFinishOk = root->findChild<QLabel *>(QStringLiteral("labelStatusTestFinishOk"));
    W.statusTestFinishNot = root->findChild<QLabel *>(QStringLiteral("labelStatusTestFinishNot"));
    return W;
}

static QIcon receiveBlackIconPause()
{
    QPixmap pm(22, 22);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(QColor(QStringLiteral("#2563eb")));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(5, 4, 4, 14, 1, 1);
    p.drawRoundedRect(13, 4, 4, 14, 1, 1);
    return QIcon(pm);
}

static QIcon receiveBlackIconPlay()
{
    QPixmap pm(22, 22);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPolygon poly;
    poly << QPoint(6, 4) << QPoint(18, 11) << QPoint(6, 18);
    p.setBrush(QColor(QStringLiteral("#2563eb")));
    p.setPen(Qt::NoPen);
    p.drawPolygon(poly);
    return QIcon(pm);
}

static QIcon receiveBlackIconStop()
{
    QPixmap pm(22, 22);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(QColor(QStringLiteral("#2563eb")));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(5, 5, 12, 12, 2, 2);
    return QIcon(pm);
}

} // namespace

QVector<quint64> MainWindow::receiveTestFrequenciesHzForTract(int tractNum) const
{
    return receiveTestFrequenciesHzForTrmType(ppmTrmTypeForTract(tractNum));
}

void MainWindow::resetReceiveTestUiForNewTractSelection(int targetTract)
{
    if (!ui) {
        return;
    }

    // При смене тракта: тест приёма должен быть полностью остановлен и UI очищен,
    // чтобы не оставалось результатов/состояний от предыдущего тракта.
    if (m_receiveTestRunning) {
        tearDownReceiveTest(true);
    } else {
        setReceiveTestControlsIdle();
        resetReceiveReadoutUi();
        setEmissionAnimating(false);
    }

    // Подготовим “пустые” полоски результатов под частоты выбранного тракта.
    m_receiveTestFreqsHz = receiveTestFrequenciesHzForTract(targetTract);
    ensureReceiveResultStripsBuilt();
    m_receiveFreqAllLevelsOk = QVector<bool>(m_receiveTestFreqsHz.size(), true);
    m_receiveFreqBaselineRssiDbm = QVector<int>(m_receiveTestFreqsHz.size(), 0);
    syncReceiveStripFreqTestLabels();
    updateReceiveResultStripsVisibility();
    updateReceiveTestButtonsAccessForSelectedTract();
}

void MainWindow::initReceiveTestingUi()
{
    if (!ui) {
        return;
    }
    m_receiveTestIconPause = receiveBlackIconPause();
    m_receiveTestIconPlay = receiveBlackIconPlay();
    m_receiveTestIconStop = receiveBlackIconStop();

    if (ui->pushButtonStartTestingRecieve) {
        ui->pushButtonStartTestingRecieve->setCheckable(false);
        ui->pushButtonStartTestingRecieve->setAutoDefault(false);
        ui->pushButtonStartTestingRecieve->setDefault(false);
        connect(ui->pushButtonStartTestingRecieve, &QPushButton::clicked,
                this, &MainWindow::onReceiveTestStartClicked);
    }
    if (ui->pushButtonRecieveTestPause) {
        ui->pushButtonRecieveTestPause->setIcon(m_receiveTestIconPause);
        setPauseButtonMode(ui->pushButtonRecieveTestPause, /*isPlayIcon=*/false);
        connect(ui->pushButtonRecieveTestPause, &QPushButton::clicked,
                this, &MainWindow::onReceiveTestPauseClicked);
    }
    if (ui->pushButtonRecieveTestStop) {
        ui->pushButtonRecieveTestStop->setIcon(m_receiveTestIconStop);
        connect(ui->pushButtonRecieveTestStop, &QPushButton::clicked,
                this, &MainWindow::onReceiveTestStopClicked);
    }
    syncReceiveTabPreviewFromCurrentTract();
    resetReceiveReadoutUi();
    updateReceiveTestButtonsAccessForSelectedTract();
}

namespace {

static QString indicatorBoxStyle(const QString &fg, const QString &bg, const QString &border)
{
    return QStringLiteral("color: %1; background-color: %2; border: 1px solid %3; border-radius: 6px; padding: 2px 2px; font-family: Consolas; font-weight: bold;")
        .arg(fg, bg, border);
}

static const QString kReceiveIndicatorPendingStyle =
    indicatorBoxStyle(QStringLiteral("#94a3b8"), QStringLiteral("#0f172a"), QStringLiteral("#334155"));

static QString receiveLevelProgressBarStyle(const QString &textColor, const QString &chunkCss)
{
    return QStringLiteral(
        "QProgressBar {"
        " color: %1;"
        " text-align: center;"
        " border: 1px solid #334155;"
        " border-radius: 10px;"
        " background-color: #0f172a;"
        "}"
        "QProgressBar::chunk {"
        " border-radius: 10px;"
        " %2"
        "}"
    ).arg(textColor, chunkCss);
}

static void applyIndicatorStyle(QWidget *w, const QString &text, const QString &style, int minWidth = -1)
{
    if (!w) return;
    if (style == kReceiveIndicatorPendingStyle) {
        if (auto *lbl = qobject_cast<QLabel *>(w)) {
            lbl->setText(text);
            lbl->setStyleSheet(QString());
            if (minWidth >= 0) {
                lbl->setMinimumWidth(minWidth);
            }
            return;
        }
        if (auto *pb = qobject_cast<QProgressBar *>(w)) {
            pb->setTextVisible(true);
            pb->setFormat(text);
            pb->setStyleSheet(receiveLevelProgressBarStyle(
                QStringLiteral("#94a3b8"),
                QStringLiteral("background-color: #0f172a;")));
            if (minWidth >= 0) {
                pb->setMinimumWidth(minWidth);
            }
            return;
        }
    }
    if (auto *lbl = qobject_cast<QLabel *>(w)) {
        lbl->setText(text);
        lbl->setStyleSheet(style);
        if (minWidth >= 0) {
            lbl->setMinimumWidth(minWidth);
        }
        return;
    }
    if (auto *pb = qobject_cast<QProgressBar *>(w)) {
        // Для прогресс-бара используем формат как "текст" (вместо QLabel::setText).
        pb->setTextVisible(true);
        pb->setFormat(text);
        // indicatorBoxStyle() сформирован под QLabel. Преобразуем в стиль для QProgressBar.
        // Извлекаем основные цвета (fg/bg/border) из строки.
        QString fg = QStringLiteral("#94a3b8");
        QString bg = QStringLiteral("#0f172a");
        QString border = QStringLiteral("#334155");
        {
            static const QRegularExpression reColor(QStringLiteral("color:\\s*([^;]+);"));
            static const QRegularExpression reBg(QStringLiteral("background-color:\\s*([^;]+);"));
            static const QRegularExpression reBorder(QStringLiteral("border:\\s*1px\\s+solid\\s+([^;]+);"));
            if (const auto m = reColor.match(style); m.hasMatch()) fg = m.captured(1).trimmed();
            if (const auto m = reBg.match(style); m.hasMatch()) bg = m.captured(1).trimmed();
            if (const auto m = reBorder.match(style); m.hasMatch()) border = m.captured(1).trimmed();
        }
        // Хотим базовый вид 1-в-1 как у progressBarRecieve:
        // рамка/фон заданы в mainwindow.ui / receiveresultstrip.ui.
        // Поэтому здесь переопределяем только цвет текста и заливку chunk под состояние.
        //
        const bool isRun = (bg.compare(QStringLiteral("#38bdf8"), Qt::CaseInsensitive) == 0);
        // Цвет текста:
        // - RUN: хотим серый как у исходных лейблов (не тёмный)
        // - иначе: берём fg из indicatorBoxStyle (PASS/FAIL останется тёмным как раньше)
        const QString textColor = isRun ? QStringLiteral("#94a3b8") : fg;

        // Полоска (chunk):
        // - RUN: делаем как у progressBarRecieve (синий градиент)
        // - иначе: используем цвет из indicatorBoxStyle (зелёный/красный и т.п.)
        const QString chunkStyle = isRun
            ? QStringLiteral(
                "background-color: qlineargradient("
                " x1:0, y1:0, x2:1, y2:0,"
                " stop:0 #2563eb,"
                " stop:1 #3b82f6"
                ");"
            )
            : QStringLiteral("background-color: %1;").arg(bg);

        pb->setStyleSheet(receiveLevelProgressBarStyle(textColor, chunkStyle));
        if (minWidth >= 0) {
            pb->setMinimumWidth(minWidth);
        }
        return;
    }
    // fallback: только стиль/ширина
    w->setStyleSheet(style);
    if (minWidth >= 0) {
        w->setMinimumWidth(minWidth);
    }
}

static void hideReceiveFinishIcon(QLabel *lbl)
{
    if (!lbl) return;
    lbl->setVisible(false);
}

static void showReceiveFinishIcons(QLabel *okLbl, QLabel *notLbl, bool ok)
{
    if (okLbl) okLbl->setVisible(ok);
    if (notLbl) notLbl->setVisible(!ok);
}
} // namespace

void MainWindow::resetReceiveReadoutUi()
{
    if (!ui) {
        return;
    }
    if (ui->lcdRecieveFreqValue) {
        ui->lcdRecieveFreqValue->display(QStringLiteral("----"));
    }
    if (ui->lcdRecieveRSSIValue) {
        ui->lcdRecieveRSSIValue->display(QStringLiteral("----"));
    }

    const QString pendingStyle =
        indicatorBoxStyle(QStringLiteral("#94a3b8"), QStringLiteral("#0f172a"), QStringLiteral("#334155"));
    if (m_receiveResultStripsBuilt) {
        for (int fi = 0; fi < m_receiveResultStrips.size(); ++fi) {
            const ReceiveResultStripUi &s = m_receiveResultStrips[fi];
            if (s.baselineValue) {
                s.baselineValue->setText(QStringLiteral("—"));
            }
            if (s.rssiValue) {
                s.rssiValue->setText(QStringLiteral("—"));
            }
            if (s.resultValue) {
                s.resultValue->setText(QStringLiteral("—"));
                s.resultValue->setStyleSheet(QString());
            }
            hideReceiveFinishIcon(s.statusTestFinishOk);
            hideReceiveFinishIcon(s.statusTestFinishNot);
            for (int li = 0; li < kRxLevelsCount; ++li) {
                if (s.levelIndicators[li]) {
                    if (auto *pb = qobject_cast<QProgressBar *>(s.levelIndicators[li])) {
                        pb->setRange(0, 100);
                        pb->setValue(0);
                    }
                    applyIndicatorStyle(s.levelIndicators[li],
                                        QString::fromLatin1(kRxLevels[li].title),
                                        pendingStyle);
                }
            }
        }
    }

    // Единый progressBar для tabRecieve (внутри frameRecieveSettings).
    if (ui->progressBarRecieve) {
        ui->progressBarRecieve->setVisible(false);
        ui->progressBarRecieve->setTextVisible(false);
        ui->progressBarRecieve->setRange(0, 100);
        ui->progressBarRecieve->setValue(0);
    }

    syncReceiveStripFreqTestLabels();
    updateReceiveResultStripsVisibility();
}

void MainWindow::ensureReceiveResultStripsBuilt()
{
    if (!ui) {
        return;
    }

    auto *vlay = qobject_cast<QVBoxLayout *>(ui->scrollAreaWidgetContentsRecieve->layout());
    if (!vlay) {
        return;
    }

    const int wantCount = m_receiveTestFreqsHz.size();
    if (wantCount <= 0) {
        return;
    }

    // Если полоски уже собраны, но количество частот изменилось (смена тракта), пересобираем.
    if (m_receiveResultStripsBuilt && m_receiveResultStrips.size() == wantCount) {
        return;
    }

    QWidget *parentW = ui->scrollAreaWidgetContentsRecieve;

    // Удаляем ранее созданные полоски (если пересборка).
    if (!m_receiveResultStrips.isEmpty()) {
        for (const ReceiveResultStripUi &s : m_receiveResultStrips) {
            if (s.frame) {
                vlay->removeWidget(s.frame);
                s.frame->deleteLater();
            }
        }
    }
    m_receiveResultStrips.clear();

    // Важно: индекс spacer'а нельзя вычислять до удаления старых полосок —
    // после removeWidget индексы сдвигаются, и новые полоски могут вставиться после spacer'а (в самый низ).
    // В layout tabRecieve spacer должен быть последним элементом, поэтому вставляем перед последним item.
    int insertPos = vlay->count();
    if (insertPos > 0) {
        // Если последний item — spacer, то insertPos=count()-1 вставит прямо перед ним.
        insertPos = vlay->count() - 1;
    }

    for (int fi = 0; fi < wantCount; ++fi) {
        auto *nf = new QFrame(parentW);
        Ui::ReceiveResultStrip stripUi;
        stripUi.setupUi(nf);
        ReceiveResultStripUi sx = bindingsForReceiveStripRoot(nf);
        if (sx.freqTestLcd) {
            sx.freqTestLcd->display(formatGroupedWithDots(static_cast<uint32_t>(m_receiveTestFreqsHz[fi])));
        }
        hideReceiveFinishIcon(sx.statusTestFinishOk);
        hideReceiveFinishIcon(sx.statusTestFinishNot);
        m_receiveResultStrips.push_back(sx);
        vlay->insertWidget(insertPos, nf);
        nf->setVisible(false);
        ++insertPos;
    }

    m_receiveResultStripsBuilt = true;
    syncReceiveStripFreqTestLabels();
}

QLabel *MainWindow::receiveStripResultLabel(int freqIndex) const
{
    if (freqIndex < 0 || freqIndex >= m_receiveResultStrips.size()) {
        return nullptr;
    }
    return m_receiveResultStrips[freqIndex].resultValue;
}

void MainWindow::syncReceiveStripFreqTestLabels()
{
    if (!ui) {
        return;
    }
    if (!m_receiveResultStripsBuilt || m_receiveResultStrips.isEmpty()) {
        return;
    }
    const int n = qMin(m_receiveResultStrips.size(), m_receiveTestFreqsHz.size());
    for (int i = 0; i < n; ++i) {
        if (m_receiveResultStrips[i].freqTestLcd) {
            m_receiveResultStrips[i].freqTestLcd->display(
                formatGroupedWithDots(static_cast<uint32_t>(m_receiveTestFreqsHz[i])));
        }
    }
}

void MainWindow::syncReceiveTabPreviewFromCurrentTract()
{
    if (!ui || m_receiveTestRunning) {
        return;
    }
    int tr = (m_ppmCurrentOnTract > 0) ? m_ppmCurrentOnTract : selectedPpmTractFromUi();
    if (tr <= 0) {
        tr = ppmFirstTractNumber();
    }
    if (tr <= 0) {
        return;
    }
    m_receiveTestFreqsHz = receiveTestFrequenciesHzForTract(tr);
    ensureReceiveResultStripsBuilt();
    syncReceiveStripFreqTestLabels();
}

void MainWindow::syncAnalyzerKeepAliveForCurrentTab()
{
    if (!m_analyzerController) {
        return;
    }
    int intervalMs = kAnalyzerKeepAliveMsDefault;
    bool onReceiveTab = false;
    if (m_tabReceiveIndex >= 0 && ui && ui->tabWidget) {
        onReceiveTab = (ui->tabWidget->currentIndex() == m_tabReceiveIndex);
    } else if (ui && ui->tabWidget) {
        QWidget *cw = ui->tabWidget->currentWidget();
        onReceiveTab = cw && cw->objectName() == QLatin1String("tabRecieve");
    }
    if (onReceiveTab && !m_receiveTestRunning) {
        intervalMs = kAnalyzerKeepAliveMsReceiveTabIdle;
    }
    m_analyzerController->setKeepAliveIntervalMs(intervalMs);
}

int MainWindow::receiveTestOverallProgressPercent() const
{
    if (!m_receiveTestRunning || m_receiveTestFreqsHz.isEmpty()) {
        return 0;
    }
    const int totalSteps = m_receiveTestFreqsHz.size() * kRxLevelsCount;
    if (totalSteps <= 0) {
        return 0;
    }
    if (m_receivePhase == ReceiveTestPhase::WaitBaseline) {
        const int completedSteps = m_receiveFreqIndex * kRxLevelsCount;
        return qBound(0, (completedSteps * 100) / totalSteps, 100);
    }
    if (m_receivePhase == ReceiveTestPhase::RunningLevel) {
        const int stepIndex = m_receiveFreqIndex * kRxLevelsCount + m_receiveLevelIndex;
        const double startPct = (100.0 * static_cast<double>(stepIndex)) / static_cast<double>(totalSteps);
        if (m_receiveTestPaused) {
            // На паузе текущий уровень уже сброшен — показываем начало шага, не «замороженный» прогресс.
            return qBound(0, static_cast<int>(qRound(startPct)), 100);
        }
        const qint64 ms = m_receiveTestElapsed.elapsed();
        const double levelFrac = qMin(1.0, static_cast<double>(ms) / static_cast<double>(kRxLevelDurationMs));
        const double endPct = (100.0 * static_cast<double>(stepIndex + 1)) / static_cast<double>(totalSteps);
        const int v = static_cast<int>(qRound(startPct + levelFrac * (endPct - startPct)));
        return qBound(0, v, 100);
    }
    return 0;
}

void MainWindow::updateReceiveResultStripsVisibility()
{
    if (!ui) {
        return;
    }
    if (!m_receiveResultStripsBuilt || m_receiveResultStrips.isEmpty()) {
        return;
    }
    bool receiveTabActive = false;
    if (m_tabReceiveIndex >= 0 && ui->tabWidget) {
        receiveTabActive = (ui->tabWidget->currentIndex() == m_tabReceiveIndex);
    } else if (ui->tabWidget) {
        QWidget *cw = ui->tabWidget->currentWidget();
        receiveTabActive = cw && cw->objectName() == QLatin1String("tabRecieve");
    }
    const bool showAllStrips = receiveTabActive || m_receiveTestRunning;
    for (int i = 0; i < m_receiveResultStrips.size(); ++i) {
        if (m_receiveResultStrips[i].frame) {
            m_receiveResultStrips[i].frame->setVisible(showAllStrips);
        }
    }

    if (ui->progressBarRecieve) {
        ui->progressBarRecieve->setRange(0, 100);
        ui->progressBarRecieve->setFormat(QStringLiteral("%p%"));
        if (!m_receiveTestRunning) {
            ui->progressBarRecieve->setVisible(false);
            ui->progressBarRecieve->setTextVisible(false);
            ui->progressBarRecieve->setValue(0);
        } else if (m_receiveTestPaused) {
            ui->progressBarRecieve->setVisible(true);
            ui->progressBarRecieve->setTextVisible(true);
            ui->progressBarRecieve->setValue(receiveTestOverallProgressPercent());
        } else if (m_receivePhase == ReceiveTestPhase::WaitBaseline
                   || m_receivePhase == ReceiveTestPhase::RunningLevel) {
            ui->progressBarRecieve->setVisible(true);
            ui->progressBarRecieve->setTextVisible(true);
            ui->progressBarRecieve->setValue(receiveTestOverallProgressPercent());
        } else {
            ui->progressBarRecieve->setVisible(false);
            ui->progressBarRecieve->setTextVisible(false);
        }
    }
}

void MainWindow::setReceiveTestControlsIdle()
{
    if (!ui) {
        return;
    }
    m_receiveTestPaused = false;
    if (ui->pushButtonStartTestingRecieve) {
        ui->pushButtonStartTestingRecieve->setVisible(true);
    }
    if (ui->pushButtonRecieveTestPause) {
        ui->pushButtonRecieveTestPause->setVisible(false);
        ui->pushButtonRecieveTestPause->setIcon(m_receiveTestIconPause);
        setPauseButtonMode(ui->pushButtonRecieveTestPause, /*isPlayIcon=*/false);
    }
    if (ui->pushButtonRecieveTestStop) {
        ui->pushButtonRecieveTestStop->setVisible(false);
    }
    m_receiveTestAutoPausedByPpmNotReady = false;
    m_receiveTestAutoPausedByAnalyzerDisconnect = false;
    updateReceiveTestButtonsAccessForSelectedTract();
}

void MainWindow::setReceiveTestControlsRunning(bool playbackPaused)
{
    if (!ui) {
        return;
    }
    if (ui->pushButtonStartTestingRecieve) {
        ui->pushButtonStartTestingRecieve->setVisible(false);
    }
    if (ui->pushButtonRecieveTestPause) {
        ui->pushButtonRecieveTestPause->setVisible(true);
        ui->pushButtonRecieveTestPause->setIcon(playbackPaused ? m_receiveTestIconPlay : m_receiveTestIconPause);
        setPauseButtonMode(ui->pushButtonRecieveTestPause, /*isPlayIcon=*/playbackPaused);
    }
    if (ui->pushButtonRecieveTestStop) {
        ui->pushButtonRecieveTestStop->setVisible(true);
    }
    updateReceiveTestButtonsAccessForSelectedTract();
}

void MainWindow::tearDownReceiveTest(bool generatorOff)
{
    setEmissionAnimating(false);
    m_receiveTestTickTimer.stop();
    m_receiveTestPaused = false;
    m_receiveTestRunning = false;
    m_receiveTestAutoPausedByPpmNotReady = false;
    m_receiveTestAutoPausedByAnalyzerDisconnect = false;
    m_receivePhase = ReceiveTestPhase::Idle;
    m_receiveTestTract = -1;
    m_receiveFreqIndex = 0;
    m_receiveLevelIndex = 0;
    m_receiveTestFreqHz = 0;
    m_receiveTestPow = 0;
    m_receiveTestPowDbm = 0;
    m_receiveLastRssiDbm = 0;
    m_receiveLastRssiDbmFull = 0.0;
    m_receiveLevelMaxRssiDbm = -9999;

    if (generatorOff && m_analyzerController) {
        m_analyzerController->setGenerator(quint64(0), /*state*/ 0, quint8(0));
    }

    setReceiveTestControlsIdle();

    resetReceiveReadoutUi();
    syncAnalyzerKeepAliveForCurrentTab();
    updateTabWidgetLockState();
}

void MainWindow::onReceiveTestStartClicked()
{
    if (!ui) {
        return;
    }

    // По ТЗ: тест приёма нельзя стартовать/продолжать, если выбранный тракт не готов (статус + зелёная рамка).
    updateReceiveTestButtonsAccessForSelectedTract();
    if (ui->pushButtonStartTestingRecieve && !ui->pushButtonStartTestingRecieve->isEnabled()) {
        return;
    }

    if (!m_deviceController || !m_deviceController->isConnected()) {
        onDeviceLogMessage(QStringLiteral("ОШИБКА: нет подключения к радиостанции (тест приёма)."));
        return;
    }
    if (!m_analyzerController || !m_analyzerController->isConnected()) {
        onDeviceLogMessage(QStringLiteral("ОШИБКА: анализатор не подключён (тест приёма)."));
        return;
    }

    resetReceiveReadoutUi();

    const int tr = (m_ppmCurrentOnTract > 0) ? m_ppmCurrentOnTract : selectedPpmTractFromUi();
    if (tr <= 0) {
        onDeviceLogMessage(QStringLiteral("ОШИБКА: не выбран тракт ППМ (тест приёма)."));
        return;
    }

    m_receiveTestFreqsHz = receiveTestFrequenciesHzForTract(tr);
    ensureReceiveResultStripsBuilt();
    m_receiveFreqAllLevelsOk = QVector<bool>(m_receiveTestFreqsHz.size(), true);

    m_receiveTestPaused = false;
    m_receiveTestAutoPausedByPpmNotReady = false;
    m_receiveTestAutoPausedByAnalyzerDisconnect = false;
    m_receiveTestTract = tr;
    m_receiveTestRunning = true;
    m_receivePhase = ReceiveTestPhase::WaitBaseline;
    m_receiveFreqIndex = 0;
    m_receiveLevelIndex = 0;
    m_receiveTestFreqHz = m_receiveTestFreqsHz[m_receiveFreqIndex];
    m_receiveBaselineRssiDbm = 0;
    m_receiveLevelMaxRssiDbm = -9999;
    m_receiveLastRssiDbm = m_lastRssiDbmByTract.value(tr, 0);
    m_receiveLastRssiDbmFull = static_cast<double>(m_receiveLastRssiDbm);
    m_receiveFreqBaselineRssiDbm = QVector<int>(m_receiveTestFreqsHz.size(), 0);

    syncReceiveStripFreqTestLabels();
    setReceiveTestControlsRunning(false);

    if (ui->lcdRecieveFreqValue) {
        ui->lcdRecieveFreqValue->display(formatGroupedWithDots(static_cast<uint32_t>(m_receiveTestFreqHz)));
    }

    updateReceiveResultStripsVisibility();

    if (!m_deviceController->setFrequencyRx(static_cast<uint8_t>(tr), static_cast<uint32_t>(m_receiveTestFreqHz))) {
        tearDownReceiveTest(true);
        onDeviceLogMessage(QStringLiteral("ОШИБКА: не удалось установить RX частоту %1 Гц (тест приёма).")
                               .arg(formatGroupedWithDots(static_cast<uint32_t>(m_receiveTestFreqHz))));
        return;
    }

    syncAnalyzerKeepAliveForCurrentTab();
    // progressBar живёт внутри ReceiveResultStrip и управляется updateReceiveResultStripsVisibility/onReceiveTestTick.
    updateTabWidgetLockState();
    onDeviceLogMessage(QStringLiteral("▶ Начат тест приёма на тракте %1.")
                           .arg(receiveTestTractDisplayNameForLog()));
}

void MainWindow::onReceiveTestPauseClicked()
{
    if (!ui || !m_receiveTestRunning) {
        return;
    }

    if (!m_receiveTestPaused) {
        m_receiveTestPaused = true;
        m_receiveTestAutoPausedByPpmNotReady = false; // ручная пауза не должна авто-возобновляться
        m_receiveTestAutoPausedByAnalyzerDisconnect = false;
        m_receiveTestTickTimer.stop();
        if (m_receivePhase == ReceiveTestPhase::RunningLevel && m_analyzerController) {
            m_analyzerController->setGenerator(m_receiveTestFreqHz, /*state*/ 0, m_receiveTestPow);
            setEmissionAnimating(false);
        }
        restartInterruptedReceiveLevelTest();
        setReceiveTestControlsRunning(true);
        updateReceiveResultStripsVisibility();
        updateTabWidgetLockState();
        onDeviceLogMessage(QStringLiteral("⏸ Тест приёма на тракте %1 поставлен на паузу.")
                               .arg(receiveTestTractDisplayNameForLog()));
        return;
    }

    m_receiveTestPaused = false;
    setReceiveTestControlsRunning(false);
    resumeReceiveLevelTestAfterPause();
    updateReceiveResultStripsVisibility();
    updateTabWidgetLockState();
}

void MainWindow::onReceiveTestStopClicked()
{
    if (m_receiveTestRunning) {
        onDeviceLogMessage(QStringLiteral("⏹ Тест приёма на тракте %1 остановлен.")
                               .arg(receiveTestTractDisplayNameForLog()));
    }
    tearDownReceiveTest(true);
}

void MainWindow::restartInterruptedReceiveLevelTest()
{
    if (!m_receiveTestRunning || m_receivePhase != ReceiveTestPhase::RunningLevel) {
        return;
    }
    if (m_receiveFreqIndex < 0 || m_receiveFreqIndex >= m_receiveResultStrips.size()) {
        return;
    }
    if (m_receiveLevelIndex < 0 || m_receiveLevelIndex >= kRxLevelsCount) {
        return;
    }

    ReceiveResultStripUi &strip = m_receiveResultStrips[m_receiveFreqIndex];

    m_receiveTestPowDbm = kRxLevels[m_receiveLevelIndex].dbm;
    m_receiveTestPow = kRxLevels[m_receiveLevelIndex].pow;
    m_receiveLevelMaxRssiDbm = -9999;
    m_receiveTestElapsed.restart();

    if (auto *pb = qobject_cast<QProgressBar *>(strip.levelIndicators[m_receiveLevelIndex])) {
        pb->setRange(0, 100);
        pb->setValue(0);
    }

    const QString runStyle = indicatorBoxStyle("#0f172a", "#38bdf8", "#38bdf8");
    applyIndicatorStyle(strip.levelIndicators[m_receiveLevelIndex],
                        QString::fromLatin1(kRxLevels[m_receiveLevelIndex].title),
                        runStyle);

    if (strip.resultValue) {
        strip.resultValue->setText(QStringLiteral("—"));
        strip.resultValue->setStyleSheet(QString());
    }
}

void MainWindow::resumeReceiveLevelTestAfterPause()
{
    if (!m_receiveTestRunning || m_receiveTestPaused) {
        return;
    }
    if (m_receivePhase != ReceiveTestPhase::RunningLevel) {
        return;
    }
    restartInterruptedReceiveLevelTest();
    m_receiveTestTickTimer.start();
    if (m_analyzerController) {
        m_analyzerController->setGenerator(m_receiveTestFreqHz, /*state*/ 1, m_receiveTestPow);
    }
    onReceiveTestTick();
}

void MainWindow::onReceiveTestTick()
{
    if (!ui || !m_receiveTestRunning || m_receiveTestPaused) {
        return;
    }
    if (m_receivePhase != ReceiveTestPhase::RunningLevel) {
        return;
    }

    const int elapsedSec = static_cast<int>(m_receiveTestElapsed.elapsed() / 1000);
    const int msLevel = static_cast<int>(m_receiveTestElapsed.elapsed());
    const int vLevel = qBound(0, (msLevel * 100) / kRxLevelDurationMs, 100);
    // Локальный прогресс в индикаторе текущего уровня внутри полоски частоты.
    if (m_receiveFreqIndex >= 0 && m_receiveFreqIndex < m_receiveResultStrips.size()
        && m_receiveLevelIndex >= 0 && m_receiveLevelIndex < kRxLevelsCount) {
        if (auto *pb = qobject_cast<QProgressBar *>(
                m_receiveResultStrips[m_receiveFreqIndex].levelIndicators[m_receiveLevelIndex])) {
            pb->setRange(0, 100);
            pb->setValue(vLevel);
        }
    }
    updateReceiveResultStripsVisibility();

    auto indicatorFor = [&](int freqIdx, int levelIdx) -> QWidget * {
        if (freqIdx < 0 || freqIdx >= m_receiveResultStrips.size()) {
            return nullptr;
        }
        if (levelIdx < 0 || levelIdx >= kRxLevelsCount) {
            return nullptr;
        }
        return m_receiveResultStrips[freqIdx].levelIndicators[levelIdx];
    };

    // Пока идёт тест (0..4 сек), держим генератор включённым и шлём команду каждые 1 сек.
    if (elapsedSec < 5) {
        if (m_analyzerController) {
            m_analyzerController->setGenerator(m_receiveTestFreqHz, /*state*/ 1, m_receiveTestPow);
        }

        constexpr double kTractAttenuationDb = 60.0;
        constexpr double kToleranceDbm = 1.5;
        const double target = static_cast<double>(m_receiveTestPowDbm) - kTractAttenuationDb;
        const double lower = target - kToleranceDbm;
        const double upper = target + kToleranceDbm;
        const bool ok = (m_receiveLastRssiDbmFull >= lower && m_receiveLastRssiDbmFull <= upper);
        const QString msg = QStringLiteral("RSSI [%1..%2]")
                                .arg(QString::number(lower, 'f', 1), QString::number(upper, 'f', 1));
        if (QLabel *rv = receiveStripResultLabel(m_receiveFreqIndex)) {
            rv->setText(msg);
            // В начале каждого нового уровня не перекрашиваем подпись: старый RSSI может временно попадать
            // в новый коридор и давать ложный «зелёный». Цвет остаётся как в конце предыдущего уровня,
            // пока не прошло окно стабилизации и не применяется реальный ok по текущему коридору.
            constexpr int kRxCorridorStyleSettleMs = 400;
            if (msLevel >= kRxCorridorStyleSettleMs) {
                rv->setStyleSheet(ok ? QStringLiteral("color: #4ade80; font-family: Consolas; font-weight: bold;")
                                     : QStringLiteral("color: #ef4444; font-family: Consolas; font-weight: bold;"));
            }
        }
        return;
    }

    // Завершение уровня: выключаем генератор, выставляем индикатор PASS/FAIL и переходим к следующему уровню/частоте.
    if (m_analyzerController) {
        m_analyzerController->setGenerator(m_receiveTestFreqHz, /*state*/ 0, m_receiveTestPow);
    }

    constexpr double kTractAttenuationDb = 60.0;
    constexpr double kToleranceDbm = 1.5;
    const double target = static_cast<double>(m_receiveTestPowDbm) - kTractAttenuationDb;
    const double lower = target - kToleranceDbm;
    const double upper = target + kToleranceDbm;
    const bool ok = (m_receiveLastRssiDbmFull >= lower && m_receiveLastRssiDbmFull <= upper);
    if (!ok && m_receiveFreqIndex >= 0 && m_receiveFreqIndex < m_receiveFreqAllLevelsOk.size()) {
        m_receiveFreqAllLevelsOk[m_receiveFreqIndex] = false;
    }
    const QString passStyle = indicatorBoxStyle("#0f172a", "#4ade80", "#4ade80");
    const QString failStyle = indicatorBoxStyle("#0f172a", "#ef4444", "#ef4444");
    const QString runStyle = indicatorBoxStyle("#0f172a", "#38bdf8", "#38bdf8");

    // После завершения уровня выводим: ожидаемый RSSI / реальный RSSI (с дробной частью).
    const QString expectedRssiText = QString::number(target, 'f', 0);
    const QString levelText = QStringLiteral("%1/%2")
                                  .arg(expectedRssiText,
                                       QString::number(m_receiveLastRssiDbmFull, 'f', 1));
    applyIndicatorStyle(indicatorFor(m_receiveFreqIndex, m_receiveLevelIndex),
                        levelText,
                        ok ? passStyle : failStyle);

    ++m_receiveLevelIndex;
    if (m_receiveLevelIndex < kRxLevelsCount) {
        m_receiveTestPowDbm = kRxLevels[m_receiveLevelIndex].dbm;
        m_receiveTestPow = kRxLevels[m_receiveLevelIndex].pow;
        m_receiveLevelMaxRssiDbm = m_receiveLastRssiDbm;
        m_receiveTestElapsed.restart();
        applyIndicatorStyle(indicatorFor(m_receiveFreqIndex, m_receiveLevelIndex),
                            QString::fromLatin1(kRxLevels[m_receiveLevelIndex].title),
                            runStyle);
        onReceiveTestTick();
        return;
    }

    // Частота завершена — выводим итог PASS/FAIL по этой частоте.
    if (m_receiveFreqIndex >= 0 && m_receiveFreqIndex < m_receiveFreqAllLevelsOk.size()) {
        const bool freqOk = m_receiveFreqAllLevelsOk[m_receiveFreqIndex];
        const QString freqText = (m_receiveFreqIndex >= 0 && m_receiveFreqIndex < m_receiveTestFreqsHz.size())
                                     ? formatGroupedWithDots(static_cast<uint32_t>(m_receiveTestFreqsHz[m_receiveFreqIndex]))
                                     : QStringLiteral("—");
        if (m_receiveFreqIndex >= 0 && m_receiveFreqIndex < m_receiveResultStrips.size()) {
            auto &s = m_receiveResultStrips[m_receiveFreqIndex];
            showReceiveFinishIcons(s.statusTestFinishOk, s.statusTestFinishNot, freqOk);
            if (s.resultValue) {
                s.resultValue->setText(freqOk ? QStringLiteral("тест пройден")
                                              : QStringLiteral("тест не пройден"));
                s.resultValue->setStyleSheet(freqOk
                                                 ? QStringLiteral("color: #4ade80; font-family: Consolas; font-weight: bold;")
                                                 : QStringLiteral("color: #ef4444; font-family: Consolas; font-weight: bold;"));
            }
        }
        const QString resultText = freqOk ? QStringLiteral("пройден успешно.")
                                        : QStringLiteral("не пройден.");
        appendDeviceLogLine(QStringLiteral("⏸ Тест приёма тракта %1 на частоте %2 %3")
                              .arg(receiveTestTractDisplayNameForLog(), freqText, resultText),
                          QColor(freqOk ? QStringLiteral("#4ade80") : QStringLiteral("#ef4444")));
    }

    ++m_receiveFreqIndex;
    m_receiveLevelIndex = 0;
    if (m_receiveFreqIndex < m_receiveTestFreqsHz.size()) {
        m_receiveTestFreqHz = m_receiveTestFreqsHz[m_receiveFreqIndex];
        if (ui->lcdRecieveFreqValue) {
            ui->lcdRecieveFreqValue->display(formatGroupedWithDots(static_cast<uint32_t>(m_receiveTestFreqHz)));
        }
        updateReceiveResultStripsVisibility();
        if (!m_deviceController->setFrequencyRx(static_cast<uint8_t>(m_receiveTestTract),
                                                static_cast<uint32_t>(m_receiveTestFreqHz))) {
            onDeviceLogMessage(QStringLiteral("ОШИБКА: не удалось установить RX частоту %1 Гц (тест приёма).")
                                   .arg(formatGroupedWithDots(static_cast<uint32_t>(m_receiveTestFreqHz))));
            tearDownReceiveTest(true);
            return;
        }
        m_receivePhase = ReceiveTestPhase::WaitBaseline;
        m_receiveTestTickTimer.stop();
        updateReceiveResultStripsVisibility();
        return;
    }

    m_receiveTestTickTimer.stop();
    onDeviceLogMessage(QStringLiteral("⏸ Тест приёма на тракте %1 завершен.")
                           .arg(receiveTestTractDisplayNameForLog()));
    m_receiveTestRunning = false;
    m_receivePhase = ReceiveTestPhase::Idle;
    setReceiveTestControlsIdle();
    updateReceiveResultStripsVisibility();
    updateTabWidgetLockState();
}

void MainWindow::onFreqRxIndicationReceived(uint8_t tractNum, uint32_t freqHz)
{
    if (!shouldProcessStationTestingUdp()) {
        return;
    }
    if (!shouldUpdatePowerReadoutForTract(tractNum)) {
        return;
    }
    if (ui && ui->lcdRecieveFreqValue) {
        ui->lcdRecieveFreqValue->display(formatGroupedWithDots(freqHz));
    }
}

void MainWindow::onFreqTxIndicationReceived(uint8_t tractNum, uint32_t freqHz)
{
    if (!shouldProcessStationTestingUdp()) {
        return;
    }
    // Запоминаем последнюю установленную TX частоту по тракту всегда — это нужно для "Авария АНТ" пульсера.
    m_lastTxFreqHzByTract.insert(static_cast<int>(tractNum), static_cast<quint64>(freqHz));

    if (!shouldUpdatePowerReadoutForTract(tractNum) || !ui->lcdPowerFreqValue) {
        return;
    }
    ui->lcdPowerFreqValue->display(formatGroupedWithDots(freqHz));
}

void MainWindow::onRssiIndicationReceived(uint8_t tractNum, int16_t rssiDbm)
{
    if (!shouldProcessStationTestingUdp()) {
        return;
    }
    const int rssi = truncateRssiFractionalDigit(rssiDbm);
    const double rssiFull = static_cast<double>(rssiDbm) / 10.0;
    m_lastRssiDbmByTract.insert(static_cast<int>(tractNum), rssi);
    m_receiveLastRssiDbm = rssi;
    m_receiveLastRssiDbmFull = rssiFull;
    if (m_receiveTestRunning && m_receivePhase == ReceiveTestPhase::RunningLevel) {
        m_receiveLevelMaxRssiDbm = std::max(m_receiveLevelMaxRssiDbm, rssi);
    }

    // Переход WaitBaseline -> RunningLevel.
    if (m_receiveTestRunning && !m_receiveTestPaused && m_receivePhase == ReceiveTestPhase::WaitBaseline
        && m_receiveTestTract > 0 && static_cast<int>(tractNum) == m_receiveTestTract) {
        m_receiveBaselineRssiDbm = rssi;
        if (m_receiveFreqIndex >= 0 && m_receiveFreqIndex < m_receiveFreqBaselineRssiDbm.size()) {
            m_receiveFreqBaselineRssiDbm[m_receiveFreqIndex] = rssi;
        }
        m_receiveLevelMaxRssiDbm = rssi;

        if (ui && m_receiveFreqIndex >= 0 && m_receiveFreqIndex < m_receiveResultStrips.size()) {
            ReceiveResultStripUi &strip = m_receiveResultStrips[m_receiveFreqIndex];
            if (strip.baselineValue) {
                strip.baselineValue->setText(QString::number(rssi));
            }
            if (strip.rssiValue) {
                strip.rssiValue->setText(QString::number(rssi));
            }
        }

        m_receiveLevelIndex = 0;
        m_receiveTestPowDbm = kRxLevels[m_receiveLevelIndex].dbm;
        m_receiveTestPow = kRxLevels[m_receiveLevelIndex].pow;

        const QString runStyle = indicatorBoxStyle("#0f172a", "#38bdf8", "#38bdf8");
        auto indicatorFor = [&](int freqIdx, int levelIdx) -> QWidget * {
            if (freqIdx < 0 || freqIdx >= m_receiveResultStrips.size()) {
                return nullptr;
            }
            if (levelIdx < 0 || levelIdx >= kRxLevelsCount) {
                return nullptr;
            }
            return m_receiveResultStrips[freqIdx].levelIndicators[levelIdx];
        };
        applyIndicatorStyle(indicatorFor(m_receiveFreqIndex, m_receiveLevelIndex),
                            QString::fromLatin1(kRxLevels[m_receiveLevelIndex].title),
                            runStyle);

        m_receivePhase = ReceiveTestPhase::RunningLevel;
        m_receiveTestElapsed.restart();
        onReceiveTestTick();
        m_receiveTestTickTimer.start();
    }

    if (!shouldUpdatePowerReadoutForTract(tractNum)) {
        return;
    }

    if (ui->lcdPowerRSSIValue) {
        ui->lcdPowerRSSIValue->display(rssi);
    }
    if (ui->lcdRecieveRSSIValue) {
        ui->lcdRecieveRSSIValue->display(QString::number(rssiFull, 'f', 1));
    }

    // RSSI в полоске активной частоты теста приёма
    if (ui && m_receiveTestTract > 0 && static_cast<int>(tractNum) == m_receiveTestTract
        && m_receiveFreqIndex >= 0 && m_receiveFreqIndex < m_receiveResultStrips.size()) {
        if (QLabel *stripRssi = m_receiveResultStrips[m_receiveFreqIndex].rssiValue) {
            stripRssi->setText(QString::number(rssi));
        }
    }
}

void MainWindow::onPowerLevelIndicationReceived(uint8_t tractNum, uint8_t levelCode)
{
    if (!shouldProcessStationTestingUdp()) {
        return;
    }
    const int tr = static_cast<int>(tractNum);
    if (tr <= 0) {
        return;
    }

    const bool hadPrevPowerInd = m_powerLevelCodeByTract.contains(tr);
    const uint8_t prevIndicatedLevel = hadPrevPowerInd ? m_powerLevelCodeByTract.value(tr) : levelCode;
    m_powerLevelCodeByTract.insert(tr, levelCode);

    // Как в Surs: при внешнем переключении уровня UI должен подхватить новый radiobutton.
    // В нашем случае отображаем уровень только для текущего активного тракта.
    if (tr != m_ppmCurrentOnTract) {
        return;
    }

    if (hadPrevPowerInd && prevIndicatedLevel != levelCode) {
        clearPowerGraphPlotCurves();
    }

    // В пульте Surs kod_power==2 — radioButton_power2 (средняя мощность). В Check_station
    // applyPowerLevelUiByCode() приводит коды 2…4 к отображению «макс», но без UDP-команды
    // тракт физически остаётся на среднем уровне — масштаб графика и показания расходятся.
    if (levelCode == 2 && m_deviceController && m_deviceController->isConnected()) {
        constexpr uint8_t kPowerLevelMax = 4;
        if (m_deviceController->setPowerLevel(tractNum, kPowerLevelMax)) {
            m_powerLevelCodeByTract.insert(tr, kPowerLevelMax);
        } else {
            onDeviceLogMessage(QStringLiteral(
                "ОШИБКА: извне выставлен средний уровень мощности (код 2); не удалось отправить команду "
                "на максимальный уровень для тракта %1.")
                .arg(tr));
        }
    }

    applyPowerLevelUiByCode(levelCode, /*rescaleGraph*/ true);
}

void MainWindow::startPpmInitAfterIntegrityOk()
{
    if (!m_deviceController || !m_deviceController->isConnected()) {
        onDeviceLogMessage("PPM: нет подключения, инициализация трактов после reboot невозможна.");
        return;
    }

    // UI: держим progressBar в бесконечном режиме, PPM скрыт до конца последовательности.
    if (ui && ui->progressBar) {
        showStationHeaderCenter(StationHeaderCenter::ProgressBar);
        ui->progressBar->setTextVisible(false);
        ui->progressBar->setRange(0, 0);
        ui->progressBar->setValue(0);
    }
    resetPowerReadoutUi();

    // Сбрасываем UI состояний трактов.
    for (int i = 0; i < m_ppmTractsSorted.size(); ++i) {
        setPpmRadioUiState(i, false, false);
    }

    m_ppmCurrentOnTract = -1;
    m_ppmPendingTargetOnTract = -1;
    m_ppmSwitchNeedsPostUpdate = false;
    m_ppmPowerStage = PpmPowerSequenceStage::InitAllOff;
    m_ppmPowerSeqIndex = 0;
    updateMenubarVisibility();

    updateTabWidgetLockState();
    continuePpmInitSequence();
}

void MainWindow::continuePpmInitSequence()
{
    if (!m_deviceController || !m_deviceController->isConnected()) {
        return;
    }
    if (m_deviceController->isAwaitingTractPowerAck()) {
        return;
    }

    const QVector<int> tracts = ppmTractNumbersForUi();
    if (m_ppmPowerStage == PpmPowerSequenceStage::InitAllOff) {
        if (m_ppmPowerSeqIndex >= tracts.size()) {
            m_ppmPowerStage = PpmPowerSequenceStage::InitFirstOn;
            m_ppmPowerSeqIndex = 0;
        } else {
            const int t = tracts.at(m_ppmPowerSeqIndex);
            ++m_ppmPowerSeqIndex;
            m_deviceController->setTractControl(static_cast<uint8_t>(t), false, true);
            return;
        }
    }

    if (m_ppmPowerStage == PpmPowerSequenceStage::InitFirstOn) {
        const int first = ppmFirstTractNumber();
        if (first <= 0) {
            m_ppmPowerStage = PpmPowerSequenceStage::None;
            return;
        }
        m_ppmCurrentOnTract = first;
        m_ppmPowerStage = PpmPowerSequenceStage::InitFirstOnWaitAck;
        m_deviceController->setTractControl(static_cast<uint8_t>(first), true, true);
        return;
    }
}

void MainWindow::startPpmSwitchToTract(int tractNum)
{
    if (!m_deviceController || !m_deviceController->isConnected()) {
        return;
    }
    if (tractNum <= 0) {
        return;
    }
    if (tractNum == m_ppmCurrentOnTract) {
        const int idx = m_ppmTractsSorted.indexOf(tractNum);
        if (idx >= 0) {
            setPpmRadioUiState(idx, true, true);
        }
        return;
    }

    // При реальном переключении сразу очищаем readout частоты/RSSI,
    // чтобы не показывать значения предыдущего тракта.
    resetPowerReadoutUi();

    // UI: на время переключения скрываем PPM и показываем progressBar (бесконечность).
    if (ui && ui->progressBar) {
        showStationHeaderCenter(StationHeaderCenter::ProgressBar);
        ui->progressBar->setTextVisible(false);
        ui->progressBar->setRange(0, 0);
        ui->progressBar->setValue(0);
    }

    // UI: держим чекбокс на текущем включенном тракте до подтверждения.
    const int curIdx = m_ppmTractsSorted.indexOf(m_ppmCurrentOnTract);
    if (curIdx >= 0) {
        setPpmRadioUiState(curIdx, true, true);
    }

    m_ppmPendingTargetOnTract = tractNum;
    m_ppmSwitchNeedsPostUpdate = false;
    if (m_ppmCurrentOnTract > 0) {
        m_ppmPowerStage = PpmPowerSequenceStage::SwitchOffCurrent;
        setPpmFrameStateForTract(m_ppmCurrentOnTract, TRAKT_START_OFF);
        // Защита: выключение текущего тракта — штатная часть переключения, не "внешнее событие".
        m_ppmIgnoreExternalPowerOffTract = m_ppmCurrentOnTract;
        const qint64 nowMs = m_uptime.isValid() ? m_uptime.elapsed() : 0;
        // Окно как после таймаута ACK: поздний IND_TRAKT OFF не должен запускать восстановление тракта.
        m_ppmIgnoreExternalPowerOffUntilMs = (nowMs >= 0) ? (nowMs + 6000) : 0;
        // Хвост перезагрузки тракта тем же окном, что и CMD_CURR_DIR_SET — подавляет ложное «внешнее выключение».
        armSelfIssuedTractReload(m_ppmCurrentOnTract);
        m_deviceController->setTractControl(static_cast<uint8_t>(m_ppmCurrentOnTract), false, true);
    } else {
        // Если текущий включенный тракт неизвестен — сначала гарантированно выключаем
        // все остальные управляемые тракты (кроме целевого), затем включаем целевой.
        m_ppmPowerStage = PpmPowerSequenceStage::SwitchOffOthersBeforeOn;
        m_ppmPowerSeqIndex = 0;
        continuePpmSwitchSequence();
    }
    updateTabWidgetLockState();
}

void MainWindow::continuePpmSwitchSequence()
{
    if (!m_deviceController || !m_deviceController->isConnected()) {
        return;
    }
    if (m_deviceController->isAwaitingTractPowerAck()) {
        return;
    }

    if (m_ppmPowerStage == PpmPowerSequenceStage::SwitchOffCurrent) {
        const int offIdx = m_ppmTractsSorted.indexOf(m_ppmCurrentOnTract);
        if (offIdx >= 0) {
            setPpmRadioUiState(offIdx, false, false);
        }
        setPpmFrameStateForTract(m_ppmCurrentOnTract, TRAKT_END_OFF);
        m_ppmCurrentOnTract = -1;
        m_ppmPowerStage = PpmPowerSequenceStage::SwitchOnTarget;
        if (m_ppmPendingTargetOnTract > 0) {
            setPpmFrameStateForTract(m_ppmPendingTargetOnTract, TRAKT_START_ON);
            m_deviceController->setTractControl(static_cast<uint8_t>(m_ppmPendingTargetOnTract), true, true);
        }
        return;
    }

    if (m_ppmPowerStage == PpmPowerSequenceStage::SwitchOffOthersBeforeOn) {
        const QVector<int> tracts = ppmTractNumbersForUi();
        // Пропускаем целевой тракт — его нужно включить последним.
        int idx = m_ppmPowerSeqIndex;
        while (idx < tracts.size() && tracts.at(idx) == m_ppmPendingTargetOnTract) {
            ++idx;
        }
        if (idx >= tracts.size()) {
            m_ppmPowerStage = PpmPowerSequenceStage::SwitchOnTarget;
            if (m_ppmPendingTargetOnTract > 0) {
                setPpmFrameStateForTract(m_ppmPendingTargetOnTract, TRAKT_START_ON);
                m_deviceController->setTractControl(static_cast<uint8_t>(m_ppmPendingTargetOnTract), true, true);
            }
            return;
        }

        const int t = tracts.at(idx);
        m_ppmPowerSeqIndex = idx + 1;
        m_ppmIgnoreExternalPowerOffTract = t;
        {
            const qint64 nowMs = m_uptime.isValid() ? m_uptime.elapsed() : 0;
            m_ppmIgnoreExternalPowerOffUntilMs = (nowMs >= 0) ? (nowMs + 6000) : 0;
        }
        armSelfIssuedTractReload(t);
        m_deviceController->setTractControl(static_cast<uint8_t>(t), false, true);
        return;
    }

    if (m_ppmPowerStage == PpmPowerSequenceStage::SwitchOnTarget) {
        if (m_ppmPendingTargetOnTract > 0) {
            m_ppmCurrentOnTract = m_ppmPendingTargetOnTract;
            m_ppmPendingTargetOnTract = -1;
            setPpmFrameStateForTract(m_ppmCurrentOnTract, TRAKT_END_ON);
            const int onIdx = m_ppmTractsSorted.indexOf(m_ppmCurrentOnTract);
            if (onIdx >= 0) {
                setPpmRadioUiState(onIdx, true, true);
            }
        }
        m_ppmPowerStage = PpmPowerSequenceStage::None;

        // UI: переключение завершено — возвращаем PPM и прячем progressBar.
        if (ui && ui->progressBar) {
            ui->progressBar->setRange(0, 100);
            ui->progressBar->setValue(0);
            showStationHeaderCenter(StationHeaderCenter::FramePpm);
        }
        applyPowerLevelUiByCode(static_cast<uint8_t>(m_powerLevelCodeByTract.value(m_ppmCurrentOnTract, 4)),
                                /*rescaleGraph*/ true);
        // Важно: IND_ERROR целевого тракта мог прийти ещё во время переключения,
        // когда selectedPpmTractFromUi() указывал на предыдущий тракт.
        // После завершения переключения перерисовываем статус из кэша целевого тракта.
        refreshPpmStatusUiForTract(m_ppmCurrentOnTract);

        // По требованию: post-update (аналог "labelUpdate") — CMD_CURR_DIR_SET DirId=1 после
        // восстановления тракта после внешнего выключения.
        if (m_ppmSwitchNeedsPostUpdate && m_ppmCurrentOnTract > 0) {
            const bool ok = sendPpmCurrDirSetDir1(m_ppmCurrentOnTract);
            if (ok) {
                setPpmUpdateLabelVisible(true);
            }
        }
        m_ppmSwitchNeedsPostUpdate = false;

        updateTabWidgetLockState();
        return;
    }
}

bool MainWindow::uploadAndActivateTestProfileOverSsh(const QString &stationIp,
                                                     const QString &localTarPath,
                                                     QString *errorText,
                                                     bool seedProfileRegistry)
{
    auto logAsync = [this](const QString &msg) {
        if (!debug) {
            return;
        }
        QMetaObject::invokeMethod(this, [this, msg]() { onDeviceLogMessage(msg); }, Qt::QueuedConnection);
    };

    SSHer ssher;
    ssher.setAllowLegacyAlgorithms(true);
    connect(&ssher, &SSHer::logMessage, this,
            [this](const QString &msg, const QString &) {
                if (!debug) {
                    return;
                }
                QMetaObject::invokeMethod(this, [this, msg]() { onDeviceLogMessage(msg); }, Qt::QueuedConnection);
            },
            Qt::QueuedConnection);

    if (localTarPath.trimmed().isEmpty() || !QFileInfo::exists(localTarPath)) {
        if (errorText) {
            *errorText = QString("Локальный архив профиля не найден: %1").arg(localTarPath);
        }
        return false;
    }

    if (!ssher.connectToHost(stationIp, 22)) {
        if (errorText) {
            *errorText = ssher.lastError().isEmpty() ? "Не удалось подключиться по SSH." : ssher.lastError();
        }
        return false;
    }
    if (!ssher.authenticate(QString::fromLatin1(kStationSshUser), QString::fromLatin1(kStationSshPassword))) {
        if (errorText) {
            *errorText = ssher.lastError().isEmpty() ? "Ошибка SSH аутентификации." : ssher.lastError();
        }
        return false;
    }

    // Загружаем УЖЕ подготовленный локальный архив.
    if (!ssher.uploadFile(localTarPath, QString::fromLatin1(kTestProfileRemotePath), 0644)) {
        if (errorText) {
            *errorText = ssher.lastError().isEmpty()
                             ? QString("Не удалось загрузить архив на устройство в %1").arg(QString::fromLatin1(kTestProfileRemotePath))
                             : ssher.lastError();
        }
        return false;
    }

    auto runChecked = [&](const QString &cmd, const QString &step) -> bool {
        int exitCode = 0;
        const QString out = ssher.executeCommand(cmd, &exitCode);
        // Если команда не выполнилась на уровне SSH (канал/exec/чтение), `executeCommand` вернёт пусто
        // и заполнит lastError(). Такой случай нельзя считать успехом даже если exitCode остался 0.
        if (exitCode == 0 && out.isEmpty() && !ssher.lastError().isEmpty()) {
            const QString msg = QString("[%1] Ошибка SSH при выполнении: %2\n%3")
                                    .arg(step, ssher.lastError(), cmd);
            if (errorText) {
                *errorText = msg;
            }
            logAsync(msg);
            return false;
        }
        if (exitCode != 0) {
            const QString details = out.trimmed().isEmpty() ? QStringLiteral("(нет вывода)") : out.trimmed();
            const QString msg = QString("[%1] Ошибка выполнения (exitCode=%2): %3\n%4")
                                    .arg(step)
                                    .arg(exitCode)
                                    .arg(cmd)
                                    .arg(details);
            if (errorText) {
                *errorText = msg;
            }
            logAsync(msg);
            return false;
        }
        if (!out.trimmed().isEmpty()) {
            logAsync(QString("[%1] %2").arg(step, out.trimmed()));
        }
        return true;
    };

    // 1) Удаляем активный профиль (если есть)
    if (!runChecked(QStringLiteral("rm -rf /radio/profiles/Profile_Active/"), QStringLiteral("rm profile"))) {
        return false;
    }

    // 2) Распаковываем архив в /radio/profiles/ (внутри архива должна быть структура Profile_Active/*)
    if (!runChecked(QStringLiteral("tar -xf /tmp/profile_active_TEST.tar.gz -C /radio/profiles/"),
                    QStringLiteral("untar profile"))) {
        return false;
    }

    if (seedProfileRegistry) {
        QTemporaryFile profilesTmp(QDir::tempPath() + "/Profiles_XXXXXX.xml");
        profilesTmp.setAutoRemove(true);
        if (!profilesTmp.open()) {
            if (errorText) {
                *errorText = QString("Не удалось создать временный Profiles.xml: %1").arg(profilesTmp.errorString());
            }
            return false;
        }
        const QByteArray profilesXml =
            generateMinimalProfilesXml(kDefaultProfileId, QStringLiteral("TEST"));
        if (profilesTmp.write(profilesXml) != profilesXml.size()) {
            if (errorText) {
                *errorText = QStringLiteral("Не удалось записать временный Profiles.xml.");
            }
            return false;
        }
        profilesTmp.flush();
        profilesTmp.close();
        if (!ssher.uploadFile(profilesTmp.fileName(), QString::fromLatin1(kProfilesRemotePath), 0644)) {
            if (errorText) {
                *errorText = ssher.lastError().isEmpty()
                                  ? QString("Не удалось загрузить %1").arg(QString::fromLatin1(kProfilesRemotePath))
                                  : ssher.lastError();
            }
            return false;
        }

        if (!runChecked(QStringLiteral("rm -rf /radio/profiles/Profile_1/ && "
                                       "cp -a /radio/profiles/Profile_Active /radio/profiles/Profile_1"),
                        QStringLiteral("copy Profile_1"))) {
            return false;
        }

        QTemporaryFile traktTmp(QDir::tempPath() + "/TraktParam_seed_XXXXXX.xml");
        traktTmp.setAutoRemove(true);
        if (!traktTmp.open()) {
            if (errorText) {
                *errorText = QString("Не удалось создать временный TraktParam.xml: %1").arg(traktTmp.errorString());
            }
            return false;
        }
        const QString traktLocal = traktTmp.fileName();
        traktTmp.close();
        if (!ssher.downloadFile(QString::fromLatin1(kTraktParamRemotePath), traktLocal)) {
            if (errorText) {
                *errorText = ssher.lastError().isEmpty()
                                  ? QString("Не удалось скачать %1").arg(QString::fromLatin1(kTraktParamRemotePath))
                                  : ssher.lastError();
            }
            return false;
        }
        QFile traktFile(traktLocal);
        if (!traktFile.open(QIODevice::ReadOnly)) {
            if (errorText) {
                *errorText = QString("Не удалось прочитать TraktParam.xml: %1").arg(traktFile.errorString());
            }
            return false;
        }
        const QString patchedTrakt =
            patchTraktParamProfIdInPlace(QString::fromUtf8(traktFile.readAll()), kDefaultProfileId);
        traktFile.close();
        if (!traktFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (errorText) {
                *errorText = QString("Не удалось записать TraktParam.xml: %1").arg(traktFile.errorString());
            }
            return false;
        }
        const QByteArray patchedBytes = patchedTrakt.toUtf8();
        if (traktFile.write(patchedBytes) != patchedBytes.size()) {
            if (errorText) {
                *errorText = QStringLiteral("Не удалось сохранить TraktParam.xml.");
            }
            return false;
        }
        traktFile.close();
        if (!ssher.uploadFile(traktLocal, QString::fromLatin1(kTraktParamRemotePath), 0644)) {
            if (errorText) {
                *errorText = ssher.lastError().isEmpty()
                                  ? QString("Не удалось загрузить %1").arg(QString::fromLatin1(kTraktParamRemotePath))
                                  : ssher.lastError();
            }
            return false;
        }
    }

    // 3) Удаляем архив с устройства
    if (!runChecked(QStringLiteral("rm -f /tmp/profile_active_TEST.tar.gz"), QStringLiteral("rm archive"))) {
        return false;
    }

    // 4) Сбрасываем буферы на диск
    if (!runChecked(QStringLiteral("sync"), QStringLiteral("sync"))) {
        return false;
    }

    // 4.5) Контроль целостности: архивируем Profile_Active до reboot.
    if (!runChecked(QStringLiteral(
                        "/bin/bash -lc 'cd /radio/profiles/Profile_Active/ && "
                        "find . -maxdepth 2 -type f \\( -name \"Trakts.xml\" -o -name \"Unions.xml\" -o -path "
                        "\"./Trakt_*/*.xml\" \\) -print0 | xargs -0 tar -cf "
                        "/radio/profile_active_before_reset.tar.gz'"),
                    QStringLiteral("tar before reboot"))) {
        return false;
    }

    // 4.6) Ещё раз sync после создания архива.
    if (!runChecked(QStringLiteral("sync"), QStringLiteral("sync after tar"))) {
        return false;
    }

    //5) Перезагружаем устройство. Здесь соединение может оборваться до получения нормального exitCode/вывода,
    //поэтому "успех" этого шага по SSH не гарантированно детектируется.
    {
        int exitCode = 0;
        const QString out = ssher.executeCommand(QStringLiteral("/sbin/reboot"), &exitCode);
        if (!out.trimmed().isEmpty()) {
            logAsync(QString("[reboot] %1").arg(out.trimmed()));
        }
        logAsync("Команда reboot отправлена (SSH-сессия может оборваться).");
    }

    return true;
}

void MainWindow::prepareTestProfileAfterConnect(const QString &stationIp)
{
    if (stationIp.trimmed().isEmpty()) {
        return;
    }
    // Если уже готовили для этой станции — не повторяем SSH, но синхронизируем кнопку старта:
    // после ложного/кратковременного «обрыва» onDeviceDisconnected отключает её, а сюда мы не заходим
    // в ветку успеха prepare, где setStartTestingButtonEnabled(true).
    if (m_preparedProfileTar && m_preparedProfileStationIp == stationIp.trimmed()) {
        if (m_deviceController && m_deviceController->isConnected()) {
            setStartTestingButtonEnabled(true);
            updateStationLabelText();
        }
        return;
    }
    if (m_preparingProfile) {
        return;
    }

    m_preparingProfile = true;
    m_preparedProfileTar.reset();
    m_preparedProfileStationIp = stationIp.trimmed();
    m_stationNeedsProfileRegistrySeed = false;
    m_externalSwitchProtectionArmed = false;
    setStartTestingButtonEnabled(false);
    if (m_deviceController) {
        m_deviceController->setInactivityWatchdogEnabled(false);
    }

    QPointer<MainWindow> self(this);
    QtConcurrent::run([self, stationIpTrimmed = m_preparedProfileStationIp]() {
        if (!self) {
            return;
        }

        QString err;

        SSHer ssher;
        ssher.setAllowLegacyAlgorithms(true);
        connect(&ssher, &SSHer::logMessage, qApp,
                [self](const QString &msg, const QString &) {
                    if (!self || !debug) {
                        return;
                    }
                    QMetaObject::invokeMethod(qApp, [self, msg]() {
                        if (!self) {
                            return;
                        }
                        self->onDeviceLogMessage(msg);
                    }, Qt::QueuedConnection);
                },
                Qt::QueuedConnection);

        if (!ssher.connectToHost(stationIpTrimmed, 22)) {
            err = ssher.lastError().isEmpty() ? QStringLiteral("Не удалось подключиться по SSH.") : ssher.lastError();
        } else if (!ssher.authenticate(QString::fromLatin1(kStationSshUser), QString::fromLatin1(kStationSshPassword))) {
            err = ssher.lastError().isEmpty() ? QStringLiteral("Ошибка SSH аутентификации.") : ssher.lastError();
        }

        QString stationVariant;
        QVector<TraktParamEntry> traktForPpm;
        int traktNumForPpm = 0;
        if (err.isEmpty()) {
            stationVariant = readStationVariantOverSsh(ssher);
        }
        if (err.isEmpty()) {
            QString traktErr;
            if (!loadTraktParamFromStationOverSsh(ssher, &traktForPpm, &traktNumForPpm, &traktErr)) {
                err = traktErr;
            }
        }

        bool needsProfileRegistrySeed = false;
        if (err.isEmpty()) {
            QString profileCheckDetail;
            needsProfileRegistrySeed =
                !stationHasUsableProfileOverSsh(ssher, traktNumForPpm, &profileCheckDetail);
        }

        // Сначала в лог — вариант и конфигурация (сразу после считывания), затем статус профиля и подготовка.
        if (err.isEmpty() && self) {
            const QString variantForLog = stationVariant;
            const QVector<TraktParamEntry> traktForLog = traktForPpm;
            const int traktNumForLog = traktNumForPpm;
            const bool profileRegistrySeedForLog = needsProfileRegistrySeed;
            QMetaObject::invokeMethod(
                qApp,
                [self, stationIpTrimmed, variantForLog, traktForLog, traktNumForLog, profileRegistrySeedForLog]() {
                    if (!self) {
                        return;
                    }
                    if (!self->m_deviceController
                        || self->m_deviceController->config().stationIp.trimmed() != stationIpTrimmed) {
                        return;
                    }
                    if (!variantForLog.isEmpty()) {
                        self->m_stationHardwareVariant = variantForLog;
                        self->updateStationLabelText();
                    }
                    self->onDeviceLogMessage(formatStationVariantLogMessage(variantForLog));
                    self->onDeviceLogMessage(formatStationTractsConfigLogMessage(traktForLog, traktNumForLog));
                    if (profileRegistrySeedForLog) {
                        self->onDeviceLogMessage(
                            QStringLiteral("На радиостанции отсутствуют профили. Подготовка тестового профиля..."));
                    }
                    self->onDeviceLogMessage(QStringLiteral("Подготовка радиоданных под конфигурацию радиостанции..."));
                },
                Qt::BlockingQueuedConnection);
        }

        // Подготовим шаблонный архив из ресурсов в temp-файл.
        QString templateTarPath;
        if (err.isEmpty()) {
            QFile resFile(QString::fromLatin1(kTestProfileResourcePath));
            if (!resFile.open(QIODevice::ReadOnly)) {
                err = QString("Не удалось открыть архив из ресурсов: %1").arg(resFile.errorString());
            } else {
                const QByteArray payload = resFile.readAll();
                if (payload.isEmpty()) {
                    err = QStringLiteral("Архив из ресурсов пустой или не прочитан.");
                } else {
                    QTemporaryFile templateTar(QDir::tempPath() + "/profile_active_TEST_template_XXXXXX.tar.gz");
                    templateTar.setAutoRemove(true);
                    if (!templateTar.open()) {
                        err = QString("Не удалось создать временный файл шаблона: %1").arg(templateTar.errorString());
                    } else if (templateTar.write(payload) != payload.size()) {
                        err = QString("Не удалось записать временный файл шаблона: %1").arg(templateTar.errorString());
                    } else {
                        templateTar.flush();
                        templateTarPath = templateTar.fileName();
                        // Важно: не удаляем файл до завершения build (оставим на диске).
                        templateTar.setAutoRemove(false);
                    }
                }
            }
        }

        // Сюда соберём кастомный архив и передадим в UI-поток как "живой" temp-файл.
        QSharedPointer<QTemporaryFile> outTar;
        if (err.isEmpty()) {
            outTar.reset(new QTemporaryFile(QDir::tempPath() + "/profile_active_TEST_custom_XXXXXX.tar.gz"));
            outTar->setAutoRemove(true);
            if (!outTar->open()) {
                err = QString("Не удалось создать временный файл архива: %1").arg(outTar->errorString());
            } else {
                const QString outPath = outTar->fileName();
                outTar->close(); // tar будет писать сам
                QString buildErr;
                if (!buildCustomizedProfileArchive(stationIpTrimmed, ssher, templateTarPath, outPath, &buildErr,
                                                   &traktForPpm, &traktNumForPpm, &traktForPpm, traktNumForPpm)) {
                    err = buildErr.isEmpty() ? QStringLiteral("Не удалось собрать профиль по TraktParam.xml") : buildErr;
                    outTar.reset();
                }
            }
        }

        // Чистим шаблонный tar (если был)
        if (!templateTarPath.isEmpty()) {
            QFile::remove(templateTarPath);
        }

        if (!self) {
            return;
        }

        QMetaObject::invokeMethod(qApp,
                                  [self, stationIpTrimmed, outTar, err, traktForPpm, traktNumForPpm, stationVariant,
                                   needsProfileRegistrySeed]() {
            if (!self) {
                return;
            }
            self->m_preparingProfile = false;
            if (self->m_deviceController) {
                self->m_deviceController->setInactivityWatchdogEnabled(true);
            }
            if (!err.isEmpty()) {
                // Если станция уже поменялась — не засоряем лог лишним.
                if (self->m_deviceController
                    && self->m_deviceController->config().stationIp.trimmed() == stationIpTrimmed) {
                    self->onDeviceLogMessage(QString("ОШИБКА подготовки профиля: %1").arg(err));
                }
                self->m_preparedProfileTar.reset();
                self->m_stationNeedsProfileRegistrySeed = false;
                self->setStartTestingButtonEnabled(false);
                return;
            }
            // Станция могла смениться, пока готовили.
            if (!self->m_deviceController
                || self->m_deviceController->config().stationIp.trimmed() != stationIpTrimmed) {
                self->m_preparedProfileTar.reset();
                self->m_stationNeedsProfileRegistrySeed = false;
                self->setStartTestingButtonEnabled(false);
                return;
            }
            self->m_preparedProfileTar = outTar;
            self->m_stationNeedsProfileRegistrySeed = needsProfileRegistrySeed;
            self->applyTraktParamToPpmUi(traktForPpm, traktNumForPpm);
            if (!stationVariant.isEmpty()) {
                self->m_stationHardwareVariant = stationVariant;
                self->updateStationLabelText();
            }
            if (needsProfileRegistrySeed) {
                self->onDeviceLogMessage(
                    QStringLiteral("Радиоданные подготовлены (тестовый профиль добавлен). Нажмите НАЧАТЬ "
                                   "ТЕСТИРОВАНИЕ."));
            } else {
                self->onDeviceLogMessage(
                    QStringLiteral("Радиоданные подготовлены и радиостанция готова к началу тестирования (нажмите "
                                   "НАЧАТЬ ТЕСТИРОВАНИЕ)."));
            }
            self->setStartTestingButtonEnabled(true);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::onStartTestingClicked()
{
    if (m_bkuUpdateMode) {
        if (m_updateBkuWidget) {
            if (m_updateBkuWidget->isStationLinkedForUpdate()) {
                m_updateBkuWidget->startUpdate();
            } else {
                m_updateBkuWidget->startEmergencyTftp();
            }
        }
        return;
    }

    const QString stationIp = m_deviceController ? m_deviceController->config().stationIp.trimmed() : QString();
    if (stationIp.isEmpty()) {
        onDeviceLogMessage("ОШИБКА: IP радиостанции не задан (нужно подключиться к радиостанции).");
        return;
    }
    if (!m_preparedProfileTar || m_preparedProfileStationIp != stationIp || m_preparingProfile) {
        onDeviceLogMessage("ОШИБКА: Профиль ещё не подготовлен для текущей радиостанции. Переподключитесь или дождитесь подготовки после подключения.");
        return;
    }
    if (!analyzerAvailableForUi(m_analyzerConnected)) {
        onDeviceLogMessage(
            QStringLiteral("ОШИБКА: анализатор не подключён — начать тестирование невозможно."));
        return;
    }

    if (ui->pushButtonStartTesting) {
        stopStartTestingButtonGlow();
    }

    m_externalSwitchProtectionArmed = true;
    setTestingUiBusy(true);
    {
        int stationNum = 0;
        const QStringList parts = stationIp.split('.');
        if (parts.size() == 4) {
            bool ok = false;
            stationNum = parts[2].toInt(&ok);
            if (!ok) {
                stationNum = 0;
            }
        }
        if (m_stationNeedsProfileRegistrySeed) {
            if (stationNum > 0) {
                onDeviceLogMessage(
                    QStringLiteral("Загрузка радиоданных и создание профиля на радиостанцию №%1").arg(stationNum));
            } else {
                onDeviceLogMessage(
                    QStringLiteral("Загрузка радиоданных и создание профиля на радиостанцию"));
            }
        } else if (stationNum > 0) {
            onDeviceLogMessage(QStringLiteral("Загрузка радиоданных на радиостанцию №%1").arg(stationNum));
        } else {
            onDeviceLogMessage(QStringLiteral("Загрузка радиоданных на радиостанцию"));
        }
    }

    const QString localTarPath = m_preparedProfileTar->fileName();
    const bool seedProfileRegistry = m_stationNeedsProfileRegistrySeed;
    QPointer<MainWindow> self(this);
    QtConcurrent::run([self, stationIp, localTarPath, seedProfileRegistry]() {
        QString err;
        const bool ok = self
            && self->uploadAndActivateTestProfileOverSsh(stationIp, localTarPath, &err, seedProfileRegistry);
        QMetaObject::invokeMethod(qApp, [self, ok, err, stationIp]() {
            if (!self) {
                return;
            }
            if (ok) {
                self->m_stationNeedsProfileRegistrySeed = false;
                self->onDeviceLogMessage(QStringLiteral(
                    "Радиоданные загружены. Перезапуск радиостанции. Ожидание включения..."));
                self->startProfileIntegritySequenceAfterReboot(stationIp);
            } else {
                self->onDeviceLogMessage(QString("ОШИБКА тестирования: %1").arg(err.isEmpty() ? QString("неизвестная ошибка") : err));
                self->setTestingUiBusy(false);
            }
        }, Qt::QueuedConnection);
    });
}

void MainWindow::initPowerTestingUi()
{
    if (ui->radioButtonPowLevelMax) {
        connect(ui->radioButtonPowLevelMax, &QRadioButton::toggled, this, &MainWindow::onPowerLevelRadioToggled);
    }
    if (ui->radioButtonPowLeveMin) {
        connect(ui->radioButtonPowLeveMin, &QRadioButton::toggled, this, &MainWindow::onPowerLevelRadioToggled);
    }
    if (ui->checkPowerPrimaryCheck) {
        connect(ui->checkPowerPrimaryCheck, &QCheckBox::toggled,
                this, &MainWindow::onPowerTestOptionsChanged);
    }
    if (ui->checkPowerFullRange) {
        connect(ui->checkPowerFullRange, &QCheckBox::toggled,
                this, &MainWindow::onPowerTestOptionsChanged);
    }
    m_powerLevelCode = (ui->radioButtonPowLeveMin && ui->radioButtonPowLeveMin->isChecked()) ? 1 : 4;

    setEmissionAnimating(false);

    if (QPushButton *btn = ui->pushButtonStartTestingPower) {
        btn->setCheckable(true);
        btn->setAutoDefault(false);
        btn->setDefault(false);
        connect(btn, &QPushButton::toggled, this, &MainWindow::onPowerTestingToggled);
    }

    m_powerTestIconPause = receiveBlackIconPause();
    m_powerTestIconPlay = receiveBlackIconPlay();
    m_powerTestIconStop = receiveBlackIconStop();

    if (ui->pushButtonPowerTestPause) {
        ui->pushButtonPowerTestPause->setIcon(m_powerTestIconPause);
        setPauseButtonMode(ui->pushButtonPowerTestPause, /*isPlayIcon=*/false);
        connect(ui->pushButtonPowerTestPause, &QPushButton::clicked,
                this, &MainWindow::onPowerTestPauseClicked);
    }
    if (ui->pushButtonPowerTestStop) {
        ui->pushButtonPowerTestStop->setIcon(m_powerTestIconStop);
        connect(ui->pushButtonPowerTestStop, &QPushButton::clicked,
                this, &MainWindow::onPowerTestStopClicked);
    }

    setPowerTestControlsIdle();
    updatePowerTestButtonsAccessForSelectedTract();

    initPowerTestingPlots();
}

void MainWindow::setPowerTestControlsIdle()
{
    if (!ui) {
        return;
    }
    setEmissionAnimating(false);
    if (ui->emissionAntennaWidget) {
        ui->emissionAntennaWidget->setVisible(false);
    }
    if (ui->pushButtonStartTestingPower) {
        ui->pushButtonStartTestingPower->setVisible(true);
    }
    if (ui->pushButtonPowerTestPause) {
        ui->pushButtonPowerTestPause->setVisible(false);
        ui->pushButtonPowerTestPause->setIcon(m_powerTestIconPause);
        setPauseButtonMode(ui->pushButtonPowerTestPause, /*isPlayIcon=*/false);
    }
    if (ui->pushButtonPowerTestStop) {
        ui->pushButtonPowerTestStop->setVisible(false);
    }
    updatePowerLevelRadioButtonsEnabled();
}

void MainWindow::setPowerTestControlsRunning(bool playbackPaused)
{
    if (!ui) {
        return;
    }
    if (ui->pushButtonStartTestingPower) {
        ui->pushButtonStartTestingPower->setVisible(false);
    }
    if (ui->pushButtonPowerTestPause) {
        ui->pushButtonPowerTestPause->setVisible(true);
        ui->pushButtonPowerTestPause->setIcon(playbackPaused ? m_powerTestIconPlay : m_powerTestIconPause);
        setPauseButtonMode(ui->pushButtonPowerTestPause, /*isPlayIcon=*/playbackPaused);
    }
    if (ui->pushButtonPowerTestStop) {
        ui->pushButtonPowerTestStop->setVisible(true);
    }
    updatePowerLevelRadioButtonsEnabled();
}

double MainWindow::currentPowerGraphCenterDbm(int tractOverride) const
{
    const int tractNum = (tractOverride > 0) ? tractOverride
                                             : ((m_ppmCurrentOnTract > 0) ? m_ppmCurrentOnTract
                                                                          : selectedPpmTractFromUi());
    // По ТЗ: для тракта №4 (выбор в framePPM) центр зелёной зоны графика мощности:
    // - PowLevelMax: 40 dBm
    // - PowLevelMin: 30 dBm
    if (tractNum == 4) {
        return (m_powerLevelCode == 1) ? 30.0 : 40.0;
    }
    if (m_powerLevelCode != 1) {
        return kPowerGraphMaxLevelCenterDbm;
    }
    if (tractNum <= 0) {
        return kPowerGraphMinLevelCenterDbmTrmType4;
    }
    const int trmType = m_ppmTrmTypeByTract.value(tractNum, -1);
    if (trmType == 2 || trmType == 3) {
        return kPowerGraphMinLevelCenterDbmTrmType23;
    }
    return kPowerGraphMinLevelCenterDbmTrmType4;
}

void MainWindow::applyPowerGraphCenterScale()
{
    if (!ui || !ui->plotWidgetPowerGraph) {
        return;
    }

    const double centerDbm = currentPowerGraphCenterDbm();
    m_powerGraphAutoYCenterDbm = centerDbm;
    m_powerGraphAutoYInitialized = true;
    ui->plotWidgetPowerGraph->yAxis->setRange(centerDbm - kPowerGraphInitialYHalfRangeDbm,
                                              centerDbm + kPowerGraphInitialYHalfRangeDbm);
    initPowerGraphHelperRects();
    updatePowerGraphHelperRectsXSpan();
    ui->plotWidgetPowerGraph->replot(QCustomPlot::rpQueuedReplot);
}

void MainWindow::clearPowerGraphPlotCurves()
{
    hidePowerGraphHoverLabel();
    m_powerGraphFreqsMHz.clear();
    m_powerGraphAmpsDbm.clear();
    m_powerGraphTargetFreqsHz.clear();
    m_powerStepAmpAccumDbm = 0.0;
    m_powerStepAmpSampleCount = 0;
    m_powerStepBestValid = false;

    const double centerDbm = currentPowerGraphCenterDbm();
    m_powerGraphAutoYInitialized = true;
    m_powerGraphAutoYCenterDbm = centerDbm;

    if (m_powerGraphTrace) {
        m_powerGraphTrace->data()->clear();
    }
    if (m_powerGraphScatterOk) {
        m_powerGraphScatterOk->data()->clear();
    }
    if (m_powerGraphScatterBad) {
        m_powerGraphScatterBad->data()->clear();
    }
    updatePowerGraphScatterLayers();
}

void MainWindow::updatePowerLevelRadioButtonsEnabled()
{
    if (!ui) {
        return;
    }
    const int tractNum = (m_ppmCurrentOnTract > 0) ? m_ppmCurrentOnTract : selectedPpmTractFromUi();
    const bool powerTestSession =
        (ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked())
        || m_powerTestPaused;
    const bool tractReady = (tractNum > 0) && isPpmTractReadyForPowerTest(tractNum);
    const bool showFrame = tractReady && !powerTestSession;

    if (ui->framePowerLevel) {
        ui->framePowerLevel->setVisible(showFrame);
    }
    if (ui->framePowerTestOptions) {
        ui->framePowerTestOptions->setVisible(showFrame);
    }
    if (ui->checkPowerPrimaryCheck) {
        ui->checkPowerPrimaryCheck->setEnabled(showFrame);
    }
    if (ui->checkPowerFullRange) {
        const int trmType = m_ppmTrmTypeByTract.value(tractNum, -1);
        const bool fullRangeApplicable = (trmType == 2 || trmType == 3);
        ui->checkPowerFullRange->setEnabled(showFrame && fullRangeApplicable);
        if (!fullRangeApplicable && ui->checkPowerFullRange->isChecked()) {
            QSignalBlocker blocker(ui->checkPowerFullRange);
            ui->checkPowerFullRange->setChecked(false);
        }
    }
}

void MainWindow::onPowerTestOptionsChanged()
{
    if (!ui) {
        return;
    }
    const bool powerTestSession =
        (ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked())
        || m_powerTestPaused;
    if (powerTestSession) {
        return;
    }

    const int tractNum = (m_ppmCurrentOnTract > 0) ? m_ppmCurrentOnTract : selectedPpmTractFromUi();
    const int trmType = m_ppmTrmTypeByTract.value(tractNum, -1);
    if (sender() == ui->checkPowerFullRange && trmType != 2 && trmType != 3) {
        if (ui->checkPowerFullRange->isChecked()) {
            QSignalBlocker blocker(ui->checkPowerFullRange);
            ui->checkPowerFullRange->setChecked(false);
        }
        return;
    }

    if (!ui->plotWidgetPowerGraph) {
        return;
    }
    const bool fullRange = ui->checkPowerFullRange && ui->checkPowerFullRange->isChecked();
    double xLo = 30.0;
    double xHi = 180.0;
    powerGraphFreqRangeMHzForTrmType(trmType, fullRange, &xLo, &xHi);
    xLo = qMax(0.0, xLo - 2.0);
    xHi = xHi + 2.0;
    ui->plotWidgetPowerGraph->xAxis->setRange(xLo, xHi);
    updatePowerGraphHelperRectsXSpan();
    // По требованию: диапазон X должен меняться сразу при установке/снятии галки.
    ui->plotWidgetPowerGraph->replot();
}

void MainWindow::onPowerLevelRadioToggled(bool checked)
{
    if (!checked || !ui) {
        return;
    }
    if (m_ignorePowerLevelUiSignal) {
        return;
    }

    clearPowerGraphPlotCurves();

    const bool isMin = (sender() == ui->radioButtonPowLeveMin);
    m_powerLevelCode = isMin ? 1 : 4;

    const int tractNum = (m_ppmCurrentOnTract > 0) ? m_ppmCurrentOnTract : selectedPpmTractFromUi();
    if (tractNum > 0 && m_deviceController && m_deviceController->isConnected()) {
        m_powerLevelCodeByTract.insert(tractNum, m_powerLevelCode);
        if (!m_deviceController->setPowerLevel(static_cast<uint8_t>(tractNum), m_powerLevelCode)) {
            onDeviceLogMessage(QStringLiteral("ОШИБКА: не удалось установить уровень мощности для тракта %1.").arg(tractNum));
        }
    }

    applyPowerGraphCenterScale();
}

void MainWindow::applyPowerLevelUiByCode(uint8_t levelCode, bool rescaleGraph)
{
    const uint8_t normalized = (levelCode <= 1) ? 1 : 4;
    const uint8_t prevNormalized = m_powerLevelCode;
    m_powerLevelCode = normalized;
    if (!ui) {
        return;
    }

    m_ignorePowerLevelUiSignal = true;
    if (ui->radioButtonPowLeveMin) {
        ui->radioButtonPowLeveMin->setChecked(normalized == 1);
    }
    if (ui->radioButtonPowLevelMax) {
        ui->radioButtonPowLevelMax->setChecked(normalized != 1);
    }
    m_ignorePowerLevelUiSignal = false;

    if (rescaleGraph) {
        if (prevNormalized != normalized) {
            clearPowerGraphPlotCurves();
        }
        applyPowerGraphCenterScale();
    }
}

void MainWindow::setEmissionAnimating(bool on)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, on]() { setEmissionAnimating(on); }, Qt::QueuedConnection);
        return;
    }

    if (!ui) {
        return;
    }

    if (!ui->emissionAntennaWidget) {
        return;
    }

    if (on) {
        ui->emissionAntennaWidget->startTransmission();
    } else {
        ui->emissionAntennaWidget->stopTransmission();
    }

    m_emissionAnimating = on;
    if (!on && m_powerMomentPeakLabel) {
        m_powerMomentPeakLabel->setVisible(false);
        if (ui->plotWidgetMomentSpetrumGraph) {
            ui->plotWidgetMomentSpetrumGraph->replot(QCustomPlot::rpQueuedReplot);
        }
    }
}

void MainWindow::initPowerTestingPlots()
{
    if (m_powerPlotsInitialized) {
        return;
    }
    if (!ui->plotWidgetMomentSpetrumGraph || !ui->plotWidgetPowerGraph) {
        return;
    }

    ui->plotWidgetMomentSpetrumGraph->clearItems();
    ui->plotWidgetMomentSpetrumGraph->clearGraphs();
    ui->plotWidgetPowerGraph->clearItems();
    ui->plotWidgetPowerGraph->clearGraphs();

    const double centerMHzDefault = static_cast<double>(kPowerTestStartFreqHz) * 1e-6;
    setupFrequencySweepPlot(ui->plotWidgetMomentSpetrumGraph,
                            centerMHzDefault - kPowerTestMomentHalfWindowMHz,
                            centerMHzDefault + kPowerTestMomentHalfWindowMHz);
    m_powerMomentTraces = createSweepTraces(ui->plotWidgetMomentSpetrumGraph);
    if (m_powerMomentTraces.memoryTrace) {
        m_powerMomentTraces.memoryTrace->setVisible(false);
    }
    ui->plotWidgetMomentSpetrumGraph->legend->setVisible(false);
    // Под осью X показываем текущую измеряемую частоту (в момент передачи).
    ui->plotWidgetMomentSpetrumGraph->xAxis->setLabel(QString());
    ui->plotWidgetMomentSpetrumGraph->yAxis->setLabel(QString());
    ui->plotWidgetMomentSpetrumGraph->xAxis->setTicks(false);
    ui->plotWidgetMomentSpetrumGraph->xAxis->setTickLabels(false);
    ui->plotWidgetMomentSpetrumGraph->yAxis->setTicks(false);
    ui->plotWidgetMomentSpetrumGraph->yAxis->setTickLabels(false);
    ui->plotWidgetMomentSpetrumGraph->yAxis->setRange(-125.0, 0.0);
    // экономим место: уменьшаем отступы (подписи убраны)
    ui->plotWidgetMomentSpetrumGraph->axisRect()->setMargins(QMargins(6, 6, 6, 6));
    if (m_powerMomentTraces.liveTrace) {
        m_powerMomentTraces.liveTrace->data()->clear();
    }
    if (m_powerMomentTraces.fillBaselineGraph) {
        m_powerMomentTraces.fillBaselineGraph->data()->clear();
    }

    // Зелёная подпись со значением dBm в точке пика моментного спектра.
    // Появляется только когда emissionAntennaWidget пульсирует (идёт излучение).
    m_powerMomentPeakLabel = new QCPItemText(ui->plotWidgetMomentSpetrumGraph);
    m_powerMomentPeakLabel->setLayer(QStringLiteral("overlay"));
    m_powerMomentPeakLabel->setClipToAxisRect(false);
    m_powerMomentPeakLabel->position->setType(QCPItemPosition::ptPlotCoords);
    m_powerMomentPeakLabel->setPositionAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    m_powerMomentPeakLabel->setPadding(QMargins(4, 2, 4, 2));
    m_powerMomentPeakLabel->setColor(QColor(QStringLiteral("#4ade80")));
    m_powerMomentPeakLabel->setBrush(Qt::NoBrush);
    m_powerMomentPeakLabel->setPen(Qt::NoPen);
    {
        QFont pf;
        pf.setFamily(QStringLiteral("Consolas"));
        pf.setPixelSize(11);
        pf.setStyleHint(QFont::Monospace);
        pf.setBold(true);
        m_powerMomentPeakLabel->setFont(pf);
    }
    m_powerMomentPeakLabel->setVisible(false);

    // Значения по умолчанию (для TrmType=2). Актуальный диапазон выставляется при старте теста.
    setupFrequencySweepPlot(ui->plotWidgetPowerGraph, 25.0, 180.0);
    ui->plotWidgetPowerGraph->legend->setVisible(false);
    m_powerGraphTrace = ui->plotWidgetPowerGraph->addGraph();
    if (m_powerGraphTrace) {
        m_powerGraphTrace->setPen(QPen(QColor(QStringLiteral("#4ade80")), 1.5));
        m_powerGraphTrace->setLineStyle(QCPGraph::lsLine);
        m_powerGraphTrace->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssNone));
        m_powerGraphTrace->setAdaptiveSampling(false);
    }
    m_powerGraphScatterOk = ui->plotWidgetPowerGraph->addGraph();
    if (m_powerGraphScatterOk) {
        m_powerGraphScatterOk->setPen(Qt::NoPen);
        m_powerGraphScatterOk->setLineStyle(QCPGraph::lsNone);
        m_powerGraphScatterOk->setScatterStyle(
            QCPScatterStyle(QCPScatterStyle::ssDisc, QColor(QStringLiteral("#4ade80")), QColor(QStringLiteral("#4ade80")), 4));
        m_powerGraphScatterOk->setAdaptiveSampling(false);
    }
    m_powerGraphScatterBad = ui->plotWidgetPowerGraph->addGraph();
    if (m_powerGraphScatterBad) {
        m_powerGraphScatterBad->setPen(Qt::NoPen);
        m_powerGraphScatterBad->setLineStyle(QCPGraph::lsNone);
        m_powerGraphScatterBad->setScatterStyle(
            QCPScatterStyle(QCPScatterStyle::ssDisc, QColor(QStringLiteral("#ef4444")), QColor(QStringLiteral("#ef4444")), 4));
        m_powerGraphScatterBad->setAdaptiveSampling(false);
    }
    ui->plotWidgetPowerGraph->xAxis->setLabel(QStringLiteral("Frequency, MHz"));
    ui->plotWidgetPowerGraph->yAxis->setLabel(QStringLiteral("Power, dBm"));
    ui->plotWidgetPowerGraph->xAxis->setNumberFormat(QStringLiteral("f"));
    ui->plotWidgetPowerGraph->xAxis->setNumberPrecision(3);
    // Дефолтный масштаб мощности: границы "красной зоны".
    const double defaultPowerGraphCenterDbm = currentPowerGraphCenterDbm();
    ui->plotWidgetPowerGraph->yAxis->setRange(defaultPowerGraphCenterDbm - kPowerGraphInitialYHalfRangeDbm,
                                              defaultPowerGraphCenterDbm + kPowerGraphInitialYHalfRangeDbm);
    m_powerGraphAutoYCenterDbm = defaultPowerGraphCenterDbm;
    ui->plotWidgetPowerGraph->setSelectionTolerance(14);

    initPowerGraphHelperRects();
    connect(ui->plotWidgetPowerGraph->xAxis,
            static_cast<void (QCPAxis::*)(const QCPRange &)>(&QCPAxis::rangeChanged),
            this,
            [this](const QCPRange &) { updatePowerGraphHelperRectsXSpan(); });

    m_powerGraphHoverLabel = new QCPItemText(ui->plotWidgetPowerGraph);
    m_powerGraphHoverLabel->setLayer(QStringLiteral("overlay"));
    m_powerGraphHoverLabel->setClipToAxisRect(false);
    m_powerGraphHoverLabel->position->setType(QCPItemPosition::ptAbsolute);
    m_powerGraphHoverLabel->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
    m_powerGraphHoverLabel->setPadding(QMargins(6, 4, 6, 4));
    m_powerGraphHoverLabel->setBrush(QBrush(QColor(QStringLiteral("#1e293b"))));
    m_powerGraphHoverLabel->setPen(QPen(QColor(QStringLiteral("#334155")), 2));
    m_powerGraphHoverLabel->setColor(QColor(QStringLiteral("#4ade80")));
    {
        QFont hf;
        hf.setFamily(QStringLiteral("Consolas"));
        hf.setPixelSize(9);
        hf.setStyleHint(QFont::Monospace);
        m_powerGraphHoverLabel->setFont(hf);
    }
    m_powerGraphHoverLabel->setVisible(false);

    connect(ui->plotWidgetPowerGraph,
            static_cast<void (QCustomPlot::*)(QMouseEvent *)>(&QCustomPlot::mouseMove),
            this,
            &MainWindow::onPowerGraphPlotMouseMove);
    ui->plotWidgetPowerGraph->installEventFilter(this);

    // До старта теста на моментном графике не должно быть следов спектра.
    ui->plotWidgetMomentSpetrumGraph->replot(QCustomPlot::rpQueuedReplot);
    ui->plotWidgetPowerGraph->replot();
    m_powerPlotsInitialized = true;
}

void MainWindow::clearPowerMomentSpectrumPlot()
{
    if (!ui || !ui->plotWidgetMomentSpetrumGraph) {
        return;
    }
    if (m_powerMomentTraces.liveTrace) {
        m_powerMomentTraces.liveTrace->data()->clear();
    }
    if (m_powerMomentTraces.fillBaselineGraph) {
        m_powerMomentTraces.fillBaselineGraph->data()->clear();
    }
    if (m_powerMomentPeakLabel) {
        m_powerMomentPeakLabel->setVisible(false);
    }
    ui->plotWidgetMomentSpetrumGraph->xAxis->setLabel(QString());
    ui->plotWidgetMomentSpetrumGraph->replot(QCustomPlot::rpQueuedReplot);
}

void MainWindow::updatePowerTestingPlots(const QVector<double> &freqs, const QVector<double> &amps)
{
    if (!m_analyzerConnected) {
        return;
    }
    if (!m_powerPlotsInitialized || freqs.isEmpty() || amps.size() != freqs.size()) {
        return;
    }
    const bool onPowerTab =
        ui && ui->tabWidget && m_tabPowerIndex >= 0 && ui->tabWidget->currentIndex() == m_tabPowerIndex;
    if (!onPowerTab) {
        return;
    }
    const bool powerTestRunning =
        ui && ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked();

    // Пока тест не начат — моментный график должен быть "пустым" (без подписи частоты).
    if (!powerTestRunning && m_powerMomentDisplayFreqHz == 0) {
        if (ui->plotWidgetMomentSpetrumGraph) {
            ui->plotWidgetMomentSpetrumGraph->xAxis->setLabel(QString());
            if (m_powerMomentTraces.liveTrace) {
                m_powerMomentTraces.liveTrace->data()->clear();
            }
            if (m_powerMomentTraces.fillBaselineGraph) {
                m_powerMomentTraces.fillBaselineGraph->data()->clear();
            }
            if (m_powerMomentPeakLabel) {
                m_powerMomentPeakLabel->setVisible(false);
            }
            ui->plotWidgetMomentSpetrumGraph->replot(QCustomPlot::rpQueuedReplot);
        }
        return;
    }

    // Определяем, какой это кадр: moment (≈kPowerTestAnalyzerSpanHz) или power (≈kPowerGraphWideSpanHz).
    const double frameLoMHz = qMin(freqs.first(), freqs.last());
    const double frameHiMHz = qMax(freqs.first(), freqs.last());
    const double frameSpanMHz = frameHiMHz - frameLoMHz;

    const quint64 centerHz = powerTestRunning ? m_powerTestCurrentFreqHz : m_powerMomentDisplayFreqHz;
    const double centerMHz = static_cast<double>(centerHz) * 1e-6;

    const double expectedMomentSpanMHz = static_cast<double>(kPowerTestAnalyzerSpanHz) * 1e-6;
    const double expectedPowerSpanMHz = static_cast<double>(kPowerGraphWideSpanHz) * 1e-6;
    const bool isPowerFrame = (std::abs(frameSpanMHz - expectedPowerSpanMHz)
                               < std::abs(frameSpanMHz - expectedMomentSpanMHz));

    if (!isPowerFrame) {
        // Как было раньше: moment spectrum plot показывает узкое окно вокруг несущей.
        const double windowLoMHz = centerMHz - kPowerTestMomentHalfWindowMHz;
        const double windowHiMHz = centerMHz + kPowerTestMomentHalfWindowMHz;

        QVector<double> localFreqs;
        QVector<double> localAmps;
        localFreqs.reserve(freqs.size());
        localAmps.reserve(amps.size());
        for (int i = 0; i < freqs.size(); ++i) {
            const double f = freqs.at(i);
            if (f >= windowLoMHz && f <= windowHiMHz) {
                localFreqs.push_back(f);
                localAmps.push_back(amps.at(i));
            }
        }

        if (m_powerMomentTraces.liveTrace) {
            m_powerMomentTraces.liveTrace->setData(localFreqs, localAmps);
        }
        if (m_powerMomentTraces.fillBaselineGraph) {
            QVector<double> base(localFreqs.size(), -150.0);
            m_powerMomentTraces.fillBaselineGraph->setData(localFreqs, base);
        }

        // Пока emissionAntennaWidget пульсирует (идёт излучение) — показать в точке пика
        // значение в dBm (с учётом смещения радиотракта): зелёное если в пределах допуска,
        // красное если вне зелёной зоны center±kPowerGraphGreenHalfWidthDbm.
        if (m_powerMomentPeakLabel) {
            if (m_emissionAnimating && !localAmps.isEmpty()) {
                int peakIdx = 0;
                double peakAmp = localAmps.at(0);
                for (int i = 1; i < localAmps.size(); ++i) {
                    if (localAmps.at(i) > peakAmp) {
                        peakAmp = localAmps.at(i);
                        peakIdx = i;
                    }
                }
                const double peakFreqMHz = localFreqs.at(peakIdx);
                const double peakRealDbm = powerGraphAnalyzerToRealDbm(peakAmp);
                const double centerDbm = currentPowerGraphCenterDbm();
                const bool insideBand = powerAmpInsideGreenBand(peakRealDbm, centerDbm);
                m_powerMomentPeakLabel->setText(QString::number(peakRealDbm, 'f', 1) + QStringLiteral(" dBm"));
                m_powerMomentPeakLabel->setColor(insideBand ? QColor(QStringLiteral("#4ade80"))
                                                            : QColor(QStringLiteral("#ef4444")));
                m_powerMomentPeakLabel->position->setCoords(peakFreqMHz, peakAmp);
                m_powerMomentPeakLabel->setVisible(true);
            } else if (m_powerMomentPeakLabel->visible()) {
                m_powerMomentPeakLabel->setVisible(false);
            }
        }

        if (ui->plotWidgetMomentSpetrumGraph) {
            ui->plotWidgetMomentSpetrumGraph->xAxis->setLabel(formatHzTriplet(centerHz));
            QSignalBlocker bx(ui->plotWidgetMomentSpetrumGraph->xAxis);
            ui->plotWidgetMomentSpetrumGraph->xAxis->setRange(windowLoMHz, windowHiMHz);
            ui->plotWidgetMomentSpetrumGraph->yAxis->setRange(-125.0, 0.0);
            ui->plotWidgetMomentSpetrumGraph->replot(QCustomPlot::rpQueuedReplot);
        }
        return;
    }

    // Power-кадр: используется ТОЛЬКО для оценки мощности на plotWidgetPowerGraph.
    if (!powerTestRunning || !m_powerMeasurementRunning) {
        return;
    }

    // Выбираем ближайшую к искомой частоту и её амплитуду.
    int nearestIdx = 0;
    double nearestDiff = std::abs(freqs.at(0) - centerMHz);
    for (int i = 1; i < freqs.size(); ++i) {
        const double d = std::abs(freqs.at(i) - centerMHz);
        if (d < nearestDiff) {
            nearestDiff = d;
            nearestIdx = i;
        }
    }
    const double nearestFreqMHz = freqs.at(nearestIdx);
    const double nearestAmpDbm = powerGraphAnalyzerToRealDbm(amps.at(nearestIdx));

    // Из всех "ближайших" частот за окно измерения выбираем ту, где амплитуда максимальна (без усреднения).
    if (!m_powerStepBestValid || nearestAmpDbm > m_powerStepBestAmpDbm) {
        m_powerStepBestValid = true;
        m_powerStepBestAmpDbm = nearestAmpDbm;
        m_powerStepBestFreqMHz = nearestFreqMHz;
    }
}

bool MainWindow::startPowerMeasurementStep()
{
    if (m_powerTestSequenceIndex < 0 || m_powerTestSequenceIndex >= m_powerTestSequenceFreqsHz.size()) {
        return false;
    }
    if (!m_deviceController || !m_powerTrafficGenerator) {
        return false;
    }

    const quint64 freqHz = m_powerTestSequenceFreqsHz.at(m_powerTestSequenceIndex);
    if (freqHz == 0 || freqHz > static_cast<quint64>(std::numeric_limits<uint32_t>::max())) {
        DEBUG << QStringLiteral("ОШИБКА: частота шага вне диапазона.");
        return false;
    }

    if (!m_deviceController->setFrequencyTx(m_powerTestTargetTract, static_cast<uint32_t>(freqHz))) {
        DEBUG << QStringLiteral("ОШИБКА: не удалось установить частоту %1 Гц.")
                     .arg(formatGroupedWithDots(freqHz));
        // В момент потери Ethernet запись в сокет может начать падать раньше watchdog.
        // Не сбрасываем тест в idle: переводим в внешнюю паузу с сохранением UI-кнопок.
        pausePowerTestForStationDisconnect();
        m_powerTestBlockedByPpm = true;
        return false;
    }

    m_powerTestCurrentFreqHz = freqHz;
    m_powerMomentDisplayFreqHz = freqHz;
    m_powerStepAmpAccumDbm = 0.0;
    m_powerStepAmpSampleCount = 0;
    m_powerStepBestValid = false;
    m_powerStepBestFreqMHz = 0.0;
    m_powerStepBestAmpDbm = -200.0;
    m_powerMeasurementRunning = false;
    m_powerTrafficStartPending = true;

    const quint64 halfNarrowSpanHz = kPowerTestAnalyzerSpanHz / 2ULL;
    const quint64 narrowStartHz = (freqHz > halfNarrowSpanHz) ? (freqHz - halfNarrowSpanHz) : 1ULL;
    const quint64 narrowStopHz = freqHz + halfNarrowSpanHz;

    const quint64 halfWideSpanHz = kPowerGraphWideSpanHz / 2ULL;
    const quint64 wideStartHz = (freqHz > halfWideSpanHz) ? (freqHz - halfWideSpanHz) : 1ULL;
    const quint64 wideStopHz = freqHz + halfWideSpanHz;

    // Для tabPower: чередуем запросы 1 МГц (moment) и kPowerGraphWideSpanHz (power) внутри одного стрима.
    if (m_analyzerController) {
        m_analyzerController->setAlternateSpectrumRanges(narrowStartHz, narrowStopHz, wideStartHz, wideStopHz);
        m_analyzerController->setAlternateSpectrumRangesEnabled(true);
    }
    // sweep bounds (для clamp) оставим по узкому диапазону, т.к. он отображается на moment графике
    syncSweepBoundsFromHz(narrowStartHz, narrowStopHz);
    m_spectrumGridAlignPending = false;
    m_spectrumGridAlignAttemptsLeft = 0;
    if (ui->plotWidgetAnalyzer) {
        QSignalBlocker bx(ui->plotWidgetAnalyzer->xAxis);
        ui->plotWidgetAnalyzer->xAxis->setRange(narrowStartHz / 1e6, narrowStopHz / 1e6);
    }
    if (ui->plotWidgetMomentSpetrumGraph) {
        ui->plotWidgetMomentSpetrumGraph->xAxis->setLabel(formatHzTriplet(freqHz));
    }
    if (!m_spectrumStreaming) {
        m_analyzerController->startSpectrumStream();
        m_spectrumStreaming = true;
    }

    DEBUG << QStringLiteral("📡 Шаг %1/%2: F=%3 Гц, multicast %4. Пауза 1 сек перед выходом на мощность.")
                 .arg(m_powerTestSequenceIndex + 1)
                 .arg(m_powerTestSequenceFreqsHz.size())
                 .arg(formatGroupedWithDots(freqHz))
                 .arg(m_powerTestMulticastAddress);
    m_powerTestBeforePowerOnTimer.start(kPowerTestPauseBetweenStepsMs);
    return true;
}

void MainWindow::finishPowerMeasurementStep()
{
    if (m_powerTestSequenceIndex < 0 || m_powerTestSequenceIndex >= m_powerTestSequenceFreqsHz.size()) {
        return;
    }

    m_powerTestAutoStopTimer.stop();
    m_powerTestBeforePowerOnTimer.stop();
    m_powerMeasurementRunning = false;
    m_powerTrafficStartPending = false;
    if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
        m_powerTrafficGenerator->stop();
    }
    setEmissionAnimating(false);

    const quint64 freqHz = m_powerTestSequenceFreqsHz.at(m_powerTestSequenceIndex);
    const double centerDbm = m_powerGraphAutoYCenterDbm;
    const double greenLoDbm = centerDbm - kPowerGraphGreenHalfWidthDbm;
    const double greenHiDbm = centerDbm + kPowerGraphGreenHalfWidthDbm;
    bool shouldRetryCurrentFrequency = false;
    bool shouldStorePoint = true;
    if (m_powerStepBestValid && m_powerGraphTrace && ui->plotWidgetPowerGraph) {
        const double bestAmpDbm = m_powerStepBestAmpDbm;
        const double sampleFreqMHz = m_powerStepBestFreqMHz;

        const bool insideBand = powerAmpInsideGreenBand(bestAmpDbm, centerDbm);
        if (!insideBand && m_powerTestCurrentFreqRetryCount < kPowerTestRemeasureMaxCount) {
            ++m_powerTestCurrentFreqRetryCount;
            shouldRetryCurrentFrequency = true;
            shouldStorePoint = false;
            DEBUG << QStringLiteral("⚠ Амплитуда %1 dBm на F=%2 Гц вне допуска [%3; %4] dBm. Переизмерение %5/%6 на этой же частоте.")
                         .arg(QString::number(bestAmpDbm, 'f', 2))
                         .arg(formatGroupedWithDots(freqHz))
                         .arg(QString::number(greenLoDbm, 'f', 1))
                         .arg(QString::number(greenHiDbm, 'f', 1))
                         .arg(m_powerTestCurrentFreqRetryCount)
                         .arg(kPowerTestRemeasureMaxCount);
        } else if (!insideBand) {
            DEBUG << QStringLiteral("⚠ Амплитуда %1 dBm на F=%2 Гц вне допуска [%3; %4] dBm после %5 переизмерений — фиксируем результат.")
                         .arg(QString::number(bestAmpDbm, 'f', 2))
                         .arg(formatGroupedWithDots(freqHz))
                         .arg(QString::number(greenLoDbm, 'f', 1))
                         .arg(QString::number(greenHiDbm, 'f', 1))
                         .arg(kPowerTestRemeasureMaxCount);
        }

        if (shouldStorePoint) {
            // Базовый диапазон Y задаётся сразу от границ красной зоны с учетом радиотракта,
            // а при выходе точки за границы — расширяем, чтобы она не сливалась с рамкой.
            if (!m_powerGraphAutoYInitialized && ui->plotWidgetPowerGraph) {
                ui->plotWidgetPowerGraph->yAxis->setRange(centerDbm - kPowerGraphInitialYHalfRangeDbm,
                                                          centerDbm + kPowerGraphInitialYHalfRangeDbm);
                m_powerGraphAutoYInitialized = true;
                m_powerGraphAutoYCenterDbm = centerDbm;
                initPowerGraphHelperRects();
            }

            int insertPos = -1;
            for (int i = 0; i < m_powerGraphFreqsMHz.size(); ++i) {
                if (std::abs(m_powerGraphFreqsMHz.at(i) - sampleFreqMHz) < 1e-6) {
                    insertPos = i;
                    break;
                }
                if (m_powerGraphFreqsMHz.at(i) > sampleFreqMHz) {
                    insertPos = i;
                    m_powerGraphFreqsMHz.insert(i, sampleFreqMHz);
                    m_powerGraphAmpsDbm.insert(i, bestAmpDbm);
                    m_powerGraphTargetFreqsHz.insert(i, freqHz);
                    break;
                }
            }
            if (insertPos < 0) {
                m_powerGraphFreqsMHz.push_back(sampleFreqMHz);
                m_powerGraphAmpsDbm.push_back(bestAmpDbm);
                m_powerGraphTargetFreqsHz.push_back(freqHz);
            } else if (insertPos < m_powerGraphAmpsDbm.size()
                       && std::abs(m_powerGraphFreqsMHz.at(insertPos) - sampleFreqMHz) < 1e-6) {
                m_powerGraphAmpsDbm[insertPos] = bestAmpDbm;
                if (insertPos < m_powerGraphTargetFreqsHz.size()) {
                    m_powerGraphTargetFreqsHz[insertPos] = freqHz;
                }
            }

            updatePowerGraphScatterLayers();
            updatePowerGraphHelperRectsXSpan();

            // Расширение диапазона Y при выходе точки за границы (без сужения).
            if (ui->plotWidgetPowerGraph) {
                constexpr double kPadDbm = 5.0; // запас, чтобы точка не "прилипала" к границе
                QCPRange yr = ui->plotWidgetPowerGraph->yAxis->range();
                bool changed = false;
                if (bestAmpDbm < yr.lower) {
                    yr.lower = bestAmpDbm - kPadDbm;
                    changed = true;
                }
                if (bestAmpDbm > yr.upper) {
                    yr.upper = bestAmpDbm + kPadDbm;
                    changed = true;
                }
                if (changed) {
                    ui->plotWidgetPowerGraph->yAxis->setRange(yr);
                }
            }
            ui->plotWidgetPowerGraph->replot(QCustomPlot::rpQueuedReplot);

            DEBUG << QStringLiteral("⏱ Замер завершен: F=%1 Гц, максимум %2 dBm (bin %3 MHz).")
                         .arg(formatGroupedWithDots(freqHz))
                         .arg(QString::number(bestAmpDbm, 'f', 2))
                         .arg(QString::number(sampleFreqMHz, 'f', 6));
        }
    } else {
        DEBUG << QStringLiteral("⏱ Замер завершен: F=%1 Гц, точки спектра за 5 секунд не получены.")
                     .arg(formatGroupedWithDots(freqHz));
    }

    if (shouldRetryCurrentFrequency) {
        DEBUG << QStringLiteral("Пауза 2 секунды перед повторным выходом на мощность на той же частоте...");
        m_powerTestStepPauseTimer.start(kPowerTestPauseBetweenRemeasureMs);
        return;
    }

    m_powerTestCurrentFreqRetryCount = 0;
    ++m_powerTestSequenceIndex;
    if (m_powerTestSequenceIndex >= m_powerTestSequenceFreqsHz.size()) {
        onDeviceLogMessage(QStringLiteral("✅ Тест замера мощности завершен."));
        if (ui && ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked()) {
            ui->pushButtonStartTestingPower->setChecked(false);
        }
        return;
    }

    DEBUG << QStringLiteral("Пауза 1 секунда перед следующей частотой...");
    m_powerTestStepPauseTimer.start(kPowerTestPauseBetweenStepsMs);
}

void MainWindow::onPowerTestingToggled(bool checked)
{
    if (!ui->pushButtonStartTestingPower) {
        return;
    }
    struct PowerLevelRadiosUpdateGuard {
        MainWindow *mw;
        explicit PowerLevelRadiosUpdateGuard(MainWindow *w) : mw(w) {}
        ~PowerLevelRadiosUpdateGuard()
        {
            if (mw) {
                mw->updatePowerLevelRadioButtonsEnabled();
            }
        }
    } powerLevelRadiosGuard(this);

    // По ТЗ: текст кнопки и блокировка вкладок зависят от состояния теста.
    ui->pushButtonStartTestingPower->setText(
        checked ? QStringLiteral("ОСТАНОВИТЬ ТЕСТ МОЩНОСТИ") : QStringLiteral("НАЧАТЬ ТЕСТ МОЩНОСТИ"));
    updateTabWidgetLockState();
    if (checked) {
        const bool resumingPausedTest = m_powerTestPaused;
        setPowerTestControlsRunning(false);
        // По требованию: "ножка/излучатель" виден сразу при старте теста,
        // а пульсация включается только по индикации реального TX (IND_CHREADY).
        if (ui->emissionAntennaWidget) {
            ui->emissionAntennaWidget->stopTransmission();
            ui->emissionAntennaWidget->setVisible(true);
        }
        // Resume after pause on "Нет связи с ПП"
        if (m_powerTestPaused) {
            if (!m_deviceController || !m_deviceController->isConnected()) {
                DEBUG << QStringLiteral("ОШИБКА: нет подключения к радиостанции (нужно подключиться).");
                QSignalBlocker blocker(ui->pushButtonStartTestingPower);
                ui->pushButtonStartTestingPower->setChecked(false);
                setPowerTestControlsRunning(true);
                return;
            }
            if (!m_powerTrafficGenerator) {
                DEBUG << QStringLiteral("ОШИБКА: генератор трафика не инициализирован.");
                QSignalBlocker blocker(ui->pushButtonStartTestingPower);
                ui->pushButtonStartTestingPower->setChecked(false);
                setPowerTestControlsRunning(true);
                return;
            }
            if (m_powerTestBlockedByStationDisconnect) {
                DEBUG << QStringLiteral(
                    "ППМ: тест мощности ожидает автоматического продолжения после восстановления связи с радиостанцией.");
                QSignalBlocker blocker(ui->pushButtonStartTestingPower);
                ui->pushButtonStartTestingPower->setChecked(false);
                ui->pushButtonStartTestingPower->setText(QStringLiteral("НАЧАТЬ ТЕСТ МОЩНОСТИ"));
                setPowerTestControlsRunning(true);
                return;
            }
            if (m_powerTestBlockedByAnalyzerDisconnect) {
                DEBUG << QStringLiteral(
                    "ППМ: тест мощности ожидает автоматического продолжения после восстановления связи с анализатором.");
                QSignalBlocker blocker(ui->pushButtonStartTestingPower);
                ui->pushButtonStartTestingPower->setChecked(false);
                ui->pushButtonStartTestingPower->setText(QStringLiteral("НАЧАТЬ ТЕСТ МОЩНОСТИ"));
                setPowerTestControlsRunning(true);
                return;
            }
            if (m_powerTestBlockedByPpm) {
                DEBUG << QStringLiteral("ППМ: тест мощности не может быть продолжен — нет связи с ПП.");
                QSignalBlocker blocker(ui->pushButtonStartTestingPower);
                ui->pushButtonStartTestingPower->setChecked(false);
                ui->pushButtonStartTestingPower->setText(QStringLiteral("НАЧАТЬ ТЕСТ МОЩНОСТИ"));
                setPowerTestControlsRunning(true);
                return;
            }
            if (m_powerTestSequenceIndex < 0 || m_powerTestSequenceIndex >= m_powerTestSequenceFreqsHz.size()
                || m_powerTestSequenceFreqsHz.isEmpty()) {
                // Нечего продолжать — считаем как новый запуск.
                m_powerTestPaused = false;
            } else {
                m_powerTestPaused = false;
                m_powerTestStepPauseTimer.stop();
                if (!startPowerMeasurementStep()) {
                    if (m_powerTestPaused) {
                        setPowerTestControlsRunning(true);
                        return;
                    }
                    QSignalBlocker blocker(ui->pushButtonStartTestingPower);
                    ui->pushButtonStartTestingPower->setChecked(false);
                    setPowerTestControlsIdle();
                    return;
                }
                if (resumingPausedTest) {
                    onDeviceLogMessage(QStringLiteral("▶ Продолжен тест замера мощности."));
                }
                setPowerTestControlsRunning(false);
                return;
            }
        }

        if (!m_deviceController || !m_deviceController->isConnected()) {
            DEBUG << QStringLiteral("ОШИБКА: нет подключения к радиостанции (нужно подключиться).");
            QSignalBlocker blocker(ui->pushButtonStartTestingPower);
            ui->pushButtonStartTestingPower->setChecked(false);
            setPowerTestControlsIdle();
            return;
        }
        if (!m_powerTrafficGenerator) {
            DEBUG << QStringLiteral("ОШИБКА: генератор трафика не инициализирован.");
            QSignalBlocker blocker(ui->pushButtonStartTestingPower);
            ui->pushButtonStartTestingPower->setChecked(false);
            setPowerTestControlsIdle();
            return;
        }

        const int selectedTract = (m_ppmCurrentOnTract > 0) ? m_ppmCurrentOnTract : ppmFirstTractNumber();
        m_powerTestTargetTract = static_cast<uint8_t>(selectedTract > 0 ? selectedTract : DEFAULT_TRACT_NUM);
        const int targetTrmType = m_ppmTrmTypeByTract.value(static_cast<int>(m_powerTestTargetTract), -1);
        m_powerTestTargetTrmType = targetTrmType;
        QString multicastAddress = QString::fromLatin1(TRAFFIC_MCAST_IP);
        switch (targetTrmType) {
        case 2:
            multicastAddress = QStringLiteral("224.0.1.2");
            break;
        case 3:
            multicastAddress = QStringLiteral("224.0.1.3");
            break;
        case 4:
            multicastAddress = QStringLiteral("224.0.1.4");
            break;
        default:
            DEBUG << QStringLiteral(
                "ПРЕДУПРЕЖДЕНИЕ: TrmType для текущего тракта не определен, используется адрес по умолчанию 224.0.1.3.");
            break;
        }
        m_powerTestMulticastAddress = multicastAddress;
        if (!m_powerPlotsInitialized) {
            initPowerTestingPlots();
        }

        if (ui->plotWidgetPowerGraph) {
            const double centerDbm = currentPowerGraphCenterDbm();
            // Важно: старт/стоп теста НЕ должен менять видимый масштаб графика.
            // Диапазоны осей выставляются при первом показе графика и/или пользователем (zoom/drag).
            m_powerGraphAutoYCenterDbm = centerDbm;
            initPowerGraphHelperRects();
            updatePowerGraphHelperRectsXSpan();
            ui->plotWidgetPowerGraph->replot(QCustomPlot::rpQueuedReplot);
        }

        {
            const bool primaryCheck = ui->checkPowerPrimaryCheck && ui->checkPowerPrimaryCheck->isChecked();
            const bool fullRange = ui->checkPowerFullRange && ui->checkPowerFullRange->isChecked();
            m_powerTestSequenceFreqsHz =
                buildPowerTestFrequencyList(m_powerTestTargetTrmType, primaryCheck, fullRange);
        }
        m_powerTestPaused = false;
        m_powerTestSequenceIndex = 0;
        m_powerTestCurrentFreqRetryCount = 0;
        m_powerMeasurementRunning = false;
        m_powerStepAmpAccumDbm = 0.0;
        m_powerStepAmpSampleCount = 0;
        m_powerGraphFreqsMHz.clear();
        m_powerGraphAmpsDbm.clear();
        m_powerGraphTargetFreqsHz.clear();
        // Авто-Y инициализируем по факту первой точки (если понадобится расширение диапазона),
        // но сам диапазон оси при старте не трогаем.
        m_powerGraphAutoYInitialized = false;
        m_powerGraphAutoYCenterDbm = currentPowerGraphCenterDbm();
        if (m_powerGraphTrace) {
            m_powerGraphTrace->data()->clear();
        }
        if (m_powerGraphScatterOk) {
            m_powerGraphScatterOk->data()->clear();
        }
        if (m_powerGraphScatterBad) {
            m_powerGraphScatterBad->data()->clear();
        }
        if (ui->plotWidgetPowerGraph) {
            initPowerGraphHelperRects();
            updatePowerGraphHelperRectsXSpan();
            ui->plotWidgetPowerGraph->replot(QCustomPlot::rpQueuedReplot);
        }
        m_powerTestStepPauseTimer.stop();
        if (!startPowerMeasurementStep()) {
            if (m_powerTestPaused) {
                setPowerTestControlsRunning(true);
                return;
            }
            ui->pushButtonStartTestingPower->setChecked(false);
            setPowerTestControlsIdle();
            return;
        }
        if (!resumingPausedTest) {
            onDeviceLogMessage(QStringLiteral("▶ Начат тест замера %1 мощности на тракте %2.")
                                   .arg(powerTestPowerKindAdjectiveForLog(), powerTestTractDisplayNameForLog()));
        }
    } else {
        setPowerTestControlsIdle();
        m_powerTestPaused = false;
        m_powerTestBlockedByPpm = false;
        m_powerTestBlockedByStationDisconnect = false;
        m_powerTestBlockedByAnalyzerDisconnect = false;
        m_powerTestBlockedByAntFault = false;
        m_powerTestBlockedByDirRestore = false;
        m_powerTestTargetTract = 0U;
        m_powerTestTargetTrmType = -1;
        m_powerTestAutoStopTimer.stop();
        m_powerTestStepPauseTimer.stop();
        m_powerTestBeforePowerOnTimer.stop();
        m_powerTestCurrentFreqHz = 0;
        m_powerMeasurementRunning = false;
        m_powerTrafficStartPending = false;
        m_powerTestSequenceIndex = -1;
        m_powerTestCurrentFreqRetryCount = 0;
        m_powerTestSequenceFreqsHz.clear();
        m_powerStepAmpAccumDbm = 0.0;
        m_powerStepAmpSampleCount = 0;
        m_powerStepBestValid = false;
        m_powerStepBestFreqMHz = 0.0;
        m_powerStepBestAmpDbm = -200.0;
        if (m_analyzerController) {
            m_analyzerController->setAlternateSpectrumRangesEnabled(false);
        }
        if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
            DEBUG << QStringLiteral("⏹ Остановка теста мощности: остановка генератора трафика...");
            m_powerTrafficGenerator->stop();
        }
        const bool stayStreamingOnPowerTab =
            (ui && ui->tabWidget && m_tabPowerIndex >= 0 && ui->tabWidget->currentIndex() == m_tabPowerIndex
             && m_powerMomentDisplayFreqHz > 0);
        if (!m_startSpectrumOnHands && !stayStreamingOnPowerTab) {
            stopSpectrumStream();
        } else if (stayStreamingOnPowerTab && m_analyzerController && m_powerMomentDisplayFreqHz > 0) {
            const quint64 halfSpanHz = kPowerTestAnalyzerSpanHz / 2ULL;
            const quint64 sweepStartHz = (m_powerMomentDisplayFreqHz > halfSpanHz)
                                             ? (m_powerMomentDisplayFreqHz - halfSpanHz)
                                             : 1ULL;
            const quint64 sweepStopHz = m_powerMomentDisplayFreqHz + halfSpanHz;
            m_analyzerController->setAlternateSpectrumRangesEnabled(false);
            m_analyzerController->setSpectrumRange(sweepStartHz, sweepStopHz);
            syncSweepBoundsFromHz(sweepStartHz, sweepStopHz);
            if (!m_spectrumStreaming) {
                m_analyzerController->startSpectrumStream();
                m_spectrumStreaming = true;
            }
            if (ui->plotWidgetMomentSpetrumGraph) {
                ui->plotWidgetMomentSpetrumGraph->xAxis->setLabel(formatHzTriplet(m_powerMomentDisplayFreqHz));
            }
        }
        updateTabWidgetLockState();
        m_powerTestUserStopRequested = false;
    }
}

void MainWindow::onPowerTestPauseClicked()
{
    if (!ui || !ui->pushButtonStartTestingPower || !ui->pushButtonPowerTestPause) {
        return;
    }

    // Пауза: останавливаем таймеры/трафик, сохраняем индекс/последовательность, чтобы продолжить с той же частоты.
    if (!m_powerTestPaused) {
        onDeviceLogMessage(QStringLiteral("⏸ Тест замера %1 мощности на тракте %2 поставлен на паузу.")
                               .arg(powerTestPowerKindAdjectiveForLog(), powerTestTractDisplayNameForLog()));
        m_powerTestAutoStopTimer.stop();
        m_powerTestStepPauseTimer.stop();
        m_powerTestBeforePowerOnTimer.stop();
        m_powerMeasurementRunning = false;
        m_powerTrafficStartPending = false;
        setEmissionAnimating(false);
        if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
            m_powerTrafficGenerator->stop();
        }
        m_powerTestPaused = true;

        // Снимаем checked без вызова onPowerTestingToggled(false), чтобы не сбросить прогресс.
        if (ui->pushButtonStartTestingPower->isChecked()) {
            QSignalBlocker blocker(ui->pushButtonStartTestingPower);
            ui->pushButtonStartTestingPower->setChecked(false);
        }
        ui->pushButtonStartTestingPower->setText(QStringLiteral("НАЧАТЬ ТЕСТ МОЩНОСТИ"));
        updateTabWidgetLockState();
        setPowerTestControlsRunning(true);
        return;
    }

    if (m_powerTestBlockedByStationDisconnect || m_powerTestBlockedByAnalyzerDisconnect || m_powerTestBlockedByPpm
        || m_powerTestBlockedByAntFault || m_powerTestBlockedByDirRestore) {
        DEBUG << QStringLiteral(
            "ППМ: ручное продолжение недоступно, ожидается автоматическое возобновление теста.");
        setPowerTestControlsRunning(true);
        return;
    }

    // Продолжение: используем штатный resume-путь через onPowerTestingToggled(true).
    setPowerTestControlsRunning(false);
    ui->pushButtonStartTestingPower->setChecked(true);
}

void MainWindow::onPowerTestStopClicked()
{
    if (!ui || !ui->pushButtonStartTestingPower) {
        return;
    }
    m_powerTestUserStopRequested = true;
    onDeviceLogMessage(QStringLiteral("⏹ Тест замера %1 мощности на тракте %2 остановлен.")
                           .arg(powerTestPowerKindAdjectiveForLog(), powerTestTractDisplayNameForLog()));
    // Стоп: полный сброс через onPowerTestingToggled(false).
    if (ui->pushButtonStartTestingPower->isChecked()) {
        ui->pushButtonStartTestingPower->setChecked(false);
        return;
    }
    const bool hasPowerTestState =
        m_powerTestPaused
        || (m_powerTestSequenceIndex >= 0 && !m_powerTestSequenceFreqsHz.isEmpty());
    if (!hasPowerTestState) {
        m_powerTestUserStopRequested = false;
        setPowerTestControlsIdle();
        return;
    }
    const bool stopDuringAntFault = m_powerTestBlockedByAntFault;
    ++m_powerResumeAfterPpmSerial;
    if (stopDuringAntFault) {
        setPpmUpdateLabelVisible(true);
    }
    onPowerTestingToggled(false);
    updatePowerTestButtonsAccessForSelectedTract();
}

void MainWindow::onTabWidgetCurrentChanged(int index)
{
    if (index >= 0 && !m_tabWidgetWasLocked) {
        m_lastUnlockedTabIndex = index;
    }

    bool isHands = false;
    bool isPower = false;
    bool isFhss = false;
    if (m_tabHandsIndex >= 0) {
        isHands = (index == m_tabHandsIndex);
    } else {
        QWidget *w = ui->tabWidget ? ui->tabWidget->widget(index) : nullptr;
        isHands = (w && w->objectName() == QStringLiteral("tabHands"));
    }
    if (m_tabPowerIndex >= 0) {
        isPower = (index == m_tabPowerIndex);
    } else {
        QWidget *w = ui->tabWidget ? ui->tabWidget->widget(index) : nullptr;
        isPower = (w && w->objectName() == QStringLiteral("tabPower"));
    }
    if (m_tabFhssIndex >= 0) {
        isFhss = (index == m_tabFhssIndex);
    } else {
        QWidget *w = ui->tabWidget ? ui->tabWidget->widget(index) : nullptr;
        isFhss = (w && w->objectName() == QStringLiteral("tabFHSS"));
    }

    m_startSpectrumOnHands = isHands && m_analyzerConnected;

    if (isHands && !m_analyzerConnected) {
        if (isPreTestingHandsOnlyPhase()) {
            updateTabWidgetLockState();
        } else {
            leaveTabHandsIfBlocked();
            updateTabWidgetLockState();
        }
        return;
    }

    if (isHands || isPower || isFhss) {
        if (!m_spectrumPlotInitialized) {
            initSpectrumPlot();
        }
        if (isHands) {
            // По ТЗ: при переходе на tabHands запросы к анализатору должны идти
            // на span 0.5 МГц и частоту из lineEditSpectrumCenterMHz.
            if (m_analyzerController) {
                m_analyzerController->setAlternateSpectrumRangesEnabled(false);
            }
            applyHandsAnalyzerCenterSpan05FromUi();
        }
        if (isFhss) {
            // На вкладке ППРЧ диапазон анализатора и ось X должны соответствовать выбранному тракту.
            int tr = selectedPpmTractFromUi();
            if (tr <= 0) {
                tr = (m_ppmCurrentOnTract > 0) ? m_ppmCurrentOnTract : ppmFirstTractNumber();
            }
            if (isFhssCapableTract(tr)) {
                updateFhssModeComboForTract(tr);
                const bool canPreserveMaxHold =
                    m_fhssKeepMaxHoldUntilNextStart && (m_fhssMaxHoldTract == tr) && m_fhssPlotInitialized
                    && !m_fhssMemoryAmps.isEmpty() && m_fhssTraces.liveTrace && m_fhssTraces.memoryTrace;
                if (canPreserveMaxHold) {
                    const FhssBandSpec spec = currentFhssBandSpec(tr);
                    const double loMHz = static_cast<double>(spec.plotLoHz) * 1e-6;
                    const double hiMHz = static_cast<double>(spec.plotHiHz) * 1e-6;
                    // Не пересоздаём графики: иначе потеряем maxhold. Просто гарантируем диапазон оси.
                    if (ui && ui->plotWidgetFHSSGraph) {
                        QSignalBlocker bx(ui->plotWidgetFHSSGraph->xAxis);
                        ui->plotWidgetFHSSGraph->xAxis->setRange(loMHz, hiMHz);
                        {
                            QSharedPointer<QCPAxisTickerText> ticker(new QCPAxisTickerText);
                            if (spec.isSingle) {
                                const double centerMHz = static_cast<double>(spec.startHz) * 1e-6;
                                ticker->addTick(centerMHz, QString::number(static_cast<int>(centerMHz + 0.5)));
                            } else {
                                const double startMHz = static_cast<double>(spec.startHz) * 1e-6;
                                const double stopMHz = static_cast<double>(spec.stopHz) * 1e-6;
                                if (stopMHz > startMHz && startMHz > 0.0) {
                                    ticker->addTick(startMHz, QString::number(static_cast<int>(startMHz)));
                                    ticker->addTick(stopMHz, QString::number(static_cast<int>(stopMHz)));
                                } else {
                                    ticker->addTick(loMHz, QString::number(static_cast<int>(loMHz)));
                                    ticker->addTick(hiMHz, QString::number(static_cast<int>(hiMHz)));
                                }
                            }
                            ui->plotWidgetFHSSGraph->xAxis->setTicker(ticker);
                            ui->plotWidgetFHSSGraph->xAxis->setSubTicks(false);
                        }
                        applyFhssYAxisForCurrentMode();
                        m_fhssTraces.memoryTrace->setVisible(true);
                        ui->plotWidgetFHSSGraph->replot(QCustomPlot::rpQueuedReplot);
                    }
                } else {
                    applyFhssXAxisForTract(tr);
                }
                updateFhssRangeLcdForTract(tr);
                syncFhssAnalyzerSpectrumRange(tr);
            }
        }
        // Вкладка "Мощность": если моментный график ещё не инициализирован частотой
        // (первое открытие/первый запуск), подставляем стартовую частоту по ВЫБРАННОМУ тракту в framePPM,
        // чтобы сразу видеть спектр как раньше (но без "хвоста" от другого тракта).
        if (isPower && m_powerMomentDisplayFreqHz == 0) {
            int tr = selectedPpmTractFromUi();
            if (tr <= 0) {
                tr = (m_ppmCurrentOnTract > 0) ? m_ppmCurrentOnTract : ppmFirstTractNumber();
            }
            const int trmType = m_ppmTrmTypeByTract.value(tr, -1);
            switch (trmType) {
            case 3:
                m_powerMomentDisplayFreqHz = kPowerTestFrequenciesType3Hz.value(0, static_cast<quint64>(kPowerTestStartFreqType3Hz));
                break;
            case 4:
                m_powerMomentDisplayFreqHz = kPowerTestFrequenciesType4Hz.value(0, static_cast<quint64>(kPowerTestStartFreqType4Hz));
                break;
            case 2:
            default:
                m_powerMomentDisplayFreqHz = kPowerTestFrequenciesType2Hz.value(0, static_cast<quint64>(kPowerTestStartFreqHz));
                break;
            }
            if (ui && ui->plotWidgetMomentSpetrumGraph) {
                const double centerMHz = static_cast<double>(m_powerMomentDisplayFreqHz) * 1e-6;
                ui->plotWidgetMomentSpetrumGraph->xAxis->setLabel(formatHzTriplet(m_powerMomentDisplayFreqHz));
                // Как было раньше: узкое окно вокруг несущей.
                ui->plotWidgetMomentSpetrumGraph->xAxis->setRange(centerMHz - kPowerTestMomentHalfWindowMHz,
                                                                  centerMHz + kPowerTestMomentHalfWindowMHz);
                ui->plotWidgetMomentSpetrumGraph->yAxis->setRange(-125.0, 0.0);
                ui->plotWidgetMomentSpetrumGraph->replot(QCustomPlot::rpQueuedReplot);
            }
        }

        // Диапазон спектра на анализаторе меняем только если есть валидная частота от последнего шага/теста.
        if (isPower && m_powerMomentDisplayFreqHz > 0 && m_analyzerController) {
            const quint64 halfSpanHz = kPowerTestAnalyzerSpanHz / 2ULL;
            const quint64 sweepStartHz = (m_powerMomentDisplayFreqHz > halfSpanHz)
                                             ? (m_powerMomentDisplayFreqHz - halfSpanHz)
                                             : 1ULL;
            const quint64 sweepStopHz = m_powerMomentDisplayFreqHz + halfSpanHz;
            // Для tabPower используем span 1 МГц и (при запуске теста) чередование диапазонов.
            m_analyzerController->setSpectrumRange(sweepStartHz, sweepStopHz);
            syncSweepBoundsFromHz(sweepStartHz, sweepStopHz);
        }
        if (m_analyzerConnected) {
            startSpectrumStream();
        }
    } else {
        stopSpectrumStream();
    }

    bool isReceive = false;
    if (m_tabReceiveIndex >= 0) {
        isReceive = (index == m_tabReceiveIndex);
    } else if (ui && ui->tabWidget) {
        QWidget *tw = ui->tabWidget->widget(index);
        isReceive = (tw && tw->objectName() == QStringLiteral("tabRecieve"));
    }
    if (isReceive && !m_receiveTestRunning) {
        syncReceiveTabPreviewFromCurrentTract();
    }
    syncAnalyzerKeepAliveForCurrentTab();
    updateReceiveResultStripsVisibility();
    updateFhssTestButtonsAccessForSelectedTract();
}

void MainWindow::onSpectrumDataReceived(const QVector<double> &freqs,
                                         const QVector<double> &amps)
{
    if (!m_spectrumStreaming) {
        return;
    }
    if (freqs.isEmpty()) {
        return;
    }

    if (m_spectrumGridAlignPending) {
        if (m_spectrumSweepStopHz <= m_spectrumSweepStartHz) {
            m_spectrumGridAlignPending = false;
            m_spectrumGridAlignAttemptsLeft = 0;
            onDeviceLogMessage(QStringLiteral("Выравнивание сетки: некорректный текущий диапазон sweep."));
        } else if (freqs.size() < 2) {
            // Недостаточно точек, чтобы оценить шаг сетки и корректно сдвинуть диапазон.
        } else {
            const double frameLoMHz = qMin(freqs.first(), freqs.last());
            const double frameHiMHz = qMax(freqs.first(), freqs.last());
            const qint64 curStartHz = static_cast<qint64>(m_spectrumSweepStartHz);
            const qint64 curStopHz = static_cast<qint64>(m_spectrumSweepStopHz);
            const qint64 curSpanHz = curStopHz - curStartHz;
            const double frameLoHz = frameLoMHz * 1e6;
            const double frameHiHz = frameHiMHz * 1e6;
            const double frameSpanHz = frameHiHz - frameLoHz;
            const double maxSpanDeltaHz = qMax(5000.0, 0.20 * static_cast<double>(curSpanHz));

            // Иногда после смены диапазона приходит устаревший кадр от предыдущего sweep.
            // Не используем такие кадры для авто-выравнивания, чтобы не увести диапазон.
            if (frameSpanHz <= 0.0
                || std::abs(frameSpanHz - static_cast<double>(curSpanHz)) > maxSpanDeltaHz) {
                return;
            }

        const double targetMHz = static_cast<double>(m_spectrumGridAlignTargetHz) * 1e-6;
        int nearestIdx = 0;
        double nearestDiffMHz = std::abs(freqs[0] - targetMHz);
        for (int i = 1; i < freqs.size(); ++i) {
            const double d = std::abs(freqs[i] - targetMHz);
            if (d < nearestDiffMHz) {
                nearestDiffMHz = d;
                nearestIdx = i;
            }
        }
        const double errHz = (freqs[nearestIdx] - targetMHz) * 1e6;
        double stepHz = 3000.0;
        if (freqs.size() >= 2) {
            const int ns = qMin(32, freqs.size() - 1);
            double sum = 0.0;
            for (int i = 0; i < ns; ++i) {
                sum += std::abs(freqs[i + 1] - freqs[i]) * 1e6;
            }
            stepHz = sum / ns;
        }
        const double tolHz = qMax(200.0, 0.04 * stepHz);
        if (std::abs(errHz) <= tolHz) {
            m_spectrumGridAlignPending = false;
            m_spectrumGridAlignAttemptsLeft = 0;
        } else if (m_spectrumGridAlignAttemptsLeft <= 0) {
            m_spectrumGridAlignPending = false;
            onDeviceLogMessage(
                QStringLiteral("Выравнивание сетки: остаток %1 Гц после %2 попыток (цель %3 Гц).")
                    .arg(QString::number(errHz, 'f', 1))
                    .arg(kSpectrumGridAlignMaxAttempts)
                    .arg(m_spectrumGridAlignTargetHz));
        } else {
            const qint64 stepHzI = qMax<qint64>(1, static_cast<qint64>(std::llround(stepHz)));
            qint64 shiftHz = -static_cast<qint64>(std::llround(errHz / static_cast<double>(stepHzI))) * stepHzI;
            if (shiftHz == 0) {
                shiftHz = -static_cast<qint64>(std::llround(errHz));
            }
            --m_spectrumGridAlignAttemptsLeft;
            const qint64 newStartHz = curStartHz + shiftHz;
            const qint64 newStopHz = curStopHz + shiftHz;
            if (newStartHz < 1 || newStopHz > static_cast<qint64>(10000000000LL) || newStopHz <= newStartHz) {
                m_spectrumGridAlignPending = false;
                onDeviceLogMessage(QStringLiteral("Выравнивание сетки: сдвиг выходит за допустимые границы."));
            } else {
                applySpectrumRangeHz(static_cast<quint64>(newStartHz), static_cast<quint64>(newStopHz),
                                     false, false, &m_spectrumGridAlignTargetHz);
                return;
            }
        }
        }
    }

    if (!m_spectrumPlotInitialized) {
        initSpectrumPlot();
    }

    updatePowerTestingPlots(freqs, amps);

    // tabFHSS: отображаем live спектр и maxhold на отдельном графике.
    // Ось X жёстко задаётся applyFhssXAxisForTract по выбранному тракту и НЕ подгоняется
    // под границы пришедших freqs[] — иначе при переходе с других вкладок (особенно tabRecieve)
    // первый «хвостовой» кадр из предыдущего sweep может скукожить ось до своего диапазона
    // (например, до 0.5 МГц), либо вызвать визуальный «миг» c разной плотностью точек.
    if ((isFhssTabActive() || isFhssTestActive()) && ui->plotWidgetFHSSGraph && m_fhssPlotInitialized
        && m_fhssTraces.liveTrace) {
        const int tractForRange = (m_fhssTract > 0) ? m_fhssTract
                                                     : ((m_ppmCurrentOnTract > 0) ? m_ppmCurrentOnTract
                                                                                  : selectedPpmTractFromUi());
        const bool inRange = isFhssSpectrumFrameValid(tractForRange, freqs);

        if (inRange) {
            const bool hold = (m_fhssRunning && m_fhssAutoMaxHold);
            if (hold) {
                accumulateSpectrumMemory(m_fhssMemoryAmps, freqs, amps);
            } else {
                if (!m_fhssKeepMaxHoldUntilNextStart) {
                    if (!m_fhssMemoryAmps.isEmpty()) {
                        m_fhssMemoryAmps.clear();
                    }
                    if (m_fhssTraces.memoryTrace && !m_fhssTraces.memoryTrace->data()->isEmpty()) {
                        m_fhssTraces.memoryTrace->data()->clear();
                    }
                }
            }
            m_fhssLatestFreqs = freqs;
            m_fhssLatestAmps = amps;
            m_fhssDisplayDirty = true;
            if (!m_fhssUiTimer.isActive()) {
                m_fhssUiTimer.start();
            }
        }
    }

    if (!ui->plotWidgetAnalyzer || !m_sweepTraces.liveTrace) {
        return;
    }

    if (isSpectrumMaxHoldOn()) {
        accumulateSpectrumMemory(m_spectrumMemoryAmps, freqs, amps);
    }

    m_spectrumLatestFreqs = freqs;
    m_spectrumLatestAmps = amps;
    m_spectrumDisplayDirty = true;

    if (!m_spectrumUiTimer.isActive()) {
        m_spectrumUiTimer.start();
    }
}

void MainWindow::onSpectrumUiTimer()
{
    if (!m_spectrumStreaming) {
        m_spectrumUiTimer.stop();
        return;
    }
    if (!m_spectrumDisplayDirty || m_spectrumLatestFreqs.isEmpty()) {
        m_spectrumUiTimer.stop();
        return;
    }
    m_spectrumDisplayDirty = false;
    redrawSpectrumDisplay();
}

void MainWindow::onFhssUiTimer()
{
    if (!m_spectrumStreaming) {
        m_fhssUiTimer.stop();
        return;
    }
    if (!m_fhssDisplayDirty || m_fhssLatestFreqs.isEmpty()) {
        m_fhssUiTimer.stop();
        return;
    }
    m_fhssDisplayDirty = false;
    redrawFhssDisplay();
}

void MainWindow::redrawSpectrumDisplay()
{
    if (!ui->plotWidgetAnalyzer || !m_sweepTraces.liveTrace || m_spectrumLatestFreqs.isEmpty()) {
        updateSpectrumPeakReadout();
        return;
    }

    const bool hold = isSpectrumMaxHoldOn();
    const int w = qMax(1, ui->plotWidgetAnalyzer->axisRect()->width());
    const int maxPts = qBound(240, w * 2, 1800);

    updateSweepSpectrumVisual(m_sweepTraces, m_spectrumLatestFreqs, m_spectrumLatestAmps,
                              hold, m_spectrumMemoryAmps, ui->plotWidgetAnalyzer,
                              maxPts);
    // Растягиваем видимую ось X по фактически пришедшим бинам:
    // прибор может квантовать start/stop и отдавать диапазон уже/сдвинутее запрошенного.
    if (m_spectrumLatestFreqs.size() >= 2) {
        const double fx0 = m_spectrumLatestFreqs.first();
        const double fx1 = m_spectrumLatestFreqs.last();
        if (fx1 > fx0) {
            QSignalBlocker bx(ui->plotWidgetAnalyzer->xAxis);
            ui->plotWidgetAnalyzer->xAxis->setRange(fx0, fx1);
        }
    }
    updateSpectrumPeakReadout();
}

void MainWindow::redrawFhssDisplay()
{
    if (!ui->plotWidgetFHSSGraph || !m_fhssPlotInitialized || !m_fhssTraces.liveTrace || m_fhssLatestFreqs.isEmpty()) {
        return;
    }

    const bool hold = (m_fhssRunning && m_fhssAutoMaxHold);
    const bool showHold = hold || m_fhssKeepMaxHoldUntilNextStart;
    const int w = qMax(1, ui->plotWidgetFHSSGraph->axisRect()->width());
    const int maxPts = qBound(240, w * 2, 1800);

    const QVector<double> displayAmps = ampsWithRadiopathOffset(m_fhssLatestAmps);
    const QVector<double> displayMem = (showHold && m_fhssMemoryAmps.size() == m_fhssLatestFreqs.size())
                                           ? ampsWithRadiopathOffset(m_fhssMemoryAmps)
                                           : QVector<double>{};

    updateSweepSpectrumVisual(m_fhssTraces, m_fhssLatestFreqs, displayAmps,
                              showHold, displayMem, ui->plotWidgetFHSSGraph,
                              maxPts);
}

void MainWindow::initSpectrumPlot()
{
    if (!ui->plotWidgetAnalyzer || m_spectrumPlotInitialized) {
        return;
    }

    ui->plotWidgetAnalyzer->clearItems();
    ui->plotWidgetAnalyzer->clearGraphs();
    m_sweepTraces = SweepPlotTraces{};

    quint64 sweepStartHz = static_cast<quint64>(ANALYZER_STREAM_START_HZ_DEFAULT);
    quint64 sweepStopHz = static_cast<quint64>(ANALYZER_STREAM_STOP_HZ_DEFAULT);
    if (!parseAndValidateHandsRangeHz(&sweepStartHz, &sweepStopHz)) {
        sweepStartHz = static_cast<quint64>(ANALYZER_STREAM_START_HZ_DEFAULT);
        sweepStopHz = static_cast<quint64>(ANALYZER_STREAM_STOP_HZ_DEFAULT);
    }
    const double xLoMHz = sweepStartHz / 1e6;
    const double xHiMHz = sweepStopHz / 1e6;
    setupFrequencySweepPlot(ui->plotWidgetAnalyzer, xLoMHz, xHiMHz);
    syncSweepBoundsFromHz(sweepStartHz, sweepStopHz);

    m_sweepTraces = createSweepTraces(ui->plotWidgetAnalyzer);
    m_spectrumMemoryAmps.clear();

    connect(ui->plotWidgetAnalyzer->xAxis,
            static_cast<void (QCPAxis::*)(const QCPRange &, const QCPRange &)>(&QCPAxis::rangeChanged),
            this,
            [this](const QCPRange &, const QCPRange &) {
                clampSpectrumXAxisToSweep();
                scheduleSpectrumRedrawAfterAxisChange();
            });
    connect(ui->plotWidgetAnalyzer->yAxis,
            static_cast<void (QCPAxis::*)(const QCPRange &, const QCPRange &)>(&QCPAxis::rangeChanged),
            this,
            [this](const QCPRange &, const QCPRange &) {
                clampSpectrumYAxisToDbmRange();
                scheduleSpectrumRedrawAfterAxisChange();
            });

    ui->plotWidgetAnalyzer->replot();
    m_spectrumPlotInitialized = true;
}

void MainWindow::startSpectrumStream()
{
    if (m_spectrumStreaming) {
        return;
    }
    if (!m_analyzerConnected) {
        return;
    }

    if (!m_spectrumPlotInitialized) {
        initSpectrumPlot();
    }

    // Если мы на tabFHSS — диапазон анализатора и ось X должны соответствовать выбранному тракту,
    // а НЕ значениям из полей tabHands (там может стоять span 0.5 МГц, что приводит к графику 0.5 МГц
    // с данными в середине ожидаемого FHSS-диапазона).
    const bool onFhssTab = (ui && ui->tabWidget && m_tabFhssIndex >= 0 && ui->tabWidget->currentIndex() == m_tabFhssIndex);
    if (onFhssTab) {
        int trForFhss = (m_fhssTract > 0)
                            ? m_fhssTract
                            : ((m_ppmCurrentOnTract > 0) ? m_ppmCurrentOnTract : selectedPpmTractFromUi());
        if (trForFhss <= 0) {
            trForFhss = ppmFirstTractNumber();
        }
        if (!isFhssCapableTract(trForFhss)) {
            const int sel = selectedPpmTractFromUi();
            if (isFhssCapableTract(sel)) {
                trForFhss = sel;
            }
        }
        if (isFhssCapableTract(trForFhss)) {
            m_analyzerController->setAlternateSpectrumRangesEnabled(false);
            const auto r = fhssSpectrumRangeHzForTract(trForFhss);
            m_analyzerController->setSpectrumRange(r.first, r.second);
            syncSweepBoundsFromHz(r.first, r.second);
            m_fhssLatestFreqs.clear();
            m_fhssLatestAmps.clear();
            m_fhssDisplayDirty = false;

            m_spectrumUiTimer.stop();
            m_spectrumDisplayDirty = false;
            m_spectrumLatestFreqs.clear();
            m_spectrumLatestAmps.clear();

            // plotWidgetAnalyzer (вкладка «Спектр») оставляем синхронным с реальным диапазоном.
            if (ui->plotWidgetAnalyzer) {
                QSignalBlocker bx(ui->plotWidgetAnalyzer->xAxis);
                QSignalBlocker by(ui->plotWidgetAnalyzer->yAxis);
                ui->plotWidgetAnalyzer->xAxis->setRange(r.first / 1e6, r.second / 1e6);
                ui->plotWidgetAnalyzer->yAxis->setRange(-150.0, 20.0);
            }
            m_spectrumMemoryAmps.clear();
            if (m_sweepTraces.liveTrace) {
                m_sweepTraces.liveTrace->data()->clear();
            }
            if (m_sweepTraces.memoryTrace) {
                m_sweepTraces.memoryTrace->data()->clear();
                m_sweepTraces.memoryTrace->setVisible(isSpectrumMaxHoldOn());
            }
            if (ui->plotWidgetAnalyzer) {
                ui->plotWidgetAnalyzer->replot(QCustomPlot::rpQueuedReplot);
            }

            m_analyzerController->startSpectrumStream();
            m_spectrumStreaming = true;
            return;
        }
        // На tabFHSS не подставляем диапазон tabHands/tabPower (0.5–1 МГц).
        return;
    }

    // Если мы на tabPower — не берём диапазон из полей tabHands, а стартуем поток на окне вокруг текущей частоты мощности.
    // Иначе при возврате с tabRecieve можно получить sweep 220–470 МГц и "пустой" moment-график.
    const bool onPowerTab = (ui && ui->tabWidget && m_tabPowerIndex >= 0 && ui->tabWidget->currentIndex() == m_tabPowerIndex);
    if (onPowerTab && m_powerMomentDisplayFreqHz > 0) {
        const quint64 halfSpanHz = kPowerTestAnalyzerSpanHz / 2ULL;
        const quint64 sweepStartHz = (m_powerMomentDisplayFreqHz > halfSpanHz) ? (m_powerMomentDisplayFreqHz - halfSpanHz) : 1ULL;
        const quint64 sweepStopHz = m_powerMomentDisplayFreqHz + halfSpanHz;
        m_analyzerController->setSpectrumRange(sweepStartHz, sweepStopHz);
        syncSweepBoundsFromHz(sweepStartHz, sweepStopHz);
        // Для таба "Спектр" (plotWidgetAnalyzer) тоже обновляем ось X, чтобы не было рассинхронизации.
        if (ui->plotWidgetAnalyzer) {
            QSignalBlocker bx(ui->plotWidgetAnalyzer->xAxis);
            QSignalBlocker by(ui->plotWidgetAnalyzer->yAxis);
            ui->plotWidgetAnalyzer->xAxis->setRange(sweepStartHz / 1e6, sweepStopHz / 1e6);
            ui->plotWidgetAnalyzer->yAxis->setRange(-150.0, 20.0);
        }

        m_spectrumUiTimer.stop();
        m_spectrumDisplayDirty = false;
        m_spectrumLatestFreqs.clear();
        m_spectrumLatestAmps.clear();

        m_spectrumMemoryAmps.clear();
        if (m_sweepTraces.liveTrace) {
            m_sweepTraces.liveTrace->data()->clear();
        }
        if (m_sweepTraces.memoryTrace) {
            m_sweepTraces.memoryTrace->data()->clear();
            m_sweepTraces.memoryTrace->setVisible(isSpectrumMaxHoldOn());
        }
        if (ui->plotWidgetAnalyzer) {
            ui->plotWidgetAnalyzer->replot(QCustomPlot::rpQueuedReplot);
        }

        m_analyzerController->startSpectrumStream();
        m_spectrumStreaming = true;
        return;
    }

    quint64 sweepStartHz = 0;
    quint64 sweepStopHz = 0;
    if (spectrumRangeFromCenterSpanUi(&sweepStartHz, &sweepStopHz)) {
    } else {
        if (!parseAndValidateHandsRangeHz(&sweepStartHz, &sweepStopHz)) {
            onDeviceLogMessage(QStringLiteral(
                "Диапазон в полях не распознан; подставлены значения по умолчанию (220–470 МГц)."));
            sweepStartHz = static_cast<quint64>(ANALYZER_STREAM_START_HZ_DEFAULT);
            sweepStopHz = static_cast<quint64>(ANALYZER_STREAM_STOP_HZ_DEFAULT);
            syncHandsFreqLineEdits(sweepStartHz, sweepStopHz);
        }
        syncSpectrumCenterSpanFromRangeHz(sweepStartHz, sweepStopHz, false);
    }
    m_analyzerController->setSpectrumRange(sweepStartHz, sweepStopHz);
    syncSweepBoundsFromHz(sweepStartHz, sweepStopHz);
    if (ui->plotWidgetAnalyzer) {
        QSignalBlocker bx(ui->plotWidgetAnalyzer->xAxis);
        QSignalBlocker by(ui->plotWidgetAnalyzer->yAxis);
        ui->plotWidgetAnalyzer->xAxis->setRange(sweepStartHz / 1e6, sweepStopHz / 1e6);
        ui->plotWidgetAnalyzer->yAxis->setRange(-150.0, 20.0);
    }

    m_spectrumUiTimer.stop();
    m_spectrumDisplayDirty = false;
    m_spectrumLatestFreqs.clear();
    m_spectrumLatestAmps.clear();

    m_spectrumMemoryAmps.clear();
    if (m_sweepTraces.liveTrace) {
        m_sweepTraces.liveTrace->data()->clear();
    }
    if (m_sweepTraces.memoryTrace) {
        m_sweepTraces.memoryTrace->data()->clear();
        m_sweepTraces.memoryTrace->setVisible(isSpectrumMaxHoldOn());
    }

    if (ui->plotWidgetAnalyzer) {
        ui->plotWidgetAnalyzer->replot(QCustomPlot::rpQueuedReplot);
    }

    m_analyzerController->startSpectrumStream();
    m_spectrumStreaming = true;
}

bool MainWindow::parseHandsRangeHz(double *startHz, double *stopHz) const
{
    if (!ui->lineEditFreqStart || !ui->lineEditFreqStop || !startHz || !stopHz) {
        return false;
    }

    auto parseTriplet = [](const QString &text, double *out) -> bool {
        const QStringList p = text.trimmed().split(QLatin1Char('.'), Qt::SkipEmptyParts);
        if (p.size() != 3 && p.size() != 4) {
            return false;
        }
        bool ok = false;
        if (p.size() == 3) {
            const double a = p[0].toDouble(&ok);
            if (!ok) {
                return false;
            }
            const double b = p[1].toDouble(&ok);
            if (!ok) {
                return false;
            }
            const double c = p[2].toDouble(&ok);
            if (!ok) {
                return false;
            }
            *out = a * 1e6 + b * 1e3 + c;
            return true;
        }
        const double a = p[0].toDouble(&ok);
        if (!ok) {
            return false;
        }
        const double b = p[1].toDouble(&ok);
        if (!ok) {
            return false;
        }
        const double c = p[2].toDouble(&ok);
        if (!ok) {
            return false;
        }
        const double d = p[3].toDouble(&ok);
        if (!ok) {
            return false;
        }
        *out = a * 1e9 + b * 1e6 + c * 1e3 + d;
        return true;
    };

    double s = 0.0;
    double t = 0.0;
    if (!parseTriplet(ui->lineEditFreqStart->text(), &s)) {
        return false;
    }
    if (!parseTriplet(ui->lineEditFreqStop->text(), &t)) {
        return false;
    }
    *startHz = s;
    *stopHz = t;
    return true;
}

bool MainWindow::parseTripletLineToHz(const QString &text, quint64 *outHz) const
{
    if (!outHz) {
        return false;
    }
    const QStringList p = text.trimmed().split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (p.size() != 3 && p.size() != 4) {
        return false;
    }
    bool ok = false;
    double hz = 0.0;
    if (p.size() == 3) {
        const double a = p[0].toDouble(&ok);
        if (!ok) {
            return false;
        }
        const double b = p[1].toDouble(&ok);
        if (!ok) {
            return false;
        }
        const double c = p[2].toDouble(&ok);
        if (!ok) {
            return false;
        }
        hz = a * 1e6 + b * 1e3 + c;
    } else {
        const double a = p[0].toDouble(&ok);
        if (!ok) {
            return false;
        }
        const double b = p[1].toDouble(&ok);
        if (!ok) {
            return false;
        }
        const double c = p[2].toDouble(&ok);
        if (!ok) {
            return false;
        }
        const double d = p[3].toDouble(&ok);
        if (!ok) {
            return false;
        }
        hz = a * 1e9 + b * 1e6 + c * 1e3 + d;
    }
    if (!std::isfinite(hz) || hz <= 0.0 || hz > static_cast<double>(kHandsMaxFreqHz)) {
        return false;
    }
    *outHz = static_cast<quint64>(hz + 0.5);
    return true;
}

bool MainWindow::parseAndValidateHandsRangeHz(quint64 *startHz, quint64 *stopHz) const
{
    if (!startHz || !stopHz) {
        return false;
    }
    double s = 0.0;
    double t = 0.0;
    if (!parseHandsRangeHz(&s, &t)) {
        return false;
    }
    quint64 su = static_cast<quint64>(s + 0.5);
    quint64 tu = static_cast<quint64>(t + 0.5);
    if (su == 0 || tu == 0) {
        return false;
    }
    if (su > tu) {
        std::swap(su, tu);
    }
    if (su >= tu) {
        return false;
    }
    if (tu > kHandsMaxFreqHz) {
        return false;
    }
    *startHz = su;
    *stopHz = tu;
    return true;
}

void MainWindow::syncHandsFreqLineEdits(quint64 startHz, quint64 stopHz)
{
    if (ui->lineEditFreqStart) {
        ui->lineEditFreqStart->setText(formatHzTriplet4(startHz));
    }
    if (ui->lineEditFreqStop) {
        ui->lineEditFreqStop->setText(formatHzTriplet4(stopHz));
    }
}

void MainWindow::initSpectrumSpanCombo()
{
    if (!ui->comboBoxSpectrumSpanMHz) {
        return;
    }

    polishComboDropDownSurface(ui->comboBoxSpectrumSpanMHz);
    setupComboOpenOnWholeAreaClick(ui->comboBoxSpectrumSpanMHz, this);

    // Важно: после setEditable(true) Qt создаёт внутренний QLineEdit со своим шрифтом.
    // Принудительно синхронизируем шрифт с lineEditSpectrumCenterMHz.
    if (ui->lineEditSpectrumCenterMHz) {
        const QFont f = ui->lineEditSpectrumCenterMHz->font();
        ui->comboBoxSpectrumSpanMHz->setFont(f);
        if (ui->comboBoxSpectrumSpanMHz->view()) {
            ui->comboBoxSpectrumSpanMHz->view()->setFont(f);
        }
    }
    if (QLineEdit *line = ui->comboBoxSpectrumSpanMHz->lineEdit()) {
        if (ui->lineEditSpectrumCenterMHz) {
            line->setFont(ui->lineEditSpectrumCenterMHz->font());
        }
        line->setAlignment(Qt::AlignCenter);
        line->setCursor(Qt::ArrowCursor);
    }

    ui->comboBoxSpectrumSpanMHz->clear();
    const QVector<double> spansMHz = {0.1, 0.5, 1.0, 3.0, 5.0, 10.0, 15.0, 30.0, 50.0, 100.0};
    for (double spanMHz : spansMHz) {
        ui->comboBoxSpectrumSpanMHz->addItem(QString::number(spanMHz, 'g', 6), spanMHz);
        const int itemIdx = ui->comboBoxSpectrumSpanMHz->count() - 1;
        ui->comboBoxSpectrumSpanMHz->setItemData(itemIdx, Qt::AlignCenter, Qt::TextAlignmentRole);
    }
    const int idx0_5MHz = ui->comboBoxSpectrumSpanMHz->findData(0.5);
    if (idx0_5MHz >= 0) {
        ui->comboBoxSpectrumSpanMHz->setCurrentIndex(idx0_5MHz);
    }
}

void MainWindow::syncSpectrumCenterSpanFromRangeHz(quint64 startHz, quint64 stopHz, bool updateSpanCombo,
                                                   bool updateCenterLine)
{
    if (!ui->lineEditSpectrumCenterMHz) {
        return;
    }
    if (updateCenterLine) {
        const quint64 centerHz = (startHz / 2) + (stopHz / 2) + ((startHz % 2 + stopHz % 2) / 2);
        ui->lineEditSpectrumCenterMHz->setText(formatHzTriplet4(centerHz));
    }

    if (!updateSpanCombo || !ui->comboBoxSpectrumSpanMHz) {
        return;
    }
    const double widthMHz = static_cast<double>(stopHz - startHz) / 1e6;
    int bestIdx = -1;
    double bestDiff = std::numeric_limits<double>::max();
    for (int i = 0; i < ui->comboBoxSpectrumSpanMHz->count(); ++i) {
        bool ok = false;
        const double v = ui->comboBoxSpectrumSpanMHz->itemData(i).toDouble(&ok);
        if (!ok || !std::isfinite(v)) {
            continue;
        }
        const double d = std::abs(v - widthMHz);
        if (d < bestDiff) {
            bestDiff = d;
            bestIdx = i;
        }
    }
    if (bestIdx >= 0) {
        ui->comboBoxSpectrumSpanMHz->setCurrentIndex(bestIdx);
    }
}

bool MainWindow::spectrumRangeFromCenterSpanUi(quint64 *outStartHz, quint64 *outStopHz) const
{
    if (!outStartHz || !outStopHz) {
        return false;
    }
    if (!ui->lineEditSpectrumCenterMHz || !ui->comboBoxSpectrumSpanMHz) {
        return false;
    }
    quint64 centerHz = 0;
    if (!parseTripletLineToHz(ui->lineEditSpectrumCenterMHz->text(), &centerHz)) {
        return false;
    }
    bool spanOk = false;
    const double spanMHz = ui->comboBoxSpectrumSpanMHz->currentData().toDouble(&spanOk);
    if (!spanOk || !std::isfinite(spanMHz) || spanMHz < 0.1) {
        return false;
    }
    const double centerMHz = static_cast<double>(centerHz) * 1e-6;
    QString err;
    return spectrumBandFromCenterSpanMHz(centerMHz, spanMHz, outStartHz, outStopHz, &err);
}

bool MainWindow::spectrumBandFromCenterSpanMHz(double centerMHz,
                                               double spanMHz,
                                               quint64 *outStartHz,
                                               quint64 *outStopHz,
                                               QString *errorText) const
{
    if (!outStartHz || !outStopHz) {
        return false;
    }
    if (!std::isfinite(centerMHz) || !std::isfinite(spanMHz) || spanMHz < 0.1 || spanMHz > 100.0) {
        if (errorText) {
            *errorText = QStringLiteral("Некорректные центр или span (0.1…100 МГц).");
        }
        return false;
    }
    const quint64 centerHz = static_cast<quint64>(std::llround(centerMHz * 1e6));
    const quint64 halfHz =
        static_cast<quint64>(std::llround(0.5 * spanMHz * 1e6));
    if (centerHz < halfHz) {
        if (errorText) {
            *errorText = QStringLiteral("Для выбранного span центр слишком мал (нижняя граница < 0).");
        }
        return false;
    }
    const quint64 s = centerHz - halfHz;
    const quint64 e = centerHz + halfHz;
    if (e <= s) {
        if (errorText) {
            *errorText = QStringLiteral("Не удалось вычислить диапазон.");
        }
        return false;
    }
    if (e > static_cast<quint64>(10000000000ULL)) {
        if (errorText) {
            *errorText = QStringLiteral("Верхняя граница частоты превышает допустимую.");
        }
        return false;
    }
    *outStartHz = s;
    *outStopHz = e;
    return true;
}

void MainWindow::armSpectrumGridAlignToTargetHz(quint64 targetHz)
{
    if (targetHz == 0) {
        return;
    }
    m_spectrumGridAlignTargetHz = targetHz;
    m_spectrumGridAlignPending = true;
    m_spectrumGridAlignAttemptsLeft = kSpectrumGridAlignMaxAttempts;
}

void MainWindow::applySpectrumRangeHz(quint64 startHz, quint64 stopHz, bool updateSpanCombo,
                                      bool triggerBwDebugFrame, const quint64 *lockCenterDisplayHz)
{
    Q_UNUSED(triggerBwDebugFrame);
    if (stopHz <= startHz) {
        onDeviceLogMessage(QStringLiteral("Диапазон sweep отклонён: stop должен быть больше start."));
        return;
    }
    m_analyzerController->setSpectrumRange(startHz, stopHz);
    syncHandsFreqLineEdits(startHz, stopHz);
    syncSweepBoundsFromHz(startHz, stopHz);
    if (lockCenterDisplayHz) {
        syncSpectrumCenterSpanFromRangeHz(startHz, stopHz, updateSpanCombo, false);
        ui->lineEditSpectrumCenterMHz->setText(formatHzTriplet4(*lockCenterDisplayHz));
    } else {
        syncSpectrumCenterSpanFromRangeHz(startHz, stopHz, updateSpanCombo, true);
    }
    if (ui->plotWidgetAnalyzer) {
        QSignalBlocker bx(ui->plotWidgetAnalyzer->xAxis);
        QSignalBlocker by(ui->plotWidgetAnalyzer->yAxis);
        ui->plotWidgetAnalyzer->xAxis->setRange(startHz / 1e6, stopHz / 1e6);
        ui->plotWidgetAnalyzer->yAxis->setRange(-150.0, 20.0);
    }
    m_spectrumMemoryAmps.clear();
    if (m_sweepTraces.liveTrace) {
        m_sweepTraces.liveTrace->data()->clear();
    }
    if (m_sweepTraces.memoryTrace) {
        m_sweepTraces.memoryTrace->data()->clear();
        m_sweepTraces.memoryTrace->setVisible(isSpectrumMaxHoldOn());
    }
    m_spectrumDisplayDirty = true;
    if (m_spectrumStreaming && !m_spectrumUiTimer.isActive()) {
        m_spectrumUiTimer.start();
    }
    redrawSpectrumDisplay();
}

void MainWindow::onHandsSpectrumApplyClicked()
{
    quint64 s = 0;
    quint64 e = 0;
    if (!parseAndValidateHandsRangeHz(&s, &e)) {
        onDeviceLogMessage(QStringLiteral(
            "Диапазон: формат NNN.NNN.NNN или N.NNN.NNN.NNN Гц, начало < конец, разумные значения частоты."));
        return;
    }
    // Ручной диапазон должен применяться точно как введён, без автоподстройки в сетку прибора.
    m_spectrumGridAlignPending = false;
    m_spectrumGridAlignAttemptsLeft = 0;
    applySpectrumRangeHz(s, e);
    updateSpectrumPeakReadout();
    onDeviceLogMessage(QStringLiteral("Диапазон анализатора: %1 – %2 Гц").arg(s).arg(e));
}

void MainWindow::onSpectrumCenterSpanApplyClicked()
{
    // ============================================================================
    // АЛГОРИТМ ПОДСТРОЙКИ ДИАПАЗОНА ПОД lineEditSpectrumCenterMHz
    // ============================================================================
    //
    // 1. ИНИЦИАЛИЗАЦИЯ (при нажатии Apply "Центр/SPAN"):
    //    - ftarget    : целевая частота (Гц) из lineEditSpectrumCenterMHz
    //    - SPAN_MHz   : выбранный диапазон (МГц)
    //    - SPAN_Hz    = SPAN_MHz * 1e6
    //    - start      = ftarget - SPAN_Hz / 2
    //    - stop       = ftarget + SPAN_Hz / 2
    //    - Вызывается setSpectrumRange(start, stop)
    //    - В UI центр фиксируется как введённый (lockCenterDisplayHz)
    //
    // 2. АВТОПОДСТРОЙКА "В СЕТКУ БИНОВ" (после получения кадров, до 3 попыток):
    //    - После получения валидных кадров ищется ближайший бин к ftarget
    //    - Считается ошибка: err = f_nearest - ftarget (Гц)
    //    - Оценивается шаг сетки step как среднее Δf по первым ~32 интервалам
    //    - Если |err| > tol, где tol = max(200, 0.04 * step), то:
    //        * start/stop сдвигаются на величину, кратную step
    //        * UI центр НЕ меняется (остаётся ftarget)
    //        * Запрос повторяется
    //
    // 3. РАСЧЁТ СДВИГА (подробно на цифрах):
    //    a) Поиск ближайшего бина:
    //         targetMHz  = ftarget * 1e-6
    //         nearestIdx = argmin(|freqs[i] - targetMHz|)
    //
    //    b) Ошибка (Гц):
    //         errHz = (freqs[nearestIdx] - targetMHz) * 1e6
    //         // err > 0 → бин выше цели, err < 0 → бин ниже цели
    //
    //    c) Шаг сетки (Гц):
    //         ns       = min(32, freqs.size() - 1)
    //         stepHz   = average(|freqs[i+1] - freqs[i]| * 1e6) for i = 0..ns-1
    //
    //    d) Допуск (Гц):
    //         tolHz = max(200.0, 0.04 * stepHz)
    //         // Если |errHz| <= tolHz → выравнивание завершено
    //
    //    e) Сдвиг диапазона (Гц):
    //         stepHzI = round(stepHz)
    //         shiftHz = -round(errHz / stepHzI) * stepHzI
    //         // Fallback, если shiftHz == 0: shiftHz = -round(errHz)
    //         newStart = curStart + shiftHz
    //         newStop  = curStop  + shiftHz
    //
    // 4. ПРИМЕР РАСЧЁТА:
    //    ftarget    = 433 920 000 Гц (433.920 МГц)
    //    f_nearest  = 433.922 МГц
    //    errHz      = (433.922 - 433.920) * 1e6 = +2 000 Гц
    //    stepHz     = 3 000 Гц (условно)
    //    tolHz      = max(200, 0.04*3000) = 200 Гц → |err| > tol, нужен сдвиг
    //    err/step   = 2000 / 3000 ≈ 0.666 → round() = 1
    //    shiftHz    = -1 * 3000 = -3 000 Гц
    //    newStart   = curStart  - 3000
    //    newStop    = curStop   - 3000
    //    → Сетка бинов сдвигается, ближайший бин становится ближе к цели.
    //
    // СМ. ФУНКЦИИ:
    //    - MainWindow::onSpectrumCenterSpanApplyClicked()
    //    - spectrumBandFromCenterSpanMHz()
    //    - applySpectrumRangeHz()
    // ============================================================================

    if (!ui->lineEditSpectrumCenterMHz || !ui->comboBoxSpectrumSpanMHz) {
        return;
    }
    quint64 centerHz = 0;
    if (!parseTripletLineToHz(ui->lineEditSpectrumCenterMHz->text(), &centerHz)) {
        onDeviceLogMessage(QStringLiteral(
            "Центр: формат NNN.NNN.NNN Гц (как в полях начала/конца диапазона)."));
        return;
    }
    const double centerMHz = static_cast<double>(centerHz) * 1e-6;
    bool spanOk = false;
    const double spanMHz = ui->comboBoxSpectrumSpanMHz->currentData().toDouble(&spanOk);
    if (!spanOk || !std::isfinite(spanMHz) || spanMHz < 0.1) {
        onDeviceLogMessage(QStringLiteral("Выберите корректный span (МГц)."));
        return;
    }
    QString err;
    quint64 s = 0;
    quint64 e = 0;
    if (!spectrumBandFromCenterSpanMHz(centerMHz, spanMHz, &s, &e, &err)) {
        onDeviceLogMessage(err.isEmpty() ? QStringLiteral("Не удалось вычислить диапазон.") : err);
        return;
    }
    applySpectrumRangeHz(s, e, true, true, &centerHz);
    armSpectrumGridAlignToTargetHz(centerHz);
    onDeviceLogMessage(QStringLiteral("Диапазон: центр %1 Гц, span %2 МГц → %3 – %4 Гц")
                             .arg(centerHz)
                             .arg(spanMHz, 0, 'g', 6)
                             .arg(s)
                             .arg(e));
}

bool MainWindow::isSpectrumMaxHoldOn() const
{
    return ui->pushButtonSpectrumMaxHold && ui->pushButtonSpectrumMaxHold->isChecked();
}

void MainWindow::onSpectrumMaxHoldToggled(bool checked)
{
    if (checked) {
        if (!m_spectrumLatestFreqs.isEmpty()
            && m_spectrumLatestAmps.size() == m_spectrumLatestFreqs.size()) {
            accumulateSpectrumMemory(m_spectrumMemoryAmps, m_spectrumLatestFreqs, m_spectrumLatestAmps);
        }
        m_spectrumDisplayDirty = true;
        if (m_spectrumStreaming && !m_spectrumUiTimer.isActive()) {
            m_spectrumUiTimer.start();
        }
    } else {
        m_spectrumMemoryAmps.clear();
        if (m_sweepTraces.memoryTrace) {
            m_sweepTraces.memoryTrace->data()->clear();
            m_sweepTraces.memoryTrace->setVisible(false);
        }
        m_spectrumDisplayDirty = true;
        if (m_spectrumStreaming && !m_spectrumUiTimer.isActive()) {
            m_spectrumUiTimer.start();
        }
    }
    redrawSpectrumDisplay();
    if (ui->plotWidgetAnalyzer && !m_spectrumLatestFreqs.isEmpty()) {
        ui->plotWidgetAnalyzer->replot();
    }
}

void MainWindow::updateSpectrumBwUi(int sliderIndex)
{
    if (ui->labelSpectrumBwValue) {
        ui->labelSpectrumBwValue->setText(spectrumBwLabelText(sliderIndex));
    }
}

void MainWindow::onSpectrumBwSliderChanged(int value)
{
    updateSpectrumBwUi(value);
    if (m_analyzerController) {
        m_analyzerController->setSpectrumBandwidth(value);
    }
}

void MainWindow::onSpectrumSavePlotClicked()
{
    if (!ui->plotWidgetAnalyzer) {
        onDeviceLogMessage(QStringLiteral("График недоступен для сохранения."));
        return;
    }

    const QString defaultName = QStringLiteral("spectrum_%1.png")
                                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    QString selectedFilter = QStringLiteral("PNG (*.png)");
    QString filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Сохранить спектр"),
        defaultName,
        QStringLiteral("PNG (*.png);;JPEG (*.jpg *.jpeg);;BMP (*.bmp);;PDF (*.pdf)"),
        &selectedFilter);

    if (filePath.isEmpty()) {
        return;
    }

    QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext.isEmpty()) {
        if (selectedFilter.startsWith(QStringLiteral("JPEG"))) {
            ext = QStringLiteral("jpg");
        } else if (selectedFilter.startsWith(QStringLiteral("BMP"))) {
            ext = QStringLiteral("bmp");
        } else if (selectedFilter.startsWith(QStringLiteral("PDF"))) {
            ext = QStringLiteral("pdf");
        } else {
            ext = QStringLiteral("png");
        }
        filePath += QStringLiteral(".") + ext;
    }

    bool ok = false;
    if (ext == QStringLiteral("png")) {
        ok = ui->plotWidgetAnalyzer->savePng(filePath, 0, 0, 1.0, -1);
    } else if (ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg")) {
        ok = ui->plotWidgetAnalyzer->saveJpg(filePath, 0, 0, 1.0, 95);
    } else if (ext == QStringLiteral("bmp")) {
        ok = ui->plotWidgetAnalyzer->saveBmp(filePath, 0, 0, 1.0);
    } else if (ext == QStringLiteral("pdf")) {
        ok = ui->plotWidgetAnalyzer->savePdf(filePath);
    } else {
        onDeviceLogMessage(QStringLiteral("Неподдерживаемый формат файла: %1").arg(ext));
        return;
    }

    if (ok) {
        onDeviceLogMessage(QStringLiteral("График сохранён: %1").arg(filePath));
    } else {
        onDeviceLogMessage(QStringLiteral("Не удалось сохранить график: %1").arg(filePath));
    }
}

void MainWindow::onToggleLogVisibilityClicked()
{
    if (!ui->logTextEdit) {
        return;
    }

    m_logCollapsed = !m_logCollapsed;
    ui->logTextEdit->setVisible(!m_logCollapsed);
    updateLogToggleButtonText();

    if (ui->plotWidgetAnalyzer) {
        ui->plotWidgetAnalyzer->replot(QCustomPlot::rpQueuedReplot);
    }

    onDeviceLogMessage(m_logCollapsed
                           ? QStringLiteral("Лог свернут.")
                           : QStringLiteral("Лог развернут."));
}

void MainWindow::updateLogToggleButtonText()
{
    if (!ui->pushButtonToggleLog) {
        return;
    }
    ui->pushButtonToggleLog->setText(QString());
    // Лог развёрнут: стрелка вниз (свернуть); свёрнут — стрелка вверх (развернуть).
    const char *iconPath = m_logCollapsed ? ":/caret-up.svg" : ":/caret-down.svg";
    ui->pushButtonToggleLog->setIcon(QIcon(QString::fromUtf8(iconPath)));
}

void MainWindow::updateSpectrumPeakReadout()
{
    const bool hasData = !m_spectrumLatestFreqs.isEmpty()
                         && m_spectrumLatestAmps.size() == m_spectrumLatestFreqs.size();

    if (!hasData) {
        if (ui->labelPeakFreqValue) {
            ui->labelPeakFreqValue->display(QStringLiteral("----"));
        }
        if (ui->labelPeakPowerValue) {
            ui->labelPeakPowerValue->display(QStringLiteral("----"));
        }
        if (ui->labelSpectrumPeakFreqValue) {
            ui->labelSpectrumPeakFreqValue->display(QStringLiteral("----"));
        }
        if (ui->labelSpectrumPeakPowerValue) {
            ui->labelSpectrumPeakPowerValue->display(QStringLiteral("----"));
        }
        return;
    }

    int bestCent = 0;
    quint64 targetHz = 0;
    const bool targetOk =
        ui->lineEditSpectrumCenterMHz
        && parseTripletLineToHz(ui->lineEditSpectrumCenterMHz->text(), &targetHz);
    if (targetOk) {
        const double targetMHz = static_cast<double>(targetHz) * 1e-6;
        double bestDiff = std::abs(m_spectrumLatestFreqs[0] - targetMHz);
        for (int i = 1; i < m_spectrumLatestFreqs.size(); ++i) {
            const double d = std::abs(m_spectrumLatestFreqs[i] - targetMHz);
            if (d < bestDiff) {
                bestDiff = d;
                bestCent = i;
            }
        }
    } else {
        double bestAmp = m_spectrumLatestAmps[0];
        for (int i = 1; i < m_spectrumLatestAmps.size(); ++i) {
            if (m_spectrumLatestAmps[i] > bestAmp) {
                bestAmp = m_spectrumLatestAmps[i];
                bestCent = i;
            }
        }
    }

    int bestSpec = 0;
    double maxSpecAmp = m_spectrumLatestAmps[0];
    for (int i = 1; i < m_spectrumLatestAmps.size(); ++i) {
        if (m_spectrumLatestAmps[i] > maxSpecAmp) {
            maxSpecAmp = m_spectrumLatestAmps[i];
            bestSpec = i;
        }
    }

    if (ui->labelPeakFreqValue) {
        const quint64 centHz =
            static_cast<quint64>(std::llround(m_spectrumLatestFreqs[bestCent] * 1e6));
        ui->labelPeakFreqValue->display(formatGroupedWithDots(centHz));
    }
    if (ui->labelPeakPowerValue) {
        ui->labelPeakPowerValue->display(QString::number(m_spectrumLatestAmps[bestCent], 'f', 2));
    }
    if (ui->labelSpectrumPeakFreqValue) {
        const quint64 specHz =
            static_cast<quint64>(std::llround(m_spectrumLatestFreqs[bestSpec] * 1e6));
        ui->labelSpectrumPeakFreqValue->display(formatGroupedWithDots(specHz));
    }
    if (ui->labelSpectrumPeakPowerValue) {
        ui->labelSpectrumPeakPowerValue->display(QString::number(maxSpecAmp, 'f', 2));
    }
}

void MainWindow::syncSweepBoundsFromHz(quint64 startHz, quint64 stopHz)
{
    m_spectrumSweepStartHz = startHz;
    m_spectrumSweepStopHz = stopHz;
    if (m_spectrumSweepStopHz <= m_spectrumSweepStartHz) {
        m_spectrumSweepStopHz = m_spectrumSweepStartHz + 1;
    }
    m_spectrumSweepMinMHz = static_cast<double>(m_spectrumSweepStartHz) / 1e6;
    m_spectrumSweepMaxMHz = static_cast<double>(m_spectrumSweepStopHz) / 1e6;
    if (m_spectrumSweepMaxMHz <= m_spectrumSweepMinMHz) {
        m_spectrumSweepMaxMHz = m_spectrumSweepMinMHz + 1e-3;
    }
}

void MainWindow::clampSpectrumXAxisToSweep()
{
    if (!ui->plotWidgetAnalyzer) {
        return;
    }
    QCPAxis *ax = ui->plotWidgetAnalyzer->xAxis;
    const QCPRange r = ax->range();
    const double xmin = m_spectrumSweepMinMHz;
    const double xmax = m_spectrumSweepMaxMHz;
    double lo = r.lower;
    double hi = r.upper;
    bool changed = false;
    if (lo < xmin) {
        lo = xmin;
        changed = true;
    }
    if (hi > xmax) {
        hi = xmax;
        changed = true;
    }
    if (hi <= lo) {
        const double span = qMax(1e-6, xmax - xmin);
        hi = qMin(xmax, lo + 0.01 * span);
        if (hi <= lo) {
            lo = xmin;
            hi = xmax;
        }
        changed = true;
    }
    if (changed) {
        QSignalBlocker b(ax);
        ax->setRange(lo, hi);
    }
}

void MainWindow::clampSpectrumYAxisToDbmRange()
{
    static constexpr double kLo = -150.0;
    static constexpr double kHi = 20.0;
    if (!ui->plotWidgetAnalyzer) {
        return;
    }
    QCPAxis *ax = ui->plotWidgetAnalyzer->yAxis;
    const QCPRange r = ax->range();
    double lo = r.lower;
    double hi = r.upper;
    bool changed = false;
    if (lo < kLo) {
        lo = kLo;
        changed = true;
    }
    if (hi > kHi) {
        hi = kHi;
        changed = true;
    }
    if (hi <= lo) {
        hi = qMin(kHi, lo + 1.0);
        if (hi <= lo) {
            lo = kLo;
            hi = kHi;
        }
        changed = true;
    }
    if (changed) {
        QSignalBlocker b(ax);
        ax->setRange(lo, hi);
    }
}

void MainWindow::scheduleSpectrumRedrawAfterAxisChange()
{
    if (m_spectrumStreaming && !m_spectrumLatestFreqs.isEmpty()) {
        m_spectrumDisplayDirty = true;
        if (!m_spectrumUiTimer.isActive()) {
            m_spectrumUiTimer.start();
        }
    }
}

void MainWindow::scheduleFhssRedrawAfterAxisChange()
{
    if (m_spectrumStreaming && !m_fhssLatestFreqs.isEmpty()) {
        m_fhssDisplayDirty = true;
        if (!m_fhssUiTimer.isActive()) {
            m_fhssUiTimer.start();
        }
    }
}

void MainWindow::stopSpectrumStream()
{
    if (!m_spectrumStreaming) {
        return;
    }

    m_spectrumGridAlignPending = false;
    m_spectrumGridAlignAttemptsLeft = 0;

    m_spectrumUiTimer.stop();
    m_spectrumDisplayDirty = false;
    m_fhssUiTimer.stop();
    m_fhssDisplayDirty = false;
    m_fhssLatestFreqs.clear();
    m_fhssLatestAmps.clear();
    m_analyzerController->stopSpectrumStream();
    m_spectrumStreaming = false;
}

// ============================================================================
// tabFHSS (ППРЧ)
// ============================================================================

void MainWindow::initFhssTestingUi()
{
    if (!ui || m_fhssControlsInitialized) {
        return;
    }
    m_fhssControlsInitialized = true;

    polishComboDropDownSurface(ui->modeFHSSComboBox);
    if (ui->modeFHSSComboBox) {
        setupComboOpenOnWholeAreaClick(ui->modeFHSSComboBox, this);
    }

    if (ui->emissionAntennaWidgetFHSS) {
        ui->emissionAntennaWidgetFHSS->setVisible(false);
    }

    if (ui->pushButtonStartTestingFHSS) {
        ui->pushButtonStartTestingFHSS->setCheckable(false);
        ui->pushButtonStartTestingFHSS->setAutoDefault(false);
        ui->pushButtonStartTestingFHSS->setDefault(false);
        connect(ui->pushButtonStartTestingFHSS, &QPushButton::clicked,
                this, &MainWindow::onStartTestingFhssClicked);
    }

    if (ui->pushButtonFHSSTestStop) {
        ui->pushButtonFHSSTestStop->setIcon(receiveBlackIconStop());
        ui->pushButtonFHSSTestStop->setAutoDefault(false);
        ui->pushButtonFHSSTestStop->setDefault(false);
        connect(ui->pushButtonFHSSTestStop, &QPushButton::clicked,
                this, &MainWindow::onFhssStopClicked);
    }

    if (ui->modeFHSSComboBox) {
        // При смене режима в комбобоксе:
        //  • перерисовать ось/тики графика и LCD под (тракт, режим);
        //  • обновить запрос анализатора, если активна вкладка ППРЧ;
        //  • переоценить доступность кнопок и подпись «НАЧАТЬ ТЕСТ <режим>».
        connect(ui->modeFHSSComboBox,
                static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                this,
                [this](int) {
                    applyFhssBandForSelectedMode();
                    updateFhssTestButtonsAccessForSelectedTract();
                });
    }

    if (ui->lcdFHSSRangeValueDash) {
        ui->lcdFHSSRangeValueDash->setDigitCount(1);
        ui->lcdFHSSRangeValueDash->display(QStringLiteral("-"));
    }

    initFhssPlot();
    setFhssTestControlsIdle();

    // MaxHold линия в FHSS-графике обновляется в onSpectrumDataReceived/onSpectrumMaxHoldToggled.
}

void MainWindow::initFhssPlot()
{
    if (!ui || !ui->plotWidgetFHSSGraph || m_fhssPlotInitialized) {
        return;
    }

    ui->plotWidgetFHSSGraph->clearItems();
    ui->plotWidgetFHSSGraph->clearGraphs();

    // По стилю/оформлению делаем как sweep-графики (аналогично powerGraph).
    setupFrequencySweepPlot(ui->plotWidgetFHSSGraph, 20.0, 190.0);

    // Требование: диапазон запроса анализатора для ППРЧ фиксированный (по тракту),
    // поэтому расширять видимый диапазон по оси X (zoom out / «увеличить масштаб») нельзя.
    // Уменьшать масштаб (zoom in) можно.
    connect(ui->plotWidgetFHSSGraph->xAxis,
            static_cast<void (QCPAxis::*)(const QCPRange &, const QCPRange &)>(&QCPAxis::rangeChanged),
            this,
            [this](const QCPRange &, const QCPRange &) {
                if (!ui || !ui->plotWidgetFHSSGraph) {
                    return;
                }
                QCPAxis *ax = ui->plotWidgetFHSSGraph->xAxis;
                if (!ax) {
                    return;
                }

                const int tractForRange = (m_fhssTract > 0) ? m_fhssTract
                                                            : ((m_ppmCurrentOnTract > 0) ? m_ppmCurrentOnTract
                                                                                        : selectedPpmTractFromUi());
                if (!isFhssCapableTract(tractForRange)) {
                    return;
                }
                const auto hz = fhssPlotXAxisRangeHzForTract(tractForRange);
                const double xmin = static_cast<double>(hz.first) * 1e-6;
                const double xmax = static_cast<double>(hz.second) * 1e-6;
                if (!(xmax > xmin)) {
                    return;
                }

                const QCPRange r = ax->range();
                double lo = r.lower;
                double hi = r.upper;
                const double fixedSpan = xmax - xmin;
                const double span = hi - lo;

                bool changed = false;
                // 1) Запрет zoom-out: если span стал больше фиксированного — возвращаем фиксированный.
                if (span > fixedSpan) {
                    lo = xmin;
                    hi = xmax;
                    changed = true;
                } else {
                    // 2) Клапан по границам (включая drag): держим видимый диапазон внутри фиксированного.
                    if (lo < xmin) {
                        lo = xmin;
                        hi = xmin + qMin(fixedSpan, span);
                        changed = true;
                    }
                    if (hi > xmax) {
                        hi = xmax;
                        lo = xmax - qMin(fixedSpan, span);
                        changed = true;
                    }
                }
                if (hi <= lo) {
                    lo = xmin;
                    hi = xmax;
                    changed = true;
                }
                if (changed) {
                    QSignalBlocker b(ax);
                    ax->setRange(lo, hi);
                }
                scheduleFhssRedrawAfterAxisChange();
            });

    m_fhssTraces = createSweepTraces(ui->plotWidgetFHSSGraph);
    if (m_fhssTraces.memoryTrace) {
        m_fhssTraces.memoryTrace->setVisible(isSpectrumMaxHoldOn());
    }
    ui->plotWidgetFHSSGraph->legend->setVisible(false);
    ui->plotWidgetFHSSGraph->xAxis->setLabel(QStringLiteral("Frequency, MHz"));
    ui->plotWidgetFHSSGraph->yAxis->setLabel(QStringLiteral("Level, dBm"));
    applyFhssYAxisForCurrentMode();
    ui->plotWidgetFHSSGraph->replot();
    m_fhssPlotInitialized = true;
}

void MainWindow::setFhssTestControlsIdle()
{
    setFhssTestControlsIdle(true);
}

void MainWindow::setFhssTestControlsIdle(bool clearMaxHold)
{
    if (!ui) {
        return;
    }

    m_fhssRunning = false;
    m_fhssDirSwitchPending = false;
    m_fhssTract = -1;
    m_fhssAutoMaxHold = false;
    m_fhssBlockedByPpm = false;
    m_fhssBlockedByAnalyzerDisconnect = false;
    m_fhssBlockedByAntFault = false;
    m_fhssBlockedByDirRestore = false;
    m_fhssReturnToDefaultDirPending = false;
    m_fhssReturnToDefaultDirTract = -1;

    if (clearMaxHold) {
        m_fhssKeepMaxHoldUntilNextStart = false;
        m_fhssMemoryAmps.clear();
        if (m_fhssTraces.memoryTrace) {
            m_fhssTraces.memoryTrace->data()->clear();
            m_fhssTraces.memoryTrace->setVisible(false);
        }
    } else {
        // MaxHold сохраняем до следующего START.
        m_fhssKeepMaxHoldUntilNextStart = true;
        if (m_fhssTraces.memoryTrace && !m_fhssMemoryAmps.isEmpty()) {
            m_fhssTraces.memoryTrace->setVisible(true);
        }
    }

    if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
        m_powerTrafficGenerator->stop();
    }
    if (ui->emissionAntennaWidgetFHSS) {
        ui->emissionAntennaWidgetFHSS->stopTransmission();
        ui->emissionAntennaWidgetFHSS->setVisible(false);
    }

    if (ui->pushButtonStartTestingFHSS) {
        ui->pushButtonStartTestingFHSS->setVisible(true);
        ui->pushButtonStartTestingFHSS->setEnabled(true);
    }
    if (ui->pushButtonFHSSTestStop) {
        ui->pushButtonFHSSTestStop->setVisible(false);
        ui->pushButtonFHSSTestStop->setEnabled(false);
    }
    if (ui->modeFHSSComboBox) {
        ui->modeFHSSComboBox->setEnabled(true);
    }

    // Тест ППРЧ полностью завершён — снимаем «лок» вкладок (если он удерживался isFhssTestActive()).
    updateTabWidgetLockState();
    // Возвращаем кнопки в дефолтное состояние с учётом текущего выбранного тракта/статуса.
    updateFhssTestButtonsAccessForSelectedTract();
}

void MainWindow::setFhssTestControlsRunning(bool running)
{
    if (!ui) {
        return;
    }
    if (ui->pushButtonStartTestingFHSS) {
        ui->pushButtonStartTestingFHSS->setVisible(!running);
    }
    if (ui->pushButtonFHSSTestStop) {
        ui->pushButtonFHSSTestStop->setVisible(running);
    }
    if (ui->modeFHSSComboBox) {
        ui->modeFHSSComboBox->setEnabled(!running);
    }
}

void MainWindow::updateFhssModeComboForTract(int tractNum)
{
    if (!ui || !ui->modeFHSSComboBox) {
        return;
    }

    QStringList items;
    switch (ppmTrmTypeForTract(tractNum)) {
    case 2:
        // То, что изначально задано в дизайнере.
        items << QStringLiteral("МПР") << QStringLiteral("ТМО-4");
        break;
    case 3:
        items << QStringLiteral("МПР")
              << QStringLiteral("ТМО-4")
              << QStringLiteral("ТМО ППРЧ")
              << QStringLiteral("СР ППРЧ");
        break;
    case 4:
        items << QStringLiteral("ДМО ППРЧ")
              << QStringLiteral("СР ППРЧ")
              << QStringLiteral("РОС");
        break;
    default:
        // Для неподдерживаемых трактов не трогаем UI.
        return;
    }

    QSignalBlocker b(ui->modeFHSSComboBox);
    ui->modeFHSSComboBox->clear();
    ui->modeFHSSComboBox->addItems(items);
    ui->modeFHSSComboBox->setCurrentIndex(0);

    // Чтобы «ДМО ППРЧ»/прочие варианты не выглядели как «ДМО» из-за обрезки по ширине.
    // Подбираем минимальную ширину по самому длинному пункту.
    const QFontMetrics fm(ui->modeFHSSComboBox->font());
    int maxTextPx = 0;
    for (const QString &s : items) {
        maxTextPx = qMax(maxTextPx, fm.horizontalAdvance(s));
    }
    // Запас под паддинги, бордер и стрелку дропдауна.
    ui->modeFHSSComboBox->setMinimumWidth(maxTextPx + 60);

    updateFhssStartTestingButtonCaption();
}

void MainWindow::updateFhssStartTestingButtonCaption()
{
    if (!ui || !ui->pushButtonStartTestingFHSS || !ui->modeFHSSComboBox) {
        return;
    }
    const QString suffix = ui->modeFHSSComboBox->currentText().trimmed();
    ui->pushButtonStartTestingFHSS->setText(suffix.isEmpty()
                                                ? QStringLiteral("НАЧАТЬ ТЕСТ")
                                                : QStringLiteral("НАЧАТЬ ТЕСТ ") + suffix);
}

void MainWindow::updateFhssTestButtonsAccessForSelectedTract()
{
    if (!ui) {
        return;
    }
    const int selected = selectedPpmTractFromUi();
    // Важно: кнопку "НАЧАТЬ ТЕСТ ППРЧ" НЕ блокируем просто из-за того, что тракт ещё не успел
    // перейти в "Норма"/зелёную рамку после переключения в framePPM.
    // Готовность тракта валидируем в обработчике onStartTestingFhssClicked(), чтобы UI всегда
    // возвращался к дефолту после смены тракта.
    const bool connected = (m_deviceController && m_deviceController->isConnected());
    const bool allow = connected && m_analyzerConnected && !m_stationDisconnectRecoveryActive
        && isFhssCapableTract(selected)
        && !m_fhssBlockedByPpm
        && !m_fhssBlockedByAnalyzerDisconnect
        && !m_fhssBlockedByAntFault
        && !m_fhssReturnToDefaultDirPending;

    updateFhssStartTestingButtonCaption();

    const bool fhssHasPausedSession =
        m_fhssRunning || m_fhssDirSwitchPending || m_fhssBlockedByPpm || m_fhssBlockedByAnalyzerDisconnect
        || m_fhssBlockedByAntFault || m_fhssBlockedByDirRestore
        || (ui->pushButtonFHSSTestStop && ui->pushButtonFHSSTestStop->isVisible());
    const bool isFhssTargetTractSelected = (m_fhssTract > 0) && (selected == m_fhssTract);
    // При «Авария АНТ» «Стоп» доступен (как на tabPower), чтобы можно было завершить тест вручную.
    const bool allowStopDespiteAntFault =
        connected && m_fhssBlockedByAntFault && fhssHasPausedSession && isFhssTargetTractSelected;

    if (ui->pushButtonStartTestingFHSS) {
        ui->pushButtonStartTestingFHSS->setEnabled(allow);
    }
    const bool allowRunButtons = allow && !m_fhssDirSwitchPending;
    const bool allowStop = allowRunButtons || allowStopDespiteAntFault;
    if (ui->pushButtonFHSSTestStop) {
        ui->pushButtonFHSSTestStop->setEnabled(allowStop);
    }
}

MainWindow::FhssBandSpec MainWindow::currentFhssBandSpec(int tractNum) const
{
    FhssBandSpec spec;
    const QString mode = (ui && ui->modeFHSSComboBox)
                             ? ui->modeFHSSComboBox->currentText().trimmed()
                             : QString();

    auto twoBand = [&](quint64 startHz, quint64 stopHz, quint64 plotLoHz, quint64 plotHiHz) {
        spec.isSingle = false;
        spec.startHz = startHz;
        spec.stopHz = stopHz;
        spec.plotLoHz = plotLoHz;
        spec.plotHiHz = plotHiHz;
    };
    auto singleBand = [&](quint64 centerHz, quint64 halfSpanHz) {
        spec.isSingle = true;
        spec.startHz = centerHz;
        spec.stopHz = 0;
        spec.plotLoHz = (centerHz > halfSpanHz) ? (centerHz - halfSpanHz) : 0;
        spec.plotHiHz = centerHz + halfSpanHz;
    };

    switch (ppmTrmTypeForTract(tractNum)) {
    case 2:
        if (mode == QStringLiteral("ТМО-4")) {
            // Окно 0.7 МГц с центром на 45 МГц.
            singleBand(45000000ULL, kFhssTmo4HalfSpanHz);
        } else {
            // МПР (по умолчанию для тракта 2).
            twoBand(30000000ULL, 180000000ULL, 26000000ULL, 190000000ULL);
        }
        break;
    case 3:
        if (mode == QStringLiteral("ТМО-4")) {
            // Окно 0.7 МГц с центром на 410 МГц.
            singleBand(410000000ULL, kFhssTmo4HalfSpanHz);
        } else if (mode == QStringLiteral("ТМО ППРЧ")) {
            twoBand(380000000ULL, 470000000ULL, 370000000ULL, 480000000ULL);
        } else {
            // МПР и «СР ППРЧ» имеют один и тот же диапазон по ТЗ.
            twoBand(220000000ULL, 470000000ULL, 210000000ULL, 480000000ULL);
        }
        break;
    case 4:
        if (mode == QStringLiteral("РОС")) {
            // Окно 30 МГц с центром на 620 МГц.
            singleBand(620000000ULL, 15000000ULL);
        } else {
            // «ДМО ППРЧ» и «СР ППРЧ» имеют один и тот же диапазон по ТЗ.
            twoBand(520000000ULL, 620000000ULL, 510000000ULL, 630000000ULL);
        }
        break;
    default:
        spec.plotLoHz = static_cast<quint64>(ANALYZER_STREAM_START_HZ_DEFAULT);
        spec.plotHiHz = static_cast<quint64>(ANALYZER_STREAM_STOP_HZ_DEFAULT);
        break;
    }
    return spec;
}

void MainWindow::syncFhssAnalyzerSpectrumRange(int tractNum)
{
    if (!m_analyzerController || !isFhssCapableTract(tractNum)) {
        return;
    }
    m_analyzerController->setAlternateSpectrumRangesEnabled(false);
    if (m_spectrumStreaming) {
        m_analyzerController->stopSpectrumStream();
        m_spectrumStreaming = false;
    }
    const auto r = fhssSpectrumRangeHzForTract(tractNum);
    m_analyzerController->setSpectrumRange(r.first, r.second);
    syncSweepBoundsFromHz(r.first, r.second);
    m_fhssLatestFreqs.clear();
    m_fhssLatestAmps.clear();
    m_fhssDisplayDirty = false;
    m_fhssUiTimer.stop();
}

bool MainWindow::isFhssSpectrumFrameValid(int tractNum, const QVector<double> &freqsMHz) const
{
    if (freqsMHz.size() < 2) {
        return false;
    }
    if (!isFhssCapableTract(tractNum)) {
        return false;
    }

    const auto rangeHz = fhssSpectrumRangeHzForTract(tractNum);
    const double loMHz = static_cast<double>(rangeHz.first) * 1e-6;
    const double hiMHz = static_cast<double>(rangeHz.second) * 1e-6;
    if (!(hiMHz > loMHz)) {
        return false;
    }

    const double frameLo = qMin(freqsMHz.first(), freqsMHz.last());
    const double frameHi = qMax(freqsMHz.first(), freqsMHz.last());
    const double frameSpanMHz = frameHi - frameLo;
    const double expectedSpanMHz = hiMHz - loMHz;
    const double tol = qMax(0.5, expectedSpanMHz * 0.05);
    if (frameLo < loMHz - tol || frameHi > hiMHz + tol) {
        return false;
    }

    const FhssBandSpec spec = currentFhssBandSpec(tractNum);
    if (!spec.isSingle) {
        // Узкие «хвостовые» кадры tabPower/tabHands (≈0.5–1 МГц) не должны рисоваться на широком FHSS-графике.
        constexpr double kMaxStaleNarrowSpanMHz = 2.0;
        if (frameSpanMHz <= kMaxStaleNarrowSpanMHz) {
            return false;
        }
        return frameSpanMHz >= expectedSpanMHz * 0.25;
    }
    return frameSpanMHz >= expectedSpanMHz * 0.4;
}

void MainWindow::applyFhssXAxisForTract(int tractNum)
{
    if (!ui || !ui->plotWidgetFHSSGraph) {
        return;
    }
    const FhssBandSpec spec = currentFhssBandSpec(tractNum);
    const double loMHz = static_cast<double>(spec.plotLoHz) * 1e-6;
    const double hiMHz = static_cast<double>(spec.plotHiHz) * 1e-6;

    ui->plotWidgetFHSSGraph->clearItems();
    ui->plotWidgetFHSSGraph->clearGraphs();
    m_fhssKeepMaxHoldUntilNextStart = false;
    m_fhssMaxHoldTract = -1;
    m_fhssMemoryAmps.clear();
    m_fhssLatestFreqs.clear();
    m_fhssLatestAmps.clear();
    m_fhssDisplayDirty = false;
    m_fhssUiTimer.stop();
    setupFrequencySweepPlot(ui->plotWidgetFHSSGraph, loMHz, hiMHz);

    // На оси X показываем только подписи, соответствующие LCD-значениям:
    // — двухграничный режим: два тика (начало/конец);
    // — одночастотный режим: одна метка-центр.
    {
        QSharedPointer<QCPAxisTickerText> ticker(new QCPAxisTickerText);
        if (spec.isSingle) {
            const double centerMHz = static_cast<double>(spec.startHz) * 1e-6;
            ticker->addTick(centerMHz, QString::number(static_cast<int>(centerMHz + 0.5)));
        } else {
            const double startMHz = static_cast<double>(spec.startHz) * 1e-6;
            const double stopMHz = static_cast<double>(spec.stopHz) * 1e-6;
            if (stopMHz > startMHz && startMHz > 0.0) {
                ticker->addTick(startMHz, QString::number(static_cast<int>(startMHz)));
                ticker->addTick(stopMHz, QString::number(static_cast<int>(stopMHz)));
            } else {
                ticker->addTick(loMHz, QString::number(static_cast<int>(loMHz)));
                ticker->addTick(hiMHz, QString::number(static_cast<int>(hiMHz)));
            }
        }
        ui->plotWidgetFHSSGraph->xAxis->setTicker(ticker);
        ui->plotWidgetFHSSGraph->xAxis->setSubTicks(false);
    }
    m_fhssTraces = createSweepTraces(ui->plotWidgetFHSSGraph);
    if (m_fhssTraces.memoryTrace) {
        m_fhssTraces.memoryTrace->setVisible(isSpectrumMaxHoldOn());
    }
    ui->plotWidgetFHSSGraph->legend->setVisible(false);
    ui->plotWidgetFHSSGraph->xAxis->setLabel(QStringLiteral("Frequency, MHz"));
    ui->plotWidgetFHSSGraph->yAxis->setLabel(QStringLiteral("Level, dBm"));
    applyFhssYAxisForCurrentMode();
    ui->plotWidgetFHSSGraph->replot(QCustomPlot::rpQueuedReplot);
    m_fhssPlotInitialized = true;
}

void MainWindow::applyFhssBandForSelectedMode()
{
    if (!ui) {
        return;
    }
    int tr = selectedPpmTractFromUi();
    if (tr <= 0) {
        tr = (m_ppmCurrentOnTract > 0) ? m_ppmCurrentOnTract : ppmFirstTractNumber();
    }
    if (!isFhssCapableTract(tr)) {
        return;
    }
    applyFhssXAxisForTract(tr);
    updateFhssRangeLcdForTract(tr);
    if (isFhssTabActive()) {
        syncFhssAnalyzerSpectrumRange(tr);
        if (m_analyzerConnected) {
            startSpectrumStream();
        }
    }

    // «Ножка/излучатель» допустима только в МПР-режиме. На остальных режимах
    // принудительно гасим её, даже если придёт IND_CHREADY (на случай, если тест уже запущен/паузил).
    if (ui->emissionAntennaWidgetFHSS && !isFhssModeMpr()) {
        ui->emissionAntennaWidgetFHSS->stopTransmission();
        ui->emissionAntennaWidgetFHSS->setVisible(false);
    }
}

bool MainWindow::isFhssModeMpr() const
{
    if (!ui || !ui->modeFHSSComboBox) {
        return false;
    }
    return ui->modeFHSSComboBox->currentText().trimmed() == QStringLiteral("МПР");
}

bool MainWindow::isFhssModeTmo4() const
{
    if (!ui || !ui->modeFHSSComboBox) {
        return false;
    }
    return ui->modeFHSSComboBox->currentText().trimmed() == QStringLiteral("ТМО-4");
}

void MainWindow::applyFhssYAxisForCurrentMode()
{
    if (!ui || !ui->plotWidgetFHSSGraph) {
        return;
    }
    // Как на plotWidgetPowerGraph: к «сырым» границам оси Y прибавляем ёмкость радиотракта (+60 dBm).
    if (isFhssModeTmo4()) {
        ui->plotWidgetFHSSGraph->yAxis->setRange(-120.0 + kPowerGraphRadiopathOffsetDbm,
                                                  -20.0 + kPowerGraphRadiopathOffsetDbm);
    } else {
        ui->plotWidgetFHSSGraph->yAxis->setRange(-150.0 + kPowerGraphRadiopathOffsetDbm,
                                                  20.0 + kPowerGraphRadiopathOffsetDbm);
    }
}

QPair<quint64, quint64> MainWindow::fhssPlotXAxisRangeHzForTract(int tractNum) const
{
    return fhssSpectrumRangeHzForTract(tractNum);
}

QPair<quint64, quint64> MainWindow::fhssSpectrumRangeHzForTract(int tractNum) const
{
    const FhssBandSpec spec = currentFhssBandSpec(tractNum);
    if (spec.plotHiHz > spec.plotLoHz) {
        return {spec.plotLoHz, spec.plotHiHz};
    }
    return {static_cast<quint64>(ANALYZER_STREAM_START_HZ_DEFAULT),
            static_cast<quint64>(ANALYZER_STREAM_STOP_HZ_DEFAULT)};
}

void MainWindow::updateFhssRangeLcdForTract(int tractNum)
{
    if (!ui) {
        return;
    }
    const FhssBandSpec spec = currentFhssBandSpec(tractNum);

    if (ui->lcdFHSSStartRangeValue) {
        ui->lcdFHSSStartRangeValue->display(
            formatGroupedWithDots(static_cast<uint32_t>(qMin<quint64>(spec.startHz, 0xFFFFFFFFULL))));
    }

    // Подпись слева от LCD: «F, Hz» для одночастотных режимов, «Range, Hz» для диапазонных.
    if (ui->labelFHSSRangeCaption) {
        ui->labelFHSSRangeCaption->setText(spec.isSingle ? QStringLiteral("F, Hz")
                                                         : QStringLiteral("Range, Hz"));
    }

    if (spec.isSingle) {
        // Одночастотные режимы (ТМО-4, РОС): прячем «-» и второе значение, чтобы был один LCD.
        if (ui->lcdFHSSRangeValueDash) {
            ui->lcdFHSSRangeValueDash->setVisible(false);
        }
        if (ui->lcdFHSSEndRangeValue) {
            ui->lcdFHSSEndRangeValue->setVisible(false);
        }
    } else {
        if (ui->lcdFHSSRangeValueDash) {
            ui->lcdFHSSRangeValueDash->setVisible(true);
            ui->lcdFHSSRangeValueDash->setDigitCount(1);
            ui->lcdFHSSRangeValueDash->display(QStringLiteral("-"));
        }
        if (ui->lcdFHSSEndRangeValue) {
            ui->lcdFHSSEndRangeValue->setVisible(true);
            if (spec.stopHz <= 0xFFFFFFFFULL) {
                ui->lcdFHSSEndRangeValue->display(formatGroupedWithDots(static_cast<uint32_t>(spec.stopHz)));
            } else {
                // formatGroupedWithDots принимает uint32_t → для значений >4 ГГц используем ручной формат.
                ui->lcdFHSSEndRangeValue->display(QStringLiteral("2.500.000.000"));
            }
        }
    }
}

bool MainWindow::isFhssTabActive() const
{
    return ui && ui->tabWidget && m_tabFhssIndex >= 0 && ui->tabWidget->currentIndex() == m_tabFhssIndex;
}

bool MainWindow::isFhssTestActive() const
{
    // Считаем тест ППРЧ «активным» во всех состояниях, кроме полного idle:
    //  • RTP/мощность реально подаётся (m_fhssRunning);
    //  • ожидание выбранного в комбобоксе DirId (m_fhssDirSwitchPending);
    //  • пауза из-за «Нет связи с ПП» / «Авария АНТ» / внешней смены направления.
    return m_fhssRunning || m_fhssDirSwitchPending || m_fhssBlockedByPpm || m_fhssBlockedByAnalyzerDisconnect
        || m_fhssBlockedByAntFault || m_fhssBlockedByDirRestore || m_fhssReturnToDefaultDirPending;
}

void MainWindow::attemptScheduleDelayedFhssTestResume(int tr)
{
    // Возобновляем ППРЧ-тест после «Норма», если он стоит на внешней паузе.
    // Зеркалим логику attemptScheduleDelayedPowerTestResume: даём короткий запас по времени,
    // чтобы статус успел стабилизироваться, и проверяем условия повторно перед стартом.
    if (m_fhssTract <= 0 || tr != m_fhssTract) {
        return;
    }
    if (!m_analyzerConnected) {
        return;
    }
    // Тест должен быть в «активном» (paused) состоянии, иначе возобновлять нечего.
    if (!m_fhssRunning && !m_fhssDirSwitchPending) {
        return;
    }
    if (m_fhssBlockedByPpm || m_fhssBlockedByAnalyzerDisconnect || m_fhssBlockedByAntFault) {
        return; // ещё не «Норма» / есть другая блокирующая причина
    }
    if (m_fhssBlockedByDirRestore) {
        return;
    }
    if (!isPpmTractReadyForPowerTest(tr)) {
        return;
    }

    constexpr int kFhssResumeDelayMs = 1500;
    const quint64 serial = ++m_fhssResumeAfterPpmSerial;

    QTimer::singleShot(kFhssResumeDelayMs, this, [this, serial, tr]() {
        if (serial != m_fhssResumeAfterPpmSerial) {
            return; // отменено более поздним событием
        }
        if (m_fhssTract <= 0 || tr != m_fhssTract) {
            return;
        }
        if (m_fhssBlockedByPpm || m_fhssBlockedByAnalyzerDisconnect || m_fhssBlockedByAntFault) {
            return;
        }
        if (m_fhssBlockedByDirRestore) {
            return;
        }
        if (!isPpmTractReadyForPowerTest(tr)) {
            updateFhssTestButtonsAccessForSelectedTract();
            return;
        }
        if (!ui || !ui->pushButtonFHSSTestStop) {
            return;
        }

        if (m_fhssDirSwitchPending) {
            // Ждали выбранный DirId и в этот момент пришла «Нет связи с ПП»/«Авария АНТ».
            const uint8_t expDir = fhssExpectedDirIdFromModeCombo();
            if (m_deviceController && m_deviceController->isConnected()) {
                armSelfIssuedDirOp(tr, expDir);
                armSelfIssuedTractReload(tr);
                if (!m_deviceController->setCurrentDirection(static_cast<uint8_t>(tr), expDir)) {
                    DEBUG << QStringLiteral("ППРЧ: не удалось повторно отправить CMD_CURR_DIR_SET DirId=%1 (тракт %2).")
                                 .arg(static_cast<int>(expDir))
                                 .arg(tr);
                    return;
                }
                m_deviceController->requestAllIndications(static_cast<uint8_t>(tr));
                DEBUG << QStringLiteral("ППРЧ: после «Норма» повторное переключение направления DirId=%1 (тракт %2).")
                             .arg(static_cast<int>(expDir))
                             .arg(tr);
            }
            return;
        }

        if (m_fhssRunning) {
            DEBUG << QStringLiteral("ППРЧ: «Норма» получена — возобновление подачи мощности (тракт %1).").arg(tr);
            // startFhssTransmission(): для «МПР» перезапустит RTP, для прочих режимов — только UI/диапазон.
            // Если запуск не удался — функция переведёт UI в idle и снимет состояние теста.
            startFhssTransmission();
            updateFhssTestButtonsAccessForSelectedTract();
        }
    });
}

void MainWindow::onStartTestingFhssClicked()
{
    if (!m_deviceController || !m_deviceController->isConnected()) {
        onDeviceLogMessage(QStringLiteral("ОШИБКА: нет подключения к радиостанции (ППРЧ)."));
        return;
    }
    const int tract = selectedPpmTractFromUi();
    if (!isFhssCapableTract(tract)) {
        onDeviceLogMessage(QStringLiteral("ОШИБКА: для теста ППРЧ выберите тракт МВ/ДМВ1/ДМВ2."));
        return;
    }
    if (m_fhssReturnToDefaultDirPending) {
        onDeviceLogMessage(QStringLiteral("ППРЧ: дождитесь завершения загрузки DirId=1 после остановки предыдущего теста."));
        updateFhssTestButtonsAccessForSelectedTract();
        return;
    }
    if (!isPpmTractReadyForPowerTest(tract)) {
        onDeviceLogMessage(
            QStringLiteral("ОШИБКА: тракт %1 не готов для ППРЧ (ожидается «Норма»/«Перегрев ЛУМ» и зелёная рамка).")
                .arg(tract));
        // UI остаётся в idle: кнопка должна оставаться доступной после переключения тракта.
        updateFhssTestButtonsAccessForSelectedTract();
        return;
    }

    // Перед выходом на мощность гарантируем, что тракт включён.
    // Это приближает сценарий к Station_starter_3, где "выход на мощность" делается на заранее включённом тракте.
    if (!m_deviceController->setTractControl(static_cast<uint8_t>(tract), true, true)) {
        onDeviceLogMessage(QStringLiteral("ППРЧ: предупреждение — не удалось отправить команду включения тракта %1.")
                               .arg(tract));
    }

    // 1) Показать start/stop и скрыть "НАЧАТЬ ТЕСТ ППРЧ", но пока заблокировать.
    setFhssTestControlsRunning(true);
    if (ui->pushButtonFHSSTestStop) {
        ui->pushButtonFHSSTestStop->setEnabled(false);
    }
    // По требованию: "ножка/излучатель" виден сразу при старте теста,
    // а пульсация включается только по индикации реального TX (IND_CHREADY).
    // По ТЗ показываем виджет только для режима «МПР»; для прочих режимов держим скрытым.
    if (ui->emissionAntennaWidgetFHSS) {
        ui->emissionAntennaWidgetFHSS->stopTransmission();
        ui->emissionAntennaWidgetFHSS->setVisible(isFhssModeMpr());
    }

    m_fhssTract = tract;
    // Сразу после старта ППРЧ-теста — заблокировать остальные вкладки (как в tabPower).
    updateTabWidgetLockState();
    // Требование: на FHSS-графике maxhold должен появляться автоматически (без pushButtonSpectrumMaxHold).
    m_fhssAutoMaxHold = true;
    // Новый старт теста: maxhold должен быть очищен именно сейчас.
    m_fhssKeepMaxHoldUntilNextStart = false;
    m_fhssMaxHoldTract = tract;
    applyFhssXAxisForTract(tract);
    updateFhssRangeLcdForTract(tract);

    syncFhssAnalyzerSpectrumRange(tract);
    if (m_analyzerConnected && isFhssTabActive()) {
        startSpectrumStream();
    }

    const uint8_t fhssExpDir = fhssExpectedDirIdFromModeCombo();
    // 2) Переключить направление на выбранный в modeFHSSComboBox DirId и ждать загрузки.
    m_fhssDirSwitchPending = true;
    const QString modeName = (ui && ui->modeFHSSComboBox)
                                 ? ui->modeFHSSComboBox->currentText().trimmed()
                                 : QString();
    QString tractName = selectedPpmTractDisplayNameFromUi();
    if (tractName.isEmpty()) {
        tractName = QString::number(tract);
    }
    onDeviceLogMessage(QStringLiteral("Запуск режима %1 на тракте %2.")
                           .arg(modeName.isEmpty() ? QStringLiteral("—") : modeName, tractName));
    armSelfIssuedDirOp(tract, fhssExpDir);
    armSelfIssuedTractReload(tract);
    if (!m_deviceController->setCurrentDirection(static_cast<uint8_t>(tract), fhssExpDir)) {
        m_fhssDirSwitchPending = false;
        clearSelfIssuedGuardsForTract(tract);
        onDeviceLogMessage(QStringLiteral("ОШИБКА: не удалось отправить CMD_CURR_DIR_SET (ППРЧ)."));
        setFhssTestControlsIdle();
        return;
    }
    m_deviceController->requestAllIndications(static_cast<uint8_t>(tract));

    // Если по последней индикации направление уже нужное, IND_ACTIVEDIR может не прийти повторно.
    if (m_ppmLastDirIdByTract.value(tract, 0) == fhssExpDir) {
        m_fhssDirSwitchPending = false;
        if (ui->pushButtonFHSSTestStop) {
            ui->pushButtonFHSSTestStop->setEnabled(true);
        }
        QTimer::singleShot(0, this, [this]() { startFhssTransmission(); });
    }
}

bool MainWindow::startFhssTransmission()
{
    if (!ui || !m_deviceController || !m_deviceController->isConnected()) {
        onDeviceLogMessage(QStringLiteral("ОШИБКА: нельзя стартовать ППРЧ (нет подключения)."));
        setFhssTestControlsIdle();
        return false;
    }
    if (!isFhssCapableTract(m_fhssTract)) {
        onDeviceLogMessage(QStringLiteral("ОШИБКА: некорректный тракт для ППРЧ."));
        setFhssTestControlsIdle();
        return false;
    }
    if (m_fhssDirSwitchPending) {
        return false;
    }

    // Освобождаем общий PowerTrafficGenerator перед стартом ППРЧ.
    if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
        m_powerTrafficGenerator->stop();
    }

    // RTP multicast нужен только для «МПР»; прочие режимы сами выходят на мощность после загрузки DirId.
    if (isFhssModeMpr()) {
        if (!m_powerTrafficGenerator) {
            onDeviceLogMessage(QStringLiteral("ОШИБКА: нельзя стартовать ППРЧ (нет генератора трафика)."));
            setFhssTestControlsIdle();
            return false;
        }

        const int trmType = ppmTrmTypeForTract(m_fhssTract);
        const int mcastSuffix = (trmType >= 2 && trmType <= 4) ? trmType : 3;
        const QString mcast = QStringLiteral("224.0.1.%1").arg(mcastSuffix);
        const quint16 tetraPort = static_cast<quint16>(12000 + 2 * RTP_PAYLOAD_TYPE_TETRA_HR); // 12160
        m_powerTrafficGenerator->setBindIp(m_deviceController->config().selfIp);
        m_powerTrafficGenerator->setMulticastAddress(mcast);
        // Как в Station_starter_3::on_pushButtonmpr_clicked(): 224.0.1.X:12160, PT=80, 30мс
        m_powerTrafficGenerator->setMulticastPort(tetraPort);
        m_powerTrafficGenerator->setSourcePort(tetraPort);
        m_powerTrafficGenerator->setIntervalMs(TRAFFIC_INTERVAL_TETRA_MS);
        m_powerTrafficGenerator->setDscp(DSCP_STREAMVOICE);
        m_powerTrafficGenerator->setEcn(ECN_DEFAULT);
        m_powerTrafficGenerator->setPayloadType(RTP_PAYLOAD_TYPE_TETRA_HR);
        m_powerTrafficGenerator->setTractNumber(static_cast<uint8_t>(m_fhssTract));

        if (!m_powerTrafficGenerator->start()) {
            onDeviceLogMessage(QStringLiteral("ОШИБКА: не удалось запустить поток ППРЧ (%1).").arg(mcast));
            setFhssTestControlsIdle();
            return false;
        }

        DEBUG << QStringLiteral("ППРЧ: подача мощности запущена (%1:%2, PT=%3, %4мс).")
                     .arg(mcast)
                     .arg(tetraPort)
                     .arg(static_cast<int>(RTP_PAYLOAD_TYPE_TETRA_HR))
                     .arg(TRAFFIC_INTERVAL_TETRA_MS);
    }

    m_fhssRunning = true;
    // Визуализацию "излучения" включаем не по факту запуска RTP,
    // а по индикации IND_CHREADY (реальный TX), как сиреневый Frame_ppm_status в пульте.
    setFhssTestControlsRunning(true);

    if (isFhssTabActive()) {
        syncFhssAnalyzerSpectrumRange(m_fhssTract);
        if (m_analyzerConnected) {
            startSpectrumStream();
        }
    }
    return true;
}

void MainWindow::onFhssStopClicked()
{
    const bool stopDuringAntFault = m_fhssBlockedByAntFault;
    if (stopDuringAntFault) {
        setPpmUpdateLabelVisible(true);
    }

    const int tract = m_fhssTract;
    bool waitDefaultDirLoaded = false;
    if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
        m_powerTrafficGenerator->stop();
    }

    if (m_deviceController && m_deviceController->isConnected() && tract > 0) {
        armSelfIssuedDirOp(tract, 1);
        armSelfIssuedTractReload(tract);
        if (!m_deviceController->setCurrentDirection(static_cast<uint8_t>(tract), 1)) {
            DEBUG << QStringLiteral("ППРЧ: не удалось вернуть направление DirId=1 для тракта %1.")
                         .arg(tract);
        } else {
            waitDefaultDirLoaded = true;
            m_deviceController->requestAllIndications(static_cast<uint8_t>(tract));
        }
    }

    const QString modeName = (ui && ui->modeFHSSComboBox)
                                 ? ui->modeFHSSComboBox->currentText().trimmed()
                                 : QString();
    QString tractName = selectedPpmTractDisplayNameFromUi();
    if (tractName.isEmpty() && tract > 0) {
        const int idx = m_ppmTractsSorted.indexOf(tract);
        if (idx >= 0 && m_ppmButtonGroup) {
            if (QAbstractButton *btn = m_ppmButtonGroup->button(idx)) {
                const QString fallback = btn->text().trimmed();
                if (!fallback.isEmpty() && fallback != QStringLiteral("—")) {
                    tractName = fallback;
                }
            }
        }
    }
    if (tractName.isEmpty() && tract > 0) {
        tractName = QString::number(tract);
    }
    onDeviceLogMessage(QStringLiteral("Режим %1 на тракте %2 остановлен.")
                           .arg(modeName.isEmpty() ? QStringLiteral("—") : modeName,
                                tractName.isEmpty() ? QStringLiteral("—") : tractName));
    m_fhssMaxHoldTract = tract;
    setFhssTestControlsIdle(false);
    if (waitDefaultDirLoaded) {
        m_fhssReturnToDefaultDirPending = true;
        m_fhssReturnToDefaultDirTract = tract;
        DEBUG << QStringLiteral("ППРЧ: ожидаю завершения загрузки DirId=1 (тракт %1)...").arg(tract);
        if (ui->pushButtonStartTestingFHSS) {
            ui->pushButtonStartTestingFHSS->setEnabled(false);
        }
        if (ui->modeFHSSComboBox) {
            ui->modeFHSSComboBox->setEnabled(false);
        }
        updateTabWidgetLockState();
        tryFinishFhssReturnToDefaultDirection(tract);
    }
}
