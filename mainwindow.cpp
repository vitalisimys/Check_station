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
#include <QPixmap>
#include <algorithm>
#include <cmath>
#include <utility>
#include <QtConcurrent>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSet>
#include <QRandomGenerator>
#include <QMap>
#include <QHash>
#include <QStringList>
#include <QPushButton>
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
#include <QPainter>
#include <QPolygon>
#include <QMovie>
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
constexpr quint64 kHandsMaxFreqHz = 2500000000ULL; // 2500.000.000 Гц
constexpr uint32_t kPowerTestStartFreqHz = 30125000U; // 30.025.000 Гц
constexpr uint32_t kPowerTestStartFreqType3Hz = 220125000U; // 220.025.000 Гц
constexpr uint32_t kPowerTestStartFreqType4Hz = 520125000U; // 520.025.000 Гц
constexpr int kPowerTestDurationMs = 4000;
constexpr int kPowerTestPauseBetweenStepsMs = 1000;
constexpr int kPowerTestPauseBetweenRemeasureMs = 2000;
constexpr quint64 kPowerTestAnalyzerSpanHz = 1000000ULL; // sweep 1 МГц для live-спектра в tabPower
constexpr double kPowerTestMomentHalfWindowMHz = 0.04; // отображаем ±50 кГц вокруг несущей
constexpr quint64 kPowerGraphWideSpanHz = 500000ULL; // 0.5 МГц для power-оценки в tabPower (plotWidgetPowerGraph)
constexpr double kPowerGraphRadiopathOffsetDbm = 60.0; // ёмкость радиотракта от станции до анализатора
constexpr double kPowerGraphAutoYHalfRangeDbm = 10.0;
constexpr double kPowerGraphInitialYHalfRangeDbm = 2.5; // зелёная зона ±2 dBm + 0.5 dBm красной зоны
constexpr double kPowerGraphMaxLevelCenterDbm = 46.0;
/** Мин. мощность: номинал для TrmType 4 (и неизвестного типа). */
constexpr double kPowerGraphMinLevelCenterDbmTrmType4 = 30.0;
/** Мин. мощность: номинал для TrmType 2 и 3. */
constexpr double kPowerGraphMinLevelCenterDbmTrmType23 = 36.0;
constexpr int kPowerTestRemeasureMaxCount = 3; // максимум переизмерений шага на одной частоте

inline double powerGraphAnalyzerToRealDbm(double analyzerDbm)
{
    return analyzerDbm + kPowerGraphRadiopathOffsetDbm;
}

inline bool powerAmpInsideGreenBand(double dbm, double centerDbm)
{
    const double hi = centerDbm + 2.0;
    const double lo = centerDbm - 2.0;
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
constexpr const char *kTestProfileResourcePath = ":/profile_active_TEST.tar.gz";
constexpr const char *kTestProfileRemotePath = "/tmp/profile_active_TEST.tar.gz";
constexpr const char *kStationSshUser = "root";
constexpr const char *kStationSshPassword = "zxcvbn";
constexpr const char *kTraktParamRemotePath = "/radio/configs/TraktParam.xml";
constexpr const char *kTemplateProfileRootDirName = "Profile_Active";

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

bool buildCustomizedProfileArchive(const QString &stationIp,
                                  SSHer &ssher,
                                  const QString &templateTarPath,
                                  const QString &outTarPath,
                                  QString *errorText,
                                  QVector<TraktParamEntry> *outEntriesForUi = nullptr,
                                  int *outTraktNumForUi = nullptr)
{
    bool okStation = false;
    const int stationNum = stationNumFromIp(stationIp, &okStation);
    if (!okStation || stationNum <= 0) {
        if (errorText) *errorText = QString("Не удалось определить номер станции из IP: %1").arg(stationIp);
        return false;
    }

    // 1) Скачиваем TraktParam.xml
    QTemporaryFile traktTmp(QDir::tempPath() + "/TraktParam_XXXXXX.xml");
    traktTmp.setAutoRemove(true);
    if (!traktTmp.open()) {
        if (errorText) *errorText = QString("Не удалось создать временный файл TraktParam.xml: %1").arg(traktTmp.errorString());
        return false;
    }
    const QString traktLocal = traktTmp.fileName();
    traktTmp.close();

    if (!ssher.downloadFile(QString::fromLatin1(kTraktParamRemotePath), traktLocal)) {
        if (errorText) *errorText = ssher.lastError().isEmpty()
                                        ? QString("Не удалось скачать %1").arg(QString::fromLatin1(kTraktParamRemotePath))
                                        : ssher.lastError();
        return false;
    }
    QFile traktFile(traktLocal);
    if (!traktFile.open(QIODevice::ReadOnly)) {
        if (errorText) *errorText = QString("Не удалось прочитать TraktParam.xml: %1").arg(traktFile.errorString());
        return false;
    }
    const QByteArray traktXml = traktFile.readAll();
    traktFile.close();

    QVector<TraktParamEntry> traktEntries;
    int traktNum = 0;
    if (!parseTraktParamXml(traktXml, &traktEntries, &traktNum, errorText)) {
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
    m_uptime.start();
    // По новой логике меню изначально скрыто.
    ui->menubar->setVisible(false);
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
    connect(m_deviceController, &DeviceController::logMessage,
            this, &MainWindow::onDeviceLogMessage);
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
    connect(m_deviceController, &DeviceController::workModeIndicationReceived,
            this, &MainWindow::onWorkModeIndicationReceived);
    connect(m_deviceController, &DeviceController::activeDirectionIndicationReceived,
            this, &MainWindow::onActiveDirectionIndicationReceived);

    m_powerTrafficGenerator = new PowerTrafficGenerator(this);
    connect(m_powerTrafficGenerator, &PowerTrafficGenerator::logMessage,
            this, &MainWindow::onDeviceLogMessage);
    connect(m_powerTrafficGenerator, &PowerTrafficGenerator::errorOccurred,
            this, &MainWindow::onDeviceError);

    // "Авария антенны": отдельный поток с таймером, который будет триггерить короткие пульсы трафика
    // (чтобы станция могла "выйти на мощность" и устранить причину аварии без перезапуска режима).
    m_antFaultPulseThread = new QThread(this);
    m_antFaultPulseTimer = new QTimer(nullptr);
    m_antFaultPulseTimer->setInterval(5000);
    m_antFaultPulseTimer->setTimerType(Qt::CoarseTimer);
    m_antFaultPulseTimer->moveToThread(m_antFaultPulseThread);
    connect(m_antFaultPulseThread, &QThread::finished, m_antFaultPulseTimer, &QObject::deleteLater);
    connect(m_antFaultPulseTimer, &QTimer::timeout, this, &MainWindow::onAntennaFaultPulseTick, Qt::QueuedConnection);
    m_antFaultPulseThread->start();

    setStationDisconnectedUi();
    setAnalyzerDisconnectedUi();
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
    if (m_tabHandsIndex < 0 || m_tabPowerIndex < 0 || m_tabReceiveIndex < 0) {
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
        }
    }
    onTabWidgetCurrentChanged(ui->tabWidget->currentIndex());

    if (QPushButton *holdBtn = ui->pushButtonSpectrumClearHold) {
        holdBtn->setCheckable(true);
        holdBtn->setAutoDefault(false);
        holdBtn->setDefault(false);
        connect(holdBtn, &QPushButton::toggled, this, &MainWindow::onSpectrumMaxHoldToggled);
    }

    initPowerTestingUi();
    initReceiveTestingUi();

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
        connect(ui->pushButtonStartTesting, &QPushButton::clicked,
                this, &MainWindow::onStartTestingClicked);
    }
    if (ui->progressBar) {
        ui->progressBar->setTextVisible(false);
        ui->progressBar->setRange(0, 100);
        ui->progressBar->setValue(0);
        ui->progressBar->setVisible(false);
    }

    m_spectrumUiTimer.setInterval(33);
    m_spectrumUiTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_spectrumUiTimer, &QTimer::timeout, this, &MainWindow::onSpectrumUiTimer);

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
            onDeviceLogMessage(QStringLiteral("ОШИБКА: не удалось начать подачу мощности после паузы (нет подключения)."));
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
            onDeviceLogMessage(QStringLiteral("ОШИБКА: не удалось запустить генератор трафика после паузы."));
            ui->pushButtonStartTestingPower->setChecked(false);
            return;
        }

        m_powerTrafficStartPending = false;
        m_powerMeasurementRunning = true;
        setEmissionAnimating(true);
        m_powerTestAutoStopTimer.start(kPowerTestDurationMs);
        onDeviceLogMessage(QStringLiteral("▶ Подача мощности включена, идет окно измерения 5 секунд."));
    });

    updateTabWidgetLockState();
}

MainWindow::~MainWindow()
{
    cleanupAddedSelfIp();

    if (m_antFaultPulseTimer) {
        QMetaObject::invokeMethod(m_antFaultPulseTimer, [t = m_antFaultPulseTimer]() {
            t->stop();
        }, Qt::QueuedConnection);
    }
    if (m_antFaultPulseThread) {
        m_antFaultPulseThread->quit();
        m_antFaultPulseThread->wait(1500);
    }
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

void MainWindow::closeEvent(QCloseEvent *event)
{
    cleanupAddedSelfIp();
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
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

    // Зеленая зона: center±2 dBm, максимум по насыщенности в center.
    // Важно: это один прямоугольник с трехточечным градиентом, без видимых границ «полос».
    addRectWithVerticalGradient(centerDbm + 2.0,
                                centerDbm - 2.0,
                                {
                                    // На границах нельзя уходить в "почти ноль",
                                    // иначе при стыковке с красным получится визуальная "пустота".
                                    {0.0, withAlpha(greenBase, 55)},  // верх (минимум)
                                    {0.5, withAlpha(greenBase, 110)}, // центр (максимум)
                                    {1.0, withAlpha(greenBase, 55)}   // низ (минимум)
                                });

    // Красный верх: center+2..center+4 (без прозрачности: у зелёной границы приглушённо, снаружи насыщеннее)
    addRectWithVerticalGradient(centerDbm + 4.0,
                                centerDbm + 2.0,
                                {
                                    {0.0, redStrong}, // верх
                                    {1.0, redMuted}   // граница с зелёным
                                });

    // Красный низ: center-2..center-4 (без прозрачности: у зелёной границы приглушённо, снаружи насыщеннее)
    addRectWithVerticalGradient(centerDbm - 2.0,
                                centerDbm - 4.0,
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
    dialog.exec();
}

void MainWindow::startAutoDiscovery()
{
    QtConcurrent::run([this]() {
        const QStringList ifaces = collectEligibleInterfaces();
        QMetaObject::invokeMethod(this, [this, ifaces]() {
            handleDiscoveryFinished(ifaces);
        }, Qt::QueuedConnection);
    });
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
    m_cachedIfaces = ifaces;
    // Требование: не скрывать меню настроек, даже если интерфейсы не найдены.
    ui->menubar->setVisible(true);

    if (ifaces.isEmpty()) {
        onDeviceLogMessage("Ethernet-интерфейсы не найдены. Откройте настройки и выберите интерфейс вручную.");
        return;
    }

    onDeviceLogMessage(QString("Найдено интерфейсов: %1").arg(ifaces.size()));

    // Если интерфейс один — сразу ищем станции на нём.
    if (ifaces.size() == 1) {
        const QString iface = ifaces.value(0);
        onDeviceLogMessage(QString("Поиск радиостанций на интерфейсе %1...").arg(iface));

        QtConcurrent::run([this, iface]() {
            const QVector<QString> foundIps = m_finder ? m_finder->searchStations(iface) : QVector<QString>();
            QMetaObject::invokeMethod(this, [this, iface, foundIps]() {
                handleStationsFound(iface, foundIps);
            }, Qt::QueuedConnection);
        });
        return;
    }

    // Интерфейсов несколько — дальнейший выбор/поиск делаем через настройки.
    onDeviceLogMessage("Интерфейсов несколько. Откройте настройки и выберите интерфейс для поиска станции.");
}

void MainWindow::handleStationsFound(const QString &iface, const QVector<QString> &foundIps)
{
    m_cachedFoundIpsByIface.insert(iface, foundIps);

    // Повторяем логику выбора *.193 по подсетям (как в SettingsDialog).
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

    const int stationCount = chosenBySubnet.size();
    // Меню скрываем только в случае "1 интерфейс + 1 станция".
    // Во всех остальных случаях меню показываем.
    const bool singleIfaceSingleStation = (m_cachedIfaces.size() == 1 && stationCount == 1);
    ui->menubar->setVisible(!singleIfaceSingleStation);

    if (stationCount == 0) {
        onDeviceLogMessage(QString("Радиостанции на %1 не найдены. Откройте настройки и выберите станцию/интерфейс.").arg(iface));
        return;
    }

    onDeviceLogMessage(QString("Найдено станций на %1: %2").arg(iface).arg(stationCount));

    // Если по итоговой логике выбора станция ровно одна — подключаемся автоматически.
    if (stationCount == 1) {
        const QString stationIp = chosenBySubnet.cbegin().value();
        onDeviceLogMessage(QString("Автоподключение к станции %1 (интерфейс %2)...").arg(stationIp, iface));
        onStationConnectRequested(stationIp, iface);
        return;
    }

    // Станций несколько — пользователь выберет в настройках.
    onDeviceLogMessage("Станций найдено несколько. Откройте настройки и выберите станцию для подключения.");
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
        if (errorText) *errorText = QString("Некорректный IP станции: %1").arg(stationIp);
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
    return true;
}

void MainWindow::onStationConnectRequested(const QString &stationIp, const QString &interfaceName) {
    const QString ip = stationIp.trimmed();
    const QString iface = interfaceName.trimmed();
    if (ip.isEmpty() || iface.isEmpty()) {
        onDeviceLogMessage("Подключение не выполнено: не выбран IP станции или интерфейс.");
        return;
    }

    // ВАЖНО: сетевую подготовку (nmcli + выбор selfIp) делаем асинхронно,
    // чтобы UI не блокировался и окно настроек могло скрыться сразу после выбора.
    onDeviceLogMessage(QString("Подготовка сетевого подключения (интерфейс %1) к станции %2...").arg(iface, ip));

    QtConcurrent::run([this, ip, iface]() {
        QString selfIp;
        QString err;
        const bool ok = ensureStationIpsConfigured(iface, ip, &selfIp, &err);
        QMetaObject::invokeMethod(this, [this, ok, err, ip, iface, selfIp]() {
            if (!ok) {
                onDeviceLogMessage(QString("Подключение не выполнено: %1").arg(err));
                return;
            }

            // После подготовки — подключаемся в UI-потоке.
            if (m_deviceController && m_deviceController->isConnected()) {
                m_deviceController->disconnectFromDevice();
            }

            setStartTestingButtonEnabled(false);
            ui->frameStation->setVisible(true);

            if (m_deviceController) {
                if (!selfIp.trimmed().isEmpty()) {
                    m_deviceController->setSelfIp(selfIp.trimmed());
                    onDeviceLogMessage(QString("Выбран self IP контроллера: %1").arg(selfIp.trimmed()));
                }
                m_deviceController->setStationIp(ip);
            }

            onDeviceLogMessage(QString("Запрос подключения к станции %1").arg(ip));

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
                const QPair<bool, QString> res = executeCommand(cmd);
                entry.connectionUuid = res.second.trimmed().split('\n', Qt::SkipEmptyParts).value(0).trimmed();
            }

            const bool exists = std::any_of(m_addedIps.cbegin(), m_addedIps.cend(), [&entry](const AddedIpEntry &e) {
                return e.iface == entry.iface && e.ip == entry.ip && e.cidr == entry.cidr;
            });
            if (!exists && !entry.ip.isEmpty() && entry.cidr > 0 && !entry.iface.isEmpty()) {
                m_addedIps.push_back(entry);
            }

            if (m_deviceController) {
                m_deviceController->connectToDevice();
            }
        }, Qt::QueuedConnection);
    });
}

void MainWindow::onDeviceConnected(const QString &ip) {
    setStationConnectedUi();
    ui->frameStation->setVisible(true);
    // Номер станции берём из IP (подсеть 192.168.X.Y -> X)
    const QStringList parts = ip.trimmed().split('.');
    if (parts.size() == 4) {
        bool ok = false;
        const int stationNum = parts[2].toInt(&ok);
        if (ok) {
            ui->labelStation->setText(QString("Станция №%1").arg(stationNum));
        }
    }
    onDeviceLogMessage(QString("Успешное подключение к р/станции: %1").arg(ip));

    // По ТЗ: сразу после подключения получаем TraktParam.xml по SSH
    // и формируем новый profile_active_TEST.tar.gz (отправка — только по кнопке).
    prepareTestProfileAfterConnect(ip);

    // Контроль целостности профиля: если это переподключение после reboot, запускаем проверку.
    if (m_profileIntegrityStage == ProfileIntegrityStage::Reconnecting &&
        !m_profileIntegrityStationIp.trimmed().isEmpty() &&
        ip.trimmed() == m_profileIntegrityStationIp.trimmed()) {
        m_postRebootReconnectTimer.stop();
        m_postRebootWaitTimer.stop();
        m_postRebootWaitProgressTimer.stop();
        // ВАЖНО: после переподключения держим progressBar в бесконечном режиме,
        // а framePPM скрытым — до завершения логики включения/выключения трактов.
        if (ui && ui->progressBar) {
            ui->progressBar->setTextVisible(false);
            ui->progressBar->setRange(0, 0);
            ui->progressBar->setValue(0);
            ui->progressBar->setVisible(true);
        }
        if (ui && ui->framePPM) {
            ui->framePPM->setVisible(false);
        }

        m_profileIntegrityStage = ProfileIntegrityStage::Checking;
        onDeviceLogMessage("Станция снова подключена. Контроль целостности профиля: архивирование и сравнение md5...");

        const QString stationIp = m_profileIntegrityStationIp.trimmed();
        QtConcurrent::run([this, stationIp]() {
            QString err;
            const bool ok = verifyProfileIntegrityAfterRebootOverSsh(stationIp, &err);
            QMetaObject::invokeMethod(this, [this, ok, err]() {
                if (ok) {
                    onDeviceLogMessage("Контроль целостности профиля: OK (md5 совпадает).");
                    startPpmInitAfterIntegrityOk();
                } else {
                    onDeviceLogMessage(QString("ОШИБКА контроля целостности профиля: %1")
                                           .arg(err.isEmpty() ? QStringLiteral("неизвестная ошибка") : err));
                    // Если контроль целостности не прошёл — не трогаем тракты и возвращаем UI в обычное состояние.
                    if (ui && ui->progressBar) {
                        ui->progressBar->setRange(0, 100);
                        ui->progressBar->setValue(0);
                        ui->progressBar->setVisible(false);
                    }
                }
                m_profileIntegrityStage = ProfileIntegrityStage::None;
                m_profileIntegrityStationIp.clear();
            }, Qt::QueuedConnection);
        });
    }

    updateTabWidgetLockState();
}

void MainWindow::onDeviceDisconnected() {
    m_ppmCurrDirSetByCheckStationTract = -1;
    ++m_resumeAfterExternalWorkModeSerial;
    m_testsPausedForExternalWorkMode = false;
    m_externalWorkModePauseTract = -1;
    m_powerTestAutoPausedByExternalWorkMode = false;
    m_receiveTestAutoPausedByExternalWorkMode = false;

    if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
        m_powerTrafficGenerator->stop();
    }
    m_powerTestAutoStopTimer.stop();
    m_powerTestStepPauseTimer.stop();
    m_powerTestBeforePowerOnTimer.stop();
    m_powerMeasurementRunning = false;
    m_powerTrafficStartPending = false;
    m_powerTestSequenceIndex = -1;
    m_powerTestSequenceFreqsHz.clear();
    m_powerStepAmpAccumDbm = 0.0;
    m_powerStepAmpSampleCount = 0;
    m_powerStepBestValid = false;
    m_powerStepBestFreqMHz = 0.0;
    m_powerStepBestAmpDbm = -200.0;
    m_powerTestCurrentFreqHz = 0;
    if (ui && ui->plotWidgetMomentSpetrumGraph) {
        ui->plotWidgetMomentSpetrumGraph->xAxis->setLabel(QString());
        ui->plotWidgetMomentSpetrumGraph->replot(QCustomPlot::rpQueuedReplot);
    }
    setEmissionAnimating(false);
    if (ui && ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked()) {
        QSignalBlocker blocker(ui->pushButtonStartTestingPower);
        ui->pushButtonStartTestingPower->setChecked(false);
    }

    setStationDisconnectedUi();
    ui->frameStation->setVisible(true);
    onDeviceLogMessage("Соединение со станцией разорвано.");

    // По ТЗ: до подготовки профиля кнопку старта держим заблокированной.
    setStartTestingButtonEnabled(false);
    updateTabWidgetLockState();
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
    m_analyzerConnected = true;
    setAnalyzerConnectedUi();
    onDeviceLogMessage("Успешное подключение к анализатору.");

    // Защита от рассинхронизации флага после reconnect:
    // при подключении заново проверяем, открыта ли tabHands сейчас.
    bool isHands = false;
    if (m_tabHandsIndex >= 0 && ui->tabWidget) {
        isHands = (ui->tabWidget->currentIndex() == m_tabHandsIndex);
    }
    m_startSpectrumOnHands = m_startSpectrumOnHands || isHands;

    // Если пользователь на tabHands — запускаем стрим.
    if (m_startSpectrumOnHands) {
        startSpectrumStream();
    }
}

void MainWindow::onAnalyzerDisconnected(const QString &reason)
{
    m_analyzerConnected = false;
    stopSpectrumStream();
    setAnalyzerDisconnectedUi();
    onDeviceLogMessage(QString("Анализатор отключен: %1").arg(reason));
}

void MainWindow::onAnalyzerLogMessage(const QString &msg)
{
    onDeviceLogMessage(msg);
}

void MainWindow::setStationConnectedUi() {
    ui->frameStation->setStyleSheet(styleSheetConnectStation);
    ui->labelPixStation->setPixmap(QPixmap(":/led_green.png"));
    ui->labelStateStation->setText("Подключена");
    ui->labelStateStation->setStyleSheet("color: #8AE08A;");
}

void MainWindow::setStationDisconnectedUi() {
    if (ui && ui->frameStation) {
        ui->frameStation->setVisible(true);
    }
    ui->frameStation->setStyleSheet(styleSheetDisconnectStation);
    ui->labelPixStation->setPixmap(QPixmap(":/led_red.png"));
    ui->labelStateStation->setText("Отключена");
    ui->labelStateStation->setStyleSheet("color: #ff5252;");
    resetPowerReadoutUi();
    m_ppmLastWorkModeByTract.clear();
    m_powerLevelCodeByTract.clear();
    m_ppmModeLaunchPendingByTract.clear();
    m_ppmModeLaunchTimedOutByTract.clear();
    m_ppmModeLaunchSinceMsByTract.clear();
    m_ppmExternalDirRecoveryTract = -1;
    setPpmUpdateLabelVisible(false);
    const int sel = selectedPpmTractFromUi();
    if (sel > 0) {
        refreshPpmStatusUiForTract(sel);
    } else {
        applyPpmTransmitterLabel(QStringLiteral("—"), PpmStatusStyle::Fault);
        applyPpmModeFrameIdle();
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
    ui->labelStateR3->setText("Подключен");
    ui->labelStateR3->setStyleSheet("color: #8AE08A;");
}

void MainWindow::setAnalyzerDisconnectedUi()
{
    if (ui && ui->frameR3) {
        ui->frameR3->setVisible(true);
    }
    m_powerTestAutoStopTimer.stop();
    m_powerTestStepPauseTimer.stop();
    m_powerTestBeforePowerOnTimer.stop();
    if (ui->pushButtonStartTestingPower) {
        if (ui->pushButtonStartTestingPower->isChecked()) {
            ui->pushButtonStartTestingPower->setChecked(false);
        }
    }
    if (ui->plotWidgetMomentSpetrumGraph) {
        ui->plotWidgetMomentSpetrumGraph->xAxis->setLabel(QString());
        ui->plotWidgetMomentSpetrumGraph->replot(QCustomPlot::rpQueuedReplot);
    }
    setEmissionAnimating(false);
    ui->frameR3->setStyleSheet(styleSheetDisconnectAnalyzer);
    ui->labelPixR3->setPixmap(QPixmap(":/led_red.png"));
    ui->labelStateR3->setText("Отключен");
    ui->labelStateR3->setStyleSheet("color: #ff5252;");
}

void MainWindow::setTestingUiBusy(bool busy)
{
    if (ui->progressBar) {
        if (busy) {
            ui->progressBar->setRange(0, 0);
            ui->progressBar->setValue(0);
            ui->progressBar->setVisible(true);
        } else {
            ui->progressBar->setRange(0, 100);
            ui->progressBar->setValue(0);
            ui->progressBar->setVisible(false);
        }
    }
}

void MainWindow::setStartTestingButtonEnabled(bool enabled)
{
    if (ui && ui->pushButtonStartTesting) {
        ui->pushButtonStartTesting->setEnabled(enabled);
    }
}

void MainWindow::startProfileIntegritySequenceAfterReboot(const QString &stationIp)
{
    const QString ip = stationIp.trimmed();
    if (ip.isEmpty()) {
        return;
    }

    m_profileIntegrityStationIp = ip;
    m_profileIntegrityStage = ProfileIntegrityStage::WaitingAfterReboot;

    onDeviceLogMessage("Контроль целостности профиля: ожидание перезагрузки станции 40 секунд...");

    // UI: 40 секунд показываем прогресс 0..100%, затем переключимся в бесконечный режим.
    if (ui && ui->progressBar) {
        ui->progressBar->setTextVisible(true);
        ui->progressBar->setFormat(QStringLiteral("%p%"));
        ui->progressBar->setRange(0, 100);
        ui->progressBar->setValue(0);
        ui->progressBar->setVisible(true);
    }
    m_postRebootWaitElapsed.restart();
    m_postRebootWaitProgressTimer.start();

    // Контроллер UDP: станция уходит в reboot, переводим соединение в "отключено"
    // и после ожидания начнём периодически слать MOD_START.
    if (m_deviceController && m_deviceController->isConnected()) {
        m_deviceController->disconnectFromDevice();
    }

    m_postRebootReconnectTimer.stop();
    m_postRebootWaitTimer.start(40000);
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

    onDeviceLogMessage("Контроль целостности профиля: ожидание завершено, начинаем переподключение (MOD_START)...");
    m_profileIntegrityStage = ProfileIntegrityStage::Reconnecting;

    // UI: ожидание завершено — переходим в бесконечный режим, пока станция не подключится.
    m_postRebootWaitProgressTimer.stop();
    if (ui && ui->progressBar) {
        ui->progressBar->setTextVisible(false);
        ui->progressBar->setRange(0, 0);
        ui->progressBar->setValue(0);
        ui->progressBar->setVisible(true);
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

    static const qint64 kTotalMs = 40000;
    const qint64 elapsed = m_postRebootWaitElapsed.isValid() ? m_postRebootWaitElapsed.elapsed() : 0;
    const int percent = qBound(0, static_cast<int>((elapsed * 100) / kTotalMs), 100);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(percent);
    ui->progressBar->setVisible(true);
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

bool MainWindow::verifyProfileIntegrityAfterRebootOverSsh(const QString &stationIp, QString *errorText)
{
    SSHer ssher;
    ssher.setAllowLegacyAlgorithms(true);

    auto logAsync = [&](const QString &msg) {
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

namespace {
constexpr qint64 kPpmModeLaunchTimeoutMs = 30000;
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
    if (ppm_on && lastCode != code && code == ERRCODE_NOERROR) {
        const int gateFrame = m_ppmFrameStateByTract.value(tr, TRAKT_STOP_WRK);
        if (gateFrame != TRAKT_START_OFF && lastWasStartOrUnset) {
            setPpmFrameStateForTract(tr, TRAKT_WRK);
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
    if (!ui) {
        return;
    }
    if (ui->labelUpdate) {
        ui->labelUpdate->setVisible(visible);
    }
    if (ui->labelRecieveUpdate) {
        ui->labelRecieveUpdate->setVisible(visible);
    }
}

void MainWindow::onPpmUpdateClicked()
{
    const int tractNum = selectedPpmTractFromUi();
    const bool restarted = restartPpmModeForTract(tractNum);
    if (restarted) {
        setPpmUpdateLabelVisible(false);
    }
}

bool MainWindow::restartPpmModeForTract(int tractNum)
{
    if (!m_deviceController || !m_deviceController->isConnected()) {
        onDeviceLogMessage(QStringLiteral("ППМ: нет подключения к станции, перезапуск режима невозможен."));
        return false;
    }
    if (tractNum <= 0) {
        onDeviceLogMessage(QStringLiteral("ППМ: не выбран тракт для перезапуска режима."));
        return false;
    }
    // Если режим уже в состоянии "запускается" (мы недавно инициировали запуск) —
    // не шлём повторно CMD_CURR_DIR_SET, чтобы не загнать станцию/защиту в петлю.
    if (m_ppmModeLaunchPendingByTract.value(tractNum, false)) {
        return false;
    }

    // Аналог Station_starter_3 pushButtonReset: отправляем CMD_CURR_DIR_SET (0x0501) с DirId=1.
    constexpr uint8_t dirId = 1;
    onDeviceLogMessage(QStringLiteral("ППМ: перезапуск режима тракта %1 (DirId=%2).").arg(tractNum).arg(static_cast<int>(dirId)));
    if (!m_deviceController->setCurrentDirection(static_cast<uint8_t>(tractNum), dirId)) {
        onDeviceLogMessage(QStringLiteral("ППМ: не удалось отправить команду перезапуска режима."));
        return false;
    }
    markPpmModeLaunchStarted(tractNum);
    applyPpmModeFrameForTract(tractNum);
    m_deviceController->requestAllIndications(static_cast<uint8_t>(tractNum));
    return true;
}

bool MainWindow::isPowerTestRunningForExternalWorkModePause(int tractNum) const
{
    return m_powerTestTargetTract != 0U
        && tractNum == static_cast<int>(m_powerTestTargetTract)
        && ui && ui->pushButtonStartTestingPower
        && (ui->pushButtonStartTestingPower->isChecked() || m_powerTestPaused);
}

bool MainWindow::isReceiveTestRunningForExternalWorkModePause(int tractNum) const
{
    return m_receiveTestRunning && m_receiveTestTract == tractNum;
}

void MainWindow::maybePauseTestsForExternalWorkModeChange(int tractNum, uint16_t prevMode, uint16_t newMode)
{
    const int tr = tractNum;

    if (tr <= 0 || tr != m_ppmCurrentOnTract) {
        return;
    }
    if (m_ppmCurrDirSetByCheckStationTract == tr) {
        return;
    }
    // Внутренний сценарий: после переключения тракта/включения тракта режим часто
    // сначала падает в 0 ("запускается") — это не внешнее вмешательство.
    if (m_ppmModeLaunchPendingByTract.value(tr, false)) {
        return;
    }
    {
        const qint64 nowMs = m_uptime.isValid() ? m_uptime.elapsed() : 0;
        constexpr qint64 kIgnoreAfterTractSwitchMs = 2000;
        if (nowMs >= 0 && tr == m_ppmLastTractSwitchToTract && m_ppmLastTractSwitchFinishedAtMs >= 0 &&
            (nowMs - m_ppmLastTractSwitchFinishedAtMs) < kIgnoreAfterTractSwitchMs) {
            return;
        }
    }
    if (m_testsPausedForExternalWorkMode && m_externalWorkModePauseTract == tr) {
        return;
    }

    if (prevMode == 0) {
        return;
    }

    const bool modeDroppedToZero = (newMode == 0 && prevMode != 0);
    const bool modeChangedToOtherNonZero = (newMode != 0 && newMode != prevMode);
    if (!modeDroppedToZero && !modeChangedToOtherNonZero) {
        return;
    }

    pauseTestsForExternalWorkModeAndRestartPpm(tr);
}

void MainWindow::pauseTestsForExternalWorkModeAndRestartPpm(int tractNum)
{
    if (tractNum <= 0) {
        return;
    }

    // Антипетля: если уже инициировали запуск режима (ждём ненулевой IND_WORKMODE) —
    // не перезапускаем снова по каждому чиху IND_WORKMODE.
    if (m_ppmModeLaunchPendingByTract.value(tractNum, false)) {
        return;
    }
    // Антипетля #2: rate-limit на перезапуск "как Обновить" от защиты.
    {
        const qint64 nowMs = m_uptime.isValid() ? m_uptime.elapsed() : 0;
        constexpr qint64 kRestartCooldownMs = 7000;
        const qint64 last = m_ppmLastExternalWorkModeRestartAtMsByTract.value(tractNum, -1000000);
        if (nowMs >= 0 && (nowMs - last) < kRestartCooldownMs) {
            return;
        }
        m_ppmLastExternalWorkModeRestartAtMsByTract.insert(tractNum, nowMs);
    }

    const bool powerActive = isPowerTestRunningForExternalWorkModePause(tractNum);
    const bool receiveActive = isReceiveTestRunningForExternalWorkModePause(tractNum);

    // Внешнее переключение режима по протоколу недопустимо и без теста: только перезапуск позиции в ППМ.
    // Пауза теста/счётчики отложенного resume — только если тест мощности или приёма реально идёт.
    if (!powerActive && !receiveActive) {
        onDeviceLogMessage(
            QStringLiteral("ППМ: обнаружено внешнее переключение режима (тракт %1) — перезапуск режима (как «Обновить»).")
                .arg(tractNum));
        const bool restarted = restartPpmModeForTract(tractNum);
        if (restarted) {
            setPpmUpdateLabelVisible(false);
        }
        updateTabWidgetLockState();
        return;
    }

    ++m_powerResumeAfterPpmSerial;
    ++m_resumeAfterExternalWorkModeSerial;

    m_testsPausedForExternalWorkMode = true;
    m_externalWorkModePauseTract = tractNum;

    onDeviceLogMessage(
        QStringLiteral("ППМ: обнаружено внешнее переключение режима (тракт %1) — пауза теста и перезапуск режима (как «Обновить»).")
            .arg(tractNum));

    if (powerActive) {
        m_powerTestAutoPausedByExternalWorkMode = true;
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

    if (receiveActive) {
        m_receiveTestAutoPausedByExternalWorkMode = true;
        if (!m_receiveTestPaused) {
            m_receiveTestPaused = true;
            m_receiveTestTickTimer.stop();
            if (m_analyzerController) {
                m_analyzerController->setGenerator(m_receiveTestFreqHz, /*state*/ 0, m_receiveTestPow);
            }
            setEmissionAnimating(false);
            setReceiveTestControlsRunning(true);
            updateReceiveResultStripsVisibility();
        }
    }

    const bool restarted = restartPpmModeForTract(tractNum);
    if (restarted) {
        setPpmUpdateLabelVisible(false);
    }

    updateTabWidgetLockState();
}

void MainWindow::tryResumeTestsAfterExternalWorkModeRecovery(int tractNum)
{
    if (tractNum <= 0) {
        return;
    }
    if (!m_testsPausedForExternalWorkMode || m_externalWorkModePauseTract != tractNum) {
        return;
    }
    if (!isPpmTractReadyForPowerTest(tractNum)) {
        return;
    }

    if (m_powerTestAutoPausedByExternalWorkMode) {
        const bool canResumeSequence =
            (m_powerTestSequenceIndex >= 0 && m_powerTestSequenceIndex < m_powerTestSequenceFreqsHz.size()
             && !m_powerTestSequenceFreqsHz.isEmpty());

        if (m_powerTestPaused && canResumeSequence && m_powerTestTargetTract != 0U
            && tractNum == static_cast<int>(m_powerTestTargetTract) && ui && ui->pushButtonStartTestingPower
            && !ui->pushButtonStartTestingPower->isChecked()) {
            constexpr int kResumeDelayMs = 5000;
            const quint64 serial = ++m_powerResumeAfterPpmSerial;
            const quint64 wmSerial = ++m_resumeAfterExternalWorkModeSerial;

            setPowerTestControlsRunning(true);

            QTimer::singleShot(kResumeDelayMs, this, [this, serial, wmSerial, tractNum]() {
                if (serial != m_powerResumeAfterPpmSerial || wmSerial != m_resumeAfterExternalWorkModeSerial) {
                    return;
                }
                if (!m_testsPausedForExternalWorkMode || m_externalWorkModePauseTract != tractNum) {
                    return;
                }
                if (m_powerTestTargetTract == 0U || tractNum != static_cast<int>(m_powerTestTargetTract)) {
                    return;
                }
                if (!m_powerTestPaused) {
                    return;
                }
                if (!isPpmTractReadyForPowerTest(tractNum)) {
                    updatePowerTestButtonsAccessForSelectedTract();
                    return;
                }
                if (selectedPpmTractFromUi() != tractNum) {
                    return;
                }
                if (!ui || !ui->tabWidget || m_tabPowerIndex < 0 || ui->tabWidget->currentIndex() != m_tabPowerIndex) {
                    return;
                }
                if (m_powerTestBlockedByPpm || m_powerTestBlockedByAntFault || m_powerTestBlockedByDirRestore) {
                    return;
                }
                if (m_powerTestSequenceFreqsHz.isEmpty() || m_powerTestSequenceIndex < 0
                    || m_powerTestSequenceIndex >= m_powerTestSequenceFreqsHz.size()) {
                    return;
                }
                if (!ui->pushButtonStartTestingPower || ui->pushButtonStartTestingPower->isChecked()) {
                    return;
                }

                m_testsPausedForExternalWorkMode = false;
                m_externalWorkModePauseTract = -1;
                m_powerTestAutoPausedByExternalWorkMode = false;

                setPowerTestControlsRunning(false);
                ui->pushButtonStartTestingPower->setChecked(true);
            });
            return;
        }
        return;
    }

    if (m_receiveTestAutoPausedByExternalWorkMode) {
        // Как у теста мощности: не снимать m_testsPausedForExternalWorkMode сразу. Иначе при «дребезге»
        // IND_WORKMODE после перезапуска ППМ maybePauseTests снова вызывает паузу → бесконечное
        // переключение play/pause и повторный restartPpm.
        constexpr int kResumeDelayMs = 5000;
        const quint64 wmSerial = ++m_resumeAfterExternalWorkModeSerial;

        setReceiveTestControlsRunning(true);

        QTimer::singleShot(kResumeDelayMs, this, [this, wmSerial, tractNum]() {
            if (wmSerial != m_resumeAfterExternalWorkModeSerial) {
                return;
            }
            if (!m_testsPausedForExternalWorkMode || m_externalWorkModePauseTract != tractNum) {
                return;
            }
            if (!m_receiveTestAutoPausedByExternalWorkMode) {
                return;
            }
            if (!m_receiveTestRunning || m_receiveTestTract != tractNum) {
                return;
            }
            if (!m_receiveTestPaused) {
                return;
            }
            if (!isPpmTractReadyForPowerTest(tractNum)) {
                updateReceiveTestButtonsAccessForSelectedTract();
                return;
            }
            if (selectedPpmTractFromUi() != tractNum) {
                return;
            }
            if (!ui || !ui->tabWidget || m_tabReceiveIndex < 0
                || ui->tabWidget->currentIndex() != m_tabReceiveIndex) {
                return;
            }

            m_testsPausedForExternalWorkMode = false;
            m_externalWorkModePauseTract = -1;
            m_receiveTestAutoPausedByExternalWorkMode = false;
            m_receiveTestPaused = false;
            setReceiveTestControlsRunning(false);

            if (m_receivePhase == ReceiveTestPhase::RunningLevel) {
                m_receiveTestTickTimer.start();
                if (m_analyzerController) {
                    m_analyzerController->setGenerator(m_receiveTestFreqHz, /*state*/ 1, m_receiveTestPow);
                }
                onReceiveTestTick();
            }
            updateReceiveResultStripsVisibility();
            onDeviceLogMessage(QStringLiteral("▶ Тест приёма продолжен после восстановления режима (тракт %1).").arg(tractNum));
            updateTabWidgetLockState();
        });
    }
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
    if (m_ppmFrameStateByTract.value(tractNum, TRAKT_STOP_WRK) != TRAKT_WRK) {
        return;
    }

    onDeviceLogMessage(QStringLiteral("ППМ: направление тракта %1 загружено (TRAKT_WRK), возвращаю DirId=1.")
                           .arg(tractNum));
    if (m_deviceController->setCurrentDirection(static_cast<uint8_t>(tractNum), 1)) {
        m_ppmRestoreDefaultDirInFlightByTract.insert(tractNum, true);
    } else {
        onDeviceLogMessage(QStringLiteral("ППМ: не удалось отправить команду возврата направления DirId=1 (тракт %1).")
                               .arg(tractNum));
    }
}

void MainWindow::refreshPpmStatusUiForTract(int tractNum)
{
    constexpr int ERRCODE_NOERROR = 0;
    constexpr int ERRCODE_PPM_NOANSWER = 1;
    constexpr int ERRCODE_PPM_LUM_OVERHEAT = 4;
    constexpr int ERRCODE_PPM_START = 10;
    constexpr int16_t ERRCODE_PPM_START_LEGACY = static_cast<int16_t>(0xFFFF);

    if (tractNum <= 0 || !m_ppmLastStatusCodeByTract.contains(tractNum)) {
        applyPpmTransmitterLabel(QStringLiteral("—"), PpmStatusStyle::Fault);
        setPpmUpdateLabelVisible(false);
        applyPpmModeFrameForTract(tractNum);
        updatePowerTestButtonsAccessForSelectedTract();
        updateReceiveTestButtonsAccessForSelectedTract();
        return;
    }

    const int16_t code = m_ppmLastStatusCodeByTract.value(tractNum);
    const QString text = ppmErrorCodeToText(code);
    if (text.isEmpty()) {
        applyPpmTransmitterLabel(QStringLiteral("—"), PpmStatusStyle::Fault);
        setPpmUpdateLabelVisible(false);
        applyPpmModeFrameForTract(tractNum);
        updatePowerTestButtonsAccessForSelectedTract();
        updateReceiveTestButtonsAccessForSelectedTract();
        return;
    }

    setPpmUpdateLabelVisible(code == ERRCODE_PPM_NOANSWER);

    if (code == ERRCODE_NOERROR) {
        applyPpmTransmitterLabel(text, PpmStatusStyle::Ok);
    } else if (code == ERRCODE_PPM_LUM_OVERHEAT || code == ERRCODE_PPM_START || code == ERRCODE_PPM_START_LEGACY) {
        applyPpmTransmitterLabel(text, PpmStatusStyle::Warning);
    } else {
        applyPpmTransmitterLabel(text, PpmStatusStyle::Fault);
    }
    applyPpmModeFrameForTract(tractNum);
    updatePowerTestButtonsAccessForSelectedTract();
    updateReceiveTestButtonsAccessForSelectedTract();
}

void MainWindow::pausePowerTestForPpmDisconnect()
{
    // Отменяем любой ранее запланированный auto-resume после "Норма".
    ++m_powerResumeAfterPpmSerial;

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
        onDeviceLogMessage(QStringLiteral("⏹ ППМ: Нет связи с ПП — остановка RTP/генератора трафика."));
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

void MainWindow::pausePowerTestForAntennaFault()
{
    // Отменяем любой ранее запланированный auto-resume после "Норма".
    ++m_powerResumeAfterPpmSerial;

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
        onDeviceLogMessage(QStringLiteral("⏹ ППМ: Авария АНТ — остановка RTP/генератора трафика."));
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

void MainWindow::pausePowerTestForDirectionRestore()
{
    ++m_powerResumeAfterPpmSerial;

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
        onDeviceLogMessage(
            QStringLiteral("⏹ ППМ: внешнее переключение направления — остановка RTP/генератора трафика."));
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

    onDeviceLogMessage(QStringLiteral("⏸ ППМ: тест мощности на паузе (внешняя смена направления)."));
    setPowerTestControlsRunning(true);
}

void MainWindow::syncPpmFrameForDir1IfTransmitterOk(int tractNum, bool requireNonZeroWorkMode)
{
    if (tractNum <= 0 || tractNum != m_ppmCurrentOnTract) {
        return;
    }
    if (m_ppmLastDirIdByTract.value(tractNum, 1) != 1) {
        return;
    }
    if (requireNonZeroWorkMode && m_ppmLastWorkModeByTract.value(tractNum, 0) == 0) {
        return;
    }
    if (!m_ppmLastStatusCodeByTract.contains(tractNum)) {
        return;
    }
    const int16_t c = m_ppmLastStatusCodeByTract.value(tractNum);
    constexpr int16_t ERRCODE_NOERROR = 0;
    constexpr int16_t ERRCODE_PPM_LUM_OVERHEAT = 4;
    // Как в пульте при ppm_on: при этих кодах рамка остаётся/становится рабочей (зелёной).
    if (c == ERRCODE_NOERROR || c == ERRCODE_PPM_LUM_OVERHEAT) {
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
            || !m_powerTestPaused) {
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
    const bool allow = isPpmTractReadyForPowerTest(selected);

    if (ui->pushButtonStartTestingPower) {
        ui->pushButtonStartTestingPower->setEnabled(allow);
    }
    if (ui->pushButtonPowerTestPause) {
        ui->pushButtonPowerTestPause->setEnabled(allow);
    }
    if (ui->pushButtonPowerTestStop) {
        ui->pushButtonPowerTestStop->setEnabled(allow);
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
    const bool allow = isPpmTractReadyForPowerTest(selected);

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

    // Если тракт снова готов — при авто-паузе автоматически продолжаем.
    if (isPpmTractReadyForPowerTest(tractNum)) {
        updateReceiveTestButtonsAccessForSelectedTract();

        if (m_receiveTestPaused && m_receiveTestAutoPausedByPpmNotReady) {
            m_receiveTestPaused = false;
            m_receiveTestAutoPausedByPpmNotReady = false;
            setReceiveTestControlsRunning(false); // иконка pause

            if (m_receivePhase == ReceiveTestPhase::RunningLevel) {
                m_receiveTestTickTimer.start();
                if (m_analyzerController) {
                    m_analyzerController->setGenerator(m_receiveTestFreqHz, /*state*/ 1, m_receiveTestPow);
                }
                onReceiveTestTick();
            }
            updateReceiveResultStripsVisibility();
            onDeviceLogMessage(QStringLiteral("▶ Тест приёма продолжен: тракт %1 снова готов.").arg(tractNum));
        }
        updateTabWidgetLockState();
        return;
    }

    // Неготов: переводим тест в paused (как по кнопке паузы), НЕ обнуляя прогресс/результаты.
    if (!m_receiveTestPaused) {
        m_receiveTestPaused = true;
        m_receiveTestAutoPausedByPpmNotReady = true;
        m_receiveTestTickTimer.stop();
        if (m_analyzerController) {
            // Вне зависимости от фазы безопасно выключаем генератор.
            m_analyzerController->setGenerator(m_receiveTestFreqHz, /*state*/ 0, m_receiveTestPow);
        }
        setEmissionAnimating(false);
        setReceiveTestControlsRunning(true); // иконка play
        updateReceiveResultStripsVisibility();
        onDeviceLogMessage(QStringLiteral("⏸ Тест приёма на паузе: тракт %1 не готов (статус/режим).").arg(tractNum));
    }

    updateReceiveTestButtonsAccessForSelectedTract();
    updateTabWidgetLockState();
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
    double xLo = 30.0;
    double xHi = 180.0;
    switch (trmType) {
    case 2:
        xLo = 30.0;
        xHi = 180.0;
        break;
    case 3:
        xLo = 220.0;
        xHi = 470.0;
        break;
    case 4:
        xLo = 520.0;
        xHi = 2500.0;
        break;
    default:
        // Если тип неизвестен — оставляем дефолт (type2).
        break;
    }
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
    constexpr int ERRCODE_NOERROR = 0;
    constexpr int ERRCODE_PPM_NOANSWER = 1; // "Нет связи с ПП"
    constexpr int ERRCODE_PPM_LUM_OVERHEAT = 4;
    constexpr int ERRCODE_PPM_SWR_ERROR = 5; // "Авария АНТ" (антенная авария / SWR)
    constexpr int ERRCODE_PPM_START = 10;
    constexpr int16_t ERRCODE_PPM_START_LEGACY = static_cast<int16_t>(0xFFFF);

    const bool isOnPowerTab =
        (ui && ui->tabWidget && m_tabPowerIndex >= 0 && ui->tabWidget->currentIndex() == m_tabPowerIndex);
    // По уточнению: логика "Авария АНТ" применяется только на tabPower.
    // Если такой статус пришел на других вкладках (например, tabRecieve) — полностью игнорируем.
    if (code == ERRCODE_PPM_SWR_ERROR && !isOnPowerTab) {
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

    // "Авария антенны" устраняется только при выходе на мощность:
    // - тест мощности ставим на паузу (без сброса прогресса)
    // - отдельно запускаем "пульсер" трафика: 1 сек каждые 5 сек
    if (isAntennaFault) {
        m_antFaultPulseActive = true;
        m_antFaultPulseTract = tr;
        if (m_antFaultPulseTimer) {
            QMetaObject::invokeMethod(m_antFaultPulseTimer, [t = m_antFaultPulseTimer]() {
                if (!t->isActive()) {
                    t->start();
                }
            }, Qt::QueuedConnection);
        }
        QTimer::singleShot(0, this, &MainWindow::onAntennaFaultPulseTick);
    } else if (m_antFaultPulseTract == tr && (m_antFaultPulseActive || m_antFaultPulseTrafficActive)) {
        // Выход из "Авария АНТ": обязательно останавливаем pulse-циклы и,
        // если в этот момент шёл 1-секундный импульс, гасим его сразу.
        m_antFaultPulseActive = false;
        m_antFaultPulseTrafficActive = false;
        const bool hadPulseTraffic = (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning());
        m_antFaultPulseTract = -1;
        ++m_antFaultPulseSerial;
        if (m_antFaultPulseTimer) {
            QMetaObject::invokeMethod(m_antFaultPulseTimer, [t = m_antFaultPulseTimer]() { t->stop(); },
                                      Qt::QueuedConnection);
        }
        if (hadPulseTraffic && m_powerTrafficGenerator) {
            m_powerTrafficGenerator->stop();
        }
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

    // Если во время теста приёма тракт стал неготов (ошибка/не тот статус) — останавливаем тест.
    stopReceiveTestIfTractNotReady(tr);

    const int selected = selectedPpmTractFromUi();
    if (selected <= 0 || selected != tr) {
        updateTabWidgetLockState();
        return;
    }

    refreshPpmStatusUiForTract(tr);
    updateTabWidgetLockState();
}

void MainWindow::onAntennaFaultPulseTick()
{
    if (!m_antFaultPulseActive || m_antFaultPulseTract <= 0) {
        return;
    }
    if (!m_deviceController || !m_deviceController->isConnected() || !m_powerTrafficGenerator) {
        return;
    }
    // Не вмешиваемся, если тест мощности сейчас активен (не на паузе) и управляет трафиком/излучением.
    // Если тест переведён на паузу из-за "Авария АНТ", пульсер должен продолжать работать.
    if ((ui && ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked())
        || m_powerMeasurementRunning || m_powerTrafficStartPending) {
        return;
    }
    // Запускать "подкачку" имеет смысл только когда мы действительно ждём восстановления после аварии АНТ.
    if (!m_powerTestBlockedByAntFault && !(m_antFaultPulseTract > 0)) {
        return;
    }
    // Если уже идёт пульс (окно 1 сек) — не стартуем второй.
    if (m_antFaultPulseTrafficActive || (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning())) {
        return;
    }
    // Требование: пульсовать на последней установленной частоте (частоту не меняем, но проверим что она известна).
    const quint64 lastHz = m_lastTxFreqHzByTract.value(m_antFaultPulseTract, 0);
    if (lastHz == 0) {
        return;
    }
    // Тракт/TrmType определяет multicast-адрес, как в power-тесте.
    QString multicastAddress = QString::fromLatin1(TRAFFIC_MCAST_IP);
    const int trmType = m_ppmTrmTypeByTract.value(m_antFaultPulseTract, -1);
    switch (trmType) {
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
        break;
    }

    m_powerTrafficGenerator->setBindIp(m_deviceController->config().selfIp);
    m_powerTrafficGenerator->setMulticastAddress(multicastAddress);
    m_powerTrafficGenerator->setMulticastPort(TRAFFIC_DST_PORT);
    m_powerTrafficGenerator->setSourcePort(TRAFFIC_SRC_PORT);
    m_powerTrafficGenerator->setDscp(DSCP_STREAMVOICE);
    m_powerTrafficGenerator->setEcn(ECN_DEFAULT);
    m_powerTrafficGenerator->setPayloadType(RTP_PAYLOAD_TYPE);
    m_powerTrafficGenerator->setTractNumber(static_cast<uint8_t>(m_antFaultPulseTract));

    if (!m_powerTrafficGenerator->start()) {
        return;
    }

    m_antFaultPulseTrafficActive = true;
    const quint64 serial = ++m_antFaultPulseSerial;
    QTimer::singleShot(1000, this, [this, serial]() {
        if (serial != m_antFaultPulseSerial) {
            return;
        }
        if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning() && m_antFaultPulseTrafficActive) {
            m_powerTrafficGenerator->stop();
        }
        m_antFaultPulseTrafficActive = false;
    });
}

void MainWindow::onActiveDirectionIndicationReceived(uint8_t tractNum, uint8_t dirId)
{
    const int tr = static_cast<int>(tractNum);
    if (tr <= 0) {
        return;
    }
    m_ppmLastDirIdByTract.insert(tr, dirId);

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
        return;
    }
    if (tr != m_ppmCurrentOnTract) {
        return;
    }

    onDeviceLogMessage(QStringLiteral("ППМ: обнаружено внешнее переключение направления (тракт %1, DirId=%2).")
                           .arg(tr)
                           .arg(static_cast<int>(dirId)));
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

void MainWindow::onWorkModeIndicationReceived(uint8_t tractNum, uint16_t mode)
{
    const int tr = static_cast<int>(tractNum);
    m_ppmLastWorkModeByTract.insert(tr, mode);
    if (mode != 0) {
        clearPpmModeLaunchStateForTract(tr);
        // После перезагрузки режима тракт может остаться в TRAKT_END_ON до следующего IND_ERROR/ACTIVEDIR.
        syncPpmFrameForDir1IfTransmitterOk(tr, true);
    }

    const int selected = selectedPpmTractFromUi();
    if (selected <= 0 || selected != tr) {
        stopReceiveTestIfTractNotReady(tr);
        return;
    }

    applyPpmModeFrameForTract(tr);
    stopReceiveTestIfTractNotReady(tr);
}

void MainWindow::initPpmUiStyle()
{
    if (!ui->framePPM) {
        return;
    }
    ui->framePPM->setStyleSheet(styleSheetFramePpm);
    if (ui->radioPPM1) {
        ui->radioPPM1->setStyleSheet(styleSheetPpmRadioOFF);
    }
    if (ui->radioPPM2) {
        ui->radioPPM2->setStyleSheet(styleSheetPpmRadioOFF);
    }
    ui->framePPM->setVisible(false);

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
        ui->labelUpdate->setVisible(false);
    }
    if (ui->labelRecieveUpdate) {
        ui->labelRecieveUpdate->setCursor(Qt::PointingHandCursor);
        connect(ui->labelRecieveUpdate, &QPushButton::clicked, this, &MainWindow::onPpmUpdateClicked);
        ui->labelRecieveUpdate->setVisible(false);
    }

    // Инициализация: подпись — до IND_ERROR; рамка — до IND_WORKMODE.
    applyPpmTransmitterLabel(QStringLiteral("—"), PpmStatusStyle::Fault);
    applyPpmModeFrameIdle();
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
    ui->radioPPM1->setStyleSheet(styleSheetPpmRadioOFF);
    ui->radioPPM2->setStyleSheet(styleSheetPpmRadioOFF);

    for (int i = 2; i < radioCount; ++i) {
        QRadioButton *rb = new QRadioButton(ui->framePPM);
        rb->setFont(ui->radioPPM1->font());
        rb->setStyleSheet(styleSheetPpmRadioOFF);
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

void MainWindow::onTractPowerIndicationReceived(uint8_t tractNum, bool isOn)
{
    const int tr = static_cast<int>(tractNum);
    if (tr <= 0) {
        return;
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
    const bool suppressExternalTractProtection = (m_ppmExternalDirRecoveryTract >= 0);

    // Логи "как в Station_starter_3": фиксируем внешнее включение/выключение тракта.
    // Это помогает отличать переключение режима от переключения тракта по последовательности событий.
    if (!suppressExternalTractProtection && m_deviceController && m_deviceController->isConnected()
        && !m_deviceController->isAwaitingTractPowerAck()
        && m_ppmPowerStage == PpmPowerSequenceStage::None) {
        onDeviceLogMessage(QStringLiteral("ППМ: обнаружено внешнее %1 тракта: tr=%2 (подтверждено)")
                               .arg(isOn ? QStringLiteral("включение") : QStringLiteral("выключение"))
                               .arg(tr));
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

    onDeviceLogMessage(QStringLiteral("ППМ: получено внешнее выключение активного тракта %1, запускаю восстановление.")
                           .arg(tr));

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
    if (ui->progressBar) {
        ui->progressBar->setTextVisible(false);
        ui->progressBar->setRange(0, 0);
        ui->progressBar->setValue(0);
        ui->progressBar->setVisible(true);
    }
    if (ui->framePPM) {
        ui->framePPM->setVisible(false);
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
            ui->progressBar->setVisible(false);
        }
        if (ui->framePPM) {
            ui->framePPM->setVisible(true);
        }
        updateTabWidgetLockState();
    }
}

void MainWindow::onTractPowerAwaitingAck(uint8_t tractNum, bool enable)
{
    setPpmFrameStateForTract(static_cast<int>(tractNum), enable ? TRAKT_START_ON : TRAKT_START_OFF);
    setAllPpmRadiosEnabled(false);
    updateTabWidgetLockState();
    updatePowerTestButtonsAccessForSelectedTract();
}

void MainWindow::onTractPowerAcknowledged(uint8_t tractNum, bool isOn)
{
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
        if (ui && ui->progressBar) {
            ui->progressBar->setRange(0, 100);
            ui->progressBar->setValue(0);
            ui->progressBar->setVisible(false);
        }
        if (ui && ui->framePPM) {
            ui->framePPM->setVisible(true);
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
            ui->progressBar->setVisible(false);
        }
        if (ui && ui->framePPM) {
            ui->framePPM->setVisible(true);
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
    if (ui && ui->progressBar && ui->progressBar->minimum() == 0 && ui->progressBar->maximum() == 0) {
        ui->progressBar->setRange(0, 100);
        ui->progressBar->setValue(0);
        ui->progressBar->setVisible(false);
    }

    // На всякий случай: если зависли со скрытым PPM вне reboot-сценария — возвращаем управление.
    if (ui && ui->framePPM && !ui->framePPM->isVisible()) {
        ui->framePPM->setVisible(true);
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

    // При смене выбранного тракта в PPM — полностью сбрасываем вкладку/график мощности под новый тракт,
    // чтобы не оставалось "продолжить тест" от предыдущего тракта.
    resetPowerTestUiForNewTractSelection(targetTract);
    // Тест приёма: останавливаем/очищаем результаты и подставляем частоты под новый тракт.
    resetReceiveTestUiForNewTractSelection(targetTract);
    // LCD/прочие readout-ы возвращаем в исходное состояние.
    resetPowerReadoutUi();
    // По ТЗ: начальные значения tabHands зависят от выбранного тракта.
    applyHandsDefaultsForTract(targetTract);

    // Перерисовываем статус для выбранного тракта (если уже получали IND_ERROR).
    refreshPpmStatusUiForTract(targetTract);

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

void MainWindow::updateTabWidgetLockState()
{
    if (!ui || !ui->tabWidget) {
        return;
    }

    int handsTabIndex = m_tabHandsIndex;
    if (handsTabIndex < 0 || handsTabIndex >= ui->tabWidget->count()) {
        for (int i = 0; i < ui->tabWidget->count(); ++i) {
            QWidget *w = ui->tabWidget->widget(i);
            if (w && w->objectName() == QStringLiteral("tabHands")) {
                handsTabIndex = i;
                break;
            }
        }
    }

    if (handsTabIndex < 0) {
        // Если вкладка "tabHands" не найдена, не рискуем блокировать все вкладки.
        for (int i = 0; i < ui->tabWidget->count(); ++i) {
            ui->tabWidget->setTabEnabled(i, true);
        }
        return;
    }

    const bool powerTestRunningChecked =
        (ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked());
    // Пауза из-за тракта (ПП/АНТ/внешний режим): вкладки держим как во время теста, пока статус не разрулен.
    const bool powerTestPausedByTractHold =
        m_powerTestPaused
        && (m_powerTestBlockedByPpm || m_powerTestBlockedByAntFault || m_powerTestBlockedByDirRestore
            || m_powerTestAutoPausedByExternalWorkMode);
    const bool powerTestLocksToPowerTab = powerTestRunningChecked || powerTestPausedByTractHold;

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

        for (int i = 0; i < ui->tabWidget->count(); ++i) {
            ui->tabWidget->setTabEnabled(i, i == powerTabIndex);
        }
        if (powerTabIndex >= 0 && ui->tabWidget->currentIndex() != powerTabIndex) {
            ui->tabWidget->setCurrentIndex(powerTabIndex);
        }
        m_tabWidgetWasLocked = true;
        return;
    }

    // Тест приёма: на время работы (и при автопаузе тракта) — только tabRecieve, как у теста мощности.
    // Исключение: ручная пауза без автопаузы тракта — можно переключить вкладку.
    const bool receiveTestLocksToReceiveTab =
        m_receiveTestRunning
        && (!m_receiveTestPaused || m_receiveTestAutoPausedByPpmNotReady
            || m_receiveTestAutoPausedByExternalWorkMode);
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
            ui->tabWidget->setTabEnabled(i, true);
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

// Частоты теста приёма (tabRecieve) зависят от выбранного тракта ППМ (framePPM).
const QVector<quint64> kRxTestFrequenciesTract2Hz = {
    30025000ULL, 34025000ULL, 38525000ULL, 45525000ULL, 52225000ULL, 62525000ULL, 72525000ULL,
    85025000ULL, 95025000ULL, 118525000ULL, 137025000ULL, 157025000ULL, 179975000ULL
};
const QVector<quint64> kRxTestFrequenciesTract3Hz = {
    220025000ULL, 270025000ULL, 300025000ULL, 340025000ULL, 380025000ULL, 440025000ULL, 469975000ULL
};
const QVector<quint64> kRxTestFrequenciesTract4Hz = {
    520025000ULL, 630025000ULL, 720025000ULL, 847525000ULL, 965025000ULL,
    1117525000ULL, 1249975000ULL, 1850025000ULL, 2100025000ULL, 2499025000ULL
};

static QVector<quint64> receiveTestFrequenciesHzForTract(int tractNum)
{
    switch (tractNum) {
    case 3: return kRxTestFrequenciesTract3Hz;
    case 4: return kRxTestFrequenciesTract4Hz;
    case 2: return kRxTestFrequenciesTract2Hz;
    default: return kRxTestFrequenciesTract2Hz;
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
    W.levelLabels[0] = root->findChild<QLabel *>(QStringLiteral("labelRecieve225LvlM8"));
    W.levelLabels[1] = root->findChild<QLabel *>(QStringLiteral("labelRecieve225LvlM11"));
    W.levelLabels[2] = root->findChild<QLabel *>(QStringLiteral("labelRecieve225LvlM14"));
    W.levelLabels[3] = root->findChild<QLabel *>(QStringLiteral("labelRecieve225LvlM17"));
    W.levelLabels[4] = root->findChild<QLabel *>(QStringLiteral("labelRecieve225LvlM20"));
    W.levelLabels[5] = root->findChild<QLabel *>(QStringLiteral("labelRecieve225LvlM23"));
    W.levelLabels[6] = root->findChild<QLabel *>(QStringLiteral("labelRecieve225LvlM26"));
    W.levelLabels[7] = root->findChild<QLabel *>(QStringLiteral("labelRecieve225LvlM29"));
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
    p.setBrush(QColor(QStringLiteral("#0f172a")));
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
    p.setBrush(QColor(QStringLiteral("#0f172a")));
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
    p.setBrush(QColor(QStringLiteral("#0f172a")));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(5, 5, 12, 12, 2, 2);
    return QIcon(pm);
}

} // namespace

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
    // До старта теста показываем частоты по умолчанию (тракт №2).
    m_receiveTestFreqsHz = receiveTestFrequenciesHzForTract(2);
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
        connect(ui->pushButtonRecieveTestPause, &QPushButton::clicked,
                this, &MainWindow::onReceiveTestPauseClicked);
    }
    if (ui->pushButtonRecieveTestStop) {
        ui->pushButtonRecieveTestStop->setIcon(m_receiveTestIconStop);
        connect(ui->pushButtonRecieveTestStop, &QPushButton::clicked,
                this, &MainWindow::onReceiveTestStopClicked);
    }
    resetReceiveReadoutUi();
    updateReceiveTestButtonsAccessForSelectedTract();
}

namespace {

static void applyIndicatorStyle(QLabel *lbl, const QString &text, const QString &style, int minWidth = -1)
{
    if (!lbl) return;
    lbl->setText(text);
    lbl->setStyleSheet(style);
    if (minWidth >= 0) {
        lbl->setMinimumWidth(minWidth);
    }
}

static QString indicatorBoxStyle(const QString &fg, const QString &bg, const QString &border)
{
    return QStringLiteral("color: %1; background-color: %2; border: 1px solid %3; border-radius: 6px; padding: 2px 2px; font-family: Consolas; font-weight: bold;")
        .arg(fg, bg, border);
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

    const QString pendingStyle = indicatorBoxStyle("#94a3b8", "#0f172a", "#334155");
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
                s.resultValue->setStyleSheet(QStringLiteral("color: #94a3b8; font-family: Consolas; font-weight: bold;"));
            }
            hideReceiveFinishIcon(s.statusTestFinishOk);
            hideReceiveFinishIcon(s.statusTestFinishNot);
            for (int li = 0; li < kRxLevelsCount; ++li) {
                if (s.levelLabels[li]) {
                    applyIndicatorStyle(s.levelLabels[li],
                                        QString::fromLatin1(kRxLevels[li].title),
                                        pendingStyle);
                }
            }
            if (s.frame) {
                s.frame->setVisible(false);
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

void MainWindow::updateReceiveResultStripsVisibility()
{
    if (!ui) {
        return;
    }
    if (!m_receiveResultStripsBuilt || m_receiveResultStrips.isEmpty()) {
        return;
    }
    if (!m_receiveTestRunning) {
        return;
    }
    for (int i = 0; i < m_receiveResultStrips.size(); ++i) {
        if (m_receiveResultStrips[i].frame) {
            m_receiveResultStrips[i].frame->setVisible(i <= m_receiveFreqIndex);
        }
    }

    // Единый progressBar: показываем только во время активного уровня мощности.
    if (ui->progressBarRecieve) {
        const bool show = (m_receivePhase == ReceiveTestPhase::RunningLevel) && m_receiveTestRunning && !m_receiveTestPaused;
        ui->progressBarRecieve->setVisible(show);
        if (!show) {
            ui->progressBarRecieve->setValue(0);
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
    }
    if (ui->pushButtonRecieveTestStop) {
        ui->pushButtonRecieveTestStop->setVisible(false);
    }
    m_receiveTestAutoPausedByPpmNotReady = false;
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
    m_receiveTestAutoPausedByExternalWorkMode = false;
    if (m_testsPausedForExternalWorkMode && !m_powerTestAutoPausedByExternalWorkMode) {
        m_testsPausedForExternalWorkMode = false;
        m_externalWorkModePauseTract = -1;
    }
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
        onDeviceLogMessage(QStringLiteral("ОШИБКА: нет подключения к станции (тест приёма)."));
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

    if (QLabel *rv = receiveStripResultLabel(m_receiveFreqIndex)) {
        rv->setText(QStringLiteral("Ждём RSSI на %1...")
                        .arg(formatGroupedWithDots(static_cast<uint32_t>(m_receiveTestFreqHz))));
        rv->setStyleSheet(QStringLiteral("color: #fbbf24; font-family: Consolas; font-weight: bold;"));
    }
    // progressBar живёт внутри ReceiveResultStrip и управляется updateReceiveResultStripsVisibility/onReceiveTestTick.
    updateTabWidgetLockState();
}

void MainWindow::onReceiveTestPauseClicked()
{
    if (!ui || !m_receiveTestRunning) {
        return;
    }

    if (!m_receiveTestPaused) {
        m_receiveTestPaused = true;
        m_receiveTestAutoPausedByPpmNotReady = false; // ручная пауза не должна авто-возобновляться
        m_receiveTestTickTimer.stop();
        if (m_receivePhase == ReceiveTestPhase::RunningLevel && m_analyzerController) {
            m_analyzerController->setGenerator(m_receiveTestFreqHz, /*state*/ 0, m_receiveTestPow);
            setEmissionAnimating(false);
        }
        setReceiveTestControlsRunning(true);
        updateReceiveResultStripsVisibility();
        updateTabWidgetLockState();
        return;
    }

    m_receiveTestPaused = false;
    setReceiveTestControlsRunning(false);
    if (m_receivePhase == ReceiveTestPhase::RunningLevel) {
        m_receiveTestTickTimer.start();
        if (m_analyzerController) {
            m_analyzerController->setGenerator(m_receiveTestFreqHz, /*state*/ 1, m_receiveTestPow);
        }
        onReceiveTestTick();
    }
    updateReceiveResultStripsVisibility();
    updateTabWidgetLockState();
}

void MainWindow::onReceiveTestStopClicked()
{
    tearDownReceiveTest(true);
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
    // Единый progressBar для текущего уровня мощности.
    if (ui->progressBarRecieve) {
        ui->progressBarRecieve->setTextVisible(false);
        ui->progressBarRecieve->setRange(0, 100);
        ui->progressBarRecieve->setVisible(true);
        constexpr int kLevelDurationMs = 5000;
        const int ms = static_cast<int>(m_receiveTestElapsed.elapsed());
        const int v = qBound(0, (ms * 100) / kLevelDurationMs, 100);
        ui->progressBarRecieve->setValue(v);
    }

    auto indicatorFor = [&](int freqIdx, int levelIdx) -> QLabel * {
        if (freqIdx < 0 || freqIdx >= m_receiveResultStrips.size()) {
            return nullptr;
        }
        if (levelIdx < 0 || levelIdx >= kRxLevelsCount) {
            return nullptr;
        }
        return m_receiveResultStrips[freqIdx].levelLabels[levelIdx];
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
            rv->setStyleSheet(ok ? QStringLiteral("color: #4ade80; font-family: Consolas; font-weight: bold;")
                                 : QStringLiteral("color: #fbbf24; font-family: Consolas; font-weight: bold;"));
        }
        return;
    }

    // Завершение уровня: выключаем генератор, выставляем индикатор PASS/FAIL и переходим к следующему уровню/частоте.
    if (m_analyzerController) {
        m_analyzerController->setGenerator(m_receiveTestFreqHz, /*state*/ 0, m_receiveTestPow);
    }
    // Уровень завершён — сбросим progressBar (до старта следующего уровня).
    if (ui->progressBarRecieve) {
        ui->progressBarRecieve->setVisible(false);
        ui->progressBarRecieve->setValue(0);
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
        if (m_receiveFreqIndex >= 0 && m_receiveFreqIndex < m_receiveResultStrips.size()) {
            auto &s = m_receiveResultStrips[m_receiveFreqIndex];
            showReceiveFinishIcons(s.statusTestFinishOk, s.statusTestFinishNot,
                                   m_receiveFreqAllLevelsOk[m_receiveFreqIndex]);
            if (s.resultValue) {
                s.resultValue->setText(m_receiveFreqAllLevelsOk[m_receiveFreqIndex]
                                           ? QStringLiteral("тест пройден")
                                           : QStringLiteral("тест не пройден"));
                s.resultValue->setStyleSheet(m_receiveFreqAllLevelsOk[m_receiveFreqIndex]
                                                 ? QStringLiteral("color: #4ade80; font-family: Consolas; font-weight: bold;")
                                                 : QStringLiteral("color: #ef4444; font-family: Consolas; font-weight: bold;"));
            }
        }
    }

    ++m_receiveFreqIndex;
    m_receiveLevelIndex = 0;
    if (m_receiveFreqIndex < m_receiveTestFreqsHz.size()) {
        m_receiveTestFreqHz = m_receiveTestFreqsHz[m_receiveFreqIndex];
        if (ui->lcdRecieveFreqValue) {
            ui->lcdRecieveFreqValue->display(formatGroupedWithDots(static_cast<uint32_t>(m_receiveTestFreqHz)));
        }
        updateReceiveResultStripsVisibility();
        if (QLabel *rv = receiveStripResultLabel(m_receiveFreqIndex)) {
            rv->setText(QStringLiteral("Ждём RSSI на %1...")
                            .arg(formatGroupedWithDots(static_cast<uint32_t>(m_receiveTestFreqHz))));
            rv->setStyleSheet(QStringLiteral("color: #fbbf24; font-family: Consolas; font-weight: bold;"));
        }
        if (!m_deviceController->setFrequencyRx(static_cast<uint8_t>(m_receiveTestTract),
                                                static_cast<uint32_t>(m_receiveTestFreqHz))) {
            onDeviceLogMessage(QStringLiteral("ОШИБКА: не удалось установить RX частоту %1 Гц (тест приёма).")
                                   .arg(formatGroupedWithDots(static_cast<uint32_t>(m_receiveTestFreqHz))));
            tearDownReceiveTest(true);
            return;
        }
        m_receivePhase = ReceiveTestPhase::WaitBaseline;
        return;
    }

    m_receiveTestTickTimer.stop();
    m_receiveTestRunning = false;
    m_receivePhase = ReceiveTestPhase::Idle;
    setReceiveTestControlsIdle();
    updateTabWidgetLockState();
}

void MainWindow::onFreqRxIndicationReceived(uint8_t tractNum, uint32_t freqHz)
{
    if (!shouldUpdatePowerReadoutForTract(tractNum)) {
        return;
    }
    if (ui && ui->lcdRecieveFreqValue) {
        ui->lcdRecieveFreqValue->display(formatGroupedWithDots(freqHz));
    }
}

void MainWindow::onFreqTxIndicationReceived(uint8_t tractNum, uint32_t freqHz)
{
    // Запоминаем последнюю установленную TX частоту по тракту всегда — это нужно для "Авария АНТ" пульсера.
    m_lastTxFreqHzByTract.insert(static_cast<int>(tractNum), static_cast<quint64>(freqHz));

    if (!shouldUpdatePowerReadoutForTract(tractNum) || !ui->lcdPowerFreqValue) {
        return;
    }
    ui->lcdPowerFreqValue->display(formatGroupedWithDots(freqHz));
}

void MainWindow::onRssiIndicationReceived(uint8_t tractNum, int16_t rssiDbm)
{
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
        auto indicatorFor = [&](int freqIdx, int levelIdx) -> QLabel * {
            if (freqIdx < 0 || freqIdx >= m_receiveResultStrips.size()) {
                return nullptr;
            }
            if (levelIdx < 0 || levelIdx >= kRxLevelsCount) {
                return nullptr;
            }
            return m_receiveResultStrips[freqIdx].levelLabels[levelIdx];
        };
        applyIndicatorStyle(indicatorFor(m_receiveFreqIndex, m_receiveLevelIndex),
                            QString::fromLatin1(kRxLevels[m_receiveLevelIndex].title),
                            runStyle);

        if (QLabel *rv = receiveStripResultLabel(m_receiveFreqIndex)) {
            rv->setText(QStringLiteral("Подача мощности (%1 dBm)...").arg(kRxLevels[m_receiveLevelIndex].dbm));
            rv->setStyleSheet(QStringLiteral("color: #38bdf8; font-family: Consolas; font-weight: bold;"));
        }

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
        ui->progressBar->setTextVisible(false);
        ui->progressBar->setRange(0, 0);
        ui->progressBar->setValue(0);
        ui->progressBar->setVisible(true);
    }
    if (ui && ui->framePPM) {
        ui->framePPM->setVisible(false);
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
        ui->progressBar->setTextVisible(false);
        ui->progressBar->setRange(0, 0);
        ui->progressBar->setValue(0);
        ui->progressBar->setVisible(true);
    }
    if (ui && ui->framePPM) {
        ui->framePPM->setVisible(false);
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
        m_ppmIgnoreExternalPowerOffUntilMs = (nowMs >= 0) ? (nowMs + 1500) : 0;
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
        // Фиксируем завершение внутреннего переключения тракта:
        // в ближайшие ~2 секунды изменения IND_WORKMODE считаем "своими".
        m_ppmLastTractSwitchToTract = m_ppmCurrentOnTract;
        m_ppmLastTractSwitchFinishedAtMs = m_uptime.isValid() ? m_uptime.elapsed() : -1;

        // UI: переключение завершено — возвращаем PPM и прячем progressBar.
        if (ui && ui->progressBar) {
            ui->progressBar->setRange(0, 100);
            ui->progressBar->setValue(0);
            ui->progressBar->setVisible(false);
        }
        if (ui && ui->framePPM) {
            ui->framePPM->setVisible(true);
        }
        applyPowerLevelUiByCode(static_cast<uint8_t>(m_powerLevelCodeByTract.value(m_ppmCurrentOnTract, 4)),
                                /*rescaleGraph*/ true);
        // Важно: IND_ERROR целевого тракта мог прийти ещё во время переключения,
        // когда selectedPpmTractFromUi() указывал на предыдущий тракт.
        // После завершения переключения перерисовываем статус из кэша целевого тракта.
        refreshPpmStatusUiForTract(m_ppmCurrentOnTract);

        // По требованию: post-update (аналог "labelUpdate") выполняем только
        // в сценарии восстановления тракта после внешнего выключения.
        if (m_ppmSwitchNeedsPostUpdate && m_ppmCurrentOnTract > 0) {
            const bool restarted = restartPpmModeForTract(m_ppmCurrentOnTract);
            if (restarted) {
                setPpmUpdateLabelVisible(false);
            }
        }
        m_ppmSwitchNeedsPostUpdate = false;

        updateTabWidgetLockState();
        return;
    }
}

bool MainWindow::uploadAndActivateTestProfileOverSsh(const QString &stationIp, const QString &localTarPath, QString *errorText)
{
    auto logAsync = [this](const QString &msg) {
        QMetaObject::invokeMethod(this, [this, msg]() { onDeviceLogMessage(msg); }, Qt::QueuedConnection);
    };

    SSHer ssher;
    ssher.setAllowLegacyAlgorithms(true);
    connect(&ssher, &SSHer::logMessage, this, &MainWindow::onDeviceLogMessage, Qt::QueuedConnection);

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
    // Если уже готовили для этой станции — не повторяем.
    if (m_preparedProfileTar && m_preparedProfileStationIp == stationIp.trimmed()) {
        return;
    }
    if (m_preparingProfile) {
        return;
    }

    m_preparingProfile = true;
    m_preparedProfileTar.reset();
    m_preparedProfileStationIp = stationIp.trimmed();
    onDeviceLogMessage(QString("Подключено к %1: подготовка профиля по TraktParam.xml...").arg(m_preparedProfileStationIp));
    setStartTestingButtonEnabled(false);

    QtConcurrent::run([this, stationIpTrimmed = m_preparedProfileStationIp]() {
        QString err;

        SSHer ssher;
        ssher.setAllowLegacyAlgorithms(true);
        connect(&ssher, &SSHer::logMessage, this, &MainWindow::onDeviceLogMessage, Qt::QueuedConnection);

        if (!ssher.connectToHost(stationIpTrimmed, 22)) {
            err = ssher.lastError().isEmpty() ? QStringLiteral("Не удалось подключиться по SSH.") : ssher.lastError();
        } else if (!ssher.authenticate(QString::fromLatin1(kStationSshUser), QString::fromLatin1(kStationSshPassword))) {
            err = ssher.lastError().isEmpty() ? QStringLiteral("Ошибка SSH аутентификации.") : ssher.lastError();
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
        QVector<TraktParamEntry> traktForPpm;
        int traktNumForPpm = 0;
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
                                                   &traktForPpm, &traktNumForPpm)) {
                    err = buildErr.isEmpty() ? QStringLiteral("Не удалось собрать профиль по TraktParam.xml") : buildErr;
                    outTar.reset();
                }
            }
        }

        // Чистим шаблонный tar (если был)
        if (!templateTarPath.isEmpty()) {
            QFile::remove(templateTarPath);
        }

        QMetaObject::invokeMethod(this, [this, stationIpTrimmed, outTar, err, traktForPpm, traktNumForPpm]() {
            m_preparingProfile = false;
            if (!err.isEmpty()) {
                // Если станция уже поменялась — не засоряем лог лишним.
                if (m_deviceController && m_deviceController->config().stationIp.trimmed() == stationIpTrimmed) {
                    onDeviceLogMessage(QString("ОШИБКА подготовки профиля: %1").arg(err));
                }
                m_preparedProfileTar.reset();
                setStartTestingButtonEnabled(false);
                return;
            }
            // Станция могла смениться, пока готовили.
            if (!m_deviceController || m_deviceController->config().stationIp.trimmed() != stationIpTrimmed) {
                m_preparedProfileTar.reset();
                setStartTestingButtonEnabled(false);
                return;
            }
            m_preparedProfileTar = outTar;
            applyTraktParamToPpmUi(traktForPpm, traktNumForPpm);
            onDeviceLogMessage("Профиль подготовлен и готов к отправке (нажмите НАЧАТЬ ТЕСТИРОВАНИЕ).");
            setStartTestingButtonEnabled(true);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::onStartTestingClicked()
{
    const QString stationIp = m_deviceController ? m_deviceController->config().stationIp.trimmed() : QString();
    if (stationIp.isEmpty()) {
        onDeviceLogMessage("ОШИБКА: IP станции не задан (нужно подключиться к станции).");
        return;
    }
    if (!m_preparedProfileTar || m_preparedProfileStationIp != stationIp || m_preparingProfile) {
        onDeviceLogMessage("ОШИБКА: Профиль ещё не подготовлен для текущей станции. Переподключитесь или дождитесь подготовки после подключения.");
        return;
    }

    if (ui->pushButtonStartTesting) {
        ui->pushButtonStartTesting->setVisible(false);
    }
    if (ui->framePPM) {
        // По ТЗ: framePPM показываем только после переподключения станции (после reboot).
        ui->framePPM->setVisible(false);
    }

    setTestingUiBusy(true);
    onDeviceLogMessage(QString("Старт тестирования: отправка профиля на %1 ...").arg(stationIp));

    const QString localTarPath = m_preparedProfileTar->fileName();
    QtConcurrent::run([this, stationIp, localTarPath]() {
        QString err;
        const bool ok = uploadAndActivateTestProfileOverSsh(stationIp, localTarPath, &err);
        QMetaObject::invokeMethod(this, [this, ok, err, stationIp]() {
            if (ok) {
                onDeviceLogMessage("Профиль отправлен и активирован; reboot отправлен.");
                startProfileIntegritySequenceAfterReboot(stationIp);
            } else {
                onDeviceLogMessage(QString("ОШИБКА тестирования: %1").arg(err.isEmpty() ? QString("неизвестная ошибка") : err));
                setTestingUiBusy(false);
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
    }
    if (ui->pushButtonPowerTestStop) {
        ui->pushButtonPowerTestStop->setVisible(true);
    }
    updatePowerLevelRadioButtonsEnabled();
}

double MainWindow::currentPowerGraphCenterDbm(int tractOverride) const
{
    if (m_powerLevelCode != 1) {
        return kPowerGraphMaxLevelCenterDbm;
    }
    const int tractNum = (tractOverride > 0) ? tractOverride
                                             : ((m_ppmCurrentOnTract > 0) ? m_ppmCurrentOnTract
                                                                          : selectedPpmTractFromUi());
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

void MainWindow::updatePowerTestingPlots(const QVector<double> &freqs, const QVector<double> &amps)
{
    if (!m_powerPlotsInitialized || freqs.isEmpty() || amps.size() != freqs.size()) {
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
        onDeviceLogMessage(QStringLiteral("ОШИБКА: частота шага вне диапазона."));
        return false;
    }

    if (!m_deviceController->setFrequencyTx(m_powerTestTargetTract, static_cast<uint32_t>(freqHz))) {
        onDeviceLogMessage(QStringLiteral("ОШИБКА: не удалось установить частоту %1 Гц.")
                               .arg(formatGroupedWithDots(freqHz)));
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

    onDeviceLogMessage(QStringLiteral("📡 Шаг %1/%2: F=%3 Гц, multicast %4. Пауза 1 сек перед выходом на мощность.")
                           .arg(m_powerTestSequenceIndex + 1)
                           .arg(m_powerTestSequenceFreqsHz.size())
                           .arg(formatGroupedWithDots(freqHz))
                           .arg(m_powerTestMulticastAddress));
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
    const double greenLoDbm = centerDbm - 2.0;
    const double greenHiDbm = centerDbm + 2.0;
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
            onDeviceLogMessage(QStringLiteral("⚠ Амплитуда %1 dBm на F=%2 Гц вне допуска [%3; %4] dBm. Переизмерение %5/%6 на этой же частоте.")
                                   .arg(QString::number(bestAmpDbm, 'f', 2))
                                   .arg(formatGroupedWithDots(freqHz))
                                   .arg(QString::number(greenLoDbm, 'f', 1))
                                   .arg(QString::number(greenHiDbm, 'f', 1))
                                   .arg(m_powerTestCurrentFreqRetryCount)
                                   .arg(kPowerTestRemeasureMaxCount));
        } else if (!insideBand) {
            onDeviceLogMessage(QStringLiteral("⚠ Амплитуда %1 dBm на F=%2 Гц вне допуска [%3; %4] dBm после %5 переизмерений — фиксируем результат.")
                                   .arg(QString::number(bestAmpDbm, 'f', 2))
                                   .arg(formatGroupedWithDots(freqHz))
                                   .arg(QString::number(greenLoDbm, 'f', 1))
                                   .arg(QString::number(greenHiDbm, 'f', 1))
                                   .arg(kPowerTestRemeasureMaxCount));
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

            onDeviceLogMessage(QStringLiteral("⏱ Замер завершен: F=%1 Гц, максимум %2 dBm (bin %3 MHz).")
                                   .arg(formatGroupedWithDots(freqHz))
                                   .arg(QString::number(bestAmpDbm, 'f', 2))
                                   .arg(QString::number(sampleFreqMHz, 'f', 6)));
        }
    } else {
        onDeviceLogMessage(QStringLiteral("⏱ Замер завершен: F=%1 Гц, точки спектра за 5 секунд не получены.")
                               .arg(formatGroupedWithDots(freqHz)));
    }

    if (shouldRetryCurrentFrequency) {
        onDeviceLogMessage(QStringLiteral("Пауза 2 секунды перед повторным выходом на мощность на той же частоте..."));
        m_powerTestStepPauseTimer.start(kPowerTestPauseBetweenRemeasureMs);
        return;
    }

    m_powerTestCurrentFreqRetryCount = 0;
    ++m_powerTestSequenceIndex;
    if (m_powerTestSequenceIndex >= m_powerTestSequenceFreqsHz.size()) {
        if (ui && ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked()) {
            ui->pushButtonStartTestingPower->setChecked(false);
        }
        onDeviceLogMessage(QStringLiteral("✅ Тест мощности по последовательности частот завершен."));
        return;
    }

    onDeviceLogMessage(QStringLiteral("Пауза 1 секунда перед следующей частотой..."));
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
        setPowerTestControlsRunning(false);
        // Resume after pause on "Нет связи с ПП"
        if (m_powerTestPaused) {
            if (!m_deviceController || !m_deviceController->isConnected()) {
                onDeviceLogMessage(QStringLiteral("ОШИБКА: нет подключения к станции (нужно подключиться)."));
                QSignalBlocker blocker(ui->pushButtonStartTestingPower);
                ui->pushButtonStartTestingPower->setChecked(false);
                setPowerTestControlsIdle();
                return;
            }
            if (!m_powerTrafficGenerator) {
                onDeviceLogMessage(QStringLiteral("ОШИБКА: генератор трафика не инициализирован."));
                QSignalBlocker blocker(ui->pushButtonStartTestingPower);
                ui->pushButtonStartTestingPower->setChecked(false);
                setPowerTestControlsIdle();
                return;
            }
            if (m_powerTestBlockedByPpm) {
                onDeviceLogMessage(QStringLiteral("ППМ: тест мощности не может быть продолжен — нет связи с ПП."));
                QSignalBlocker blocker(ui->pushButtonStartTestingPower);
                ui->pushButtonStartTestingPower->setChecked(false);
                setPowerTestControlsIdle();
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
                    QSignalBlocker blocker(ui->pushButtonStartTestingPower);
                    ui->pushButtonStartTestingPower->setChecked(false);
                    setPowerTestControlsIdle();
                    return;
                }
                if (ui->emissionAntennaWidget) {
                    ui->emissionAntennaWidget->setVisible(true);
                }
                setPowerTestControlsRunning(false);
                return;
            }
        }

        if (!m_deviceController || !m_deviceController->isConnected()) {
            onDeviceLogMessage(QStringLiteral("ОШИБКА: нет подключения к станции (нужно подключиться)."));
            QSignalBlocker blocker(ui->pushButtonStartTestingPower);
            ui->pushButtonStartTestingPower->setChecked(false);
            setPowerTestControlsIdle();
            return;
        }
        if (!m_powerTrafficGenerator) {
            onDeviceLogMessage(QStringLiteral("ОШИБКА: генератор трафика не инициализирован."));
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
            onDeviceLogMessage(QStringLiteral("ПРЕДУПРЕЖДЕНИЕ: TrmType для текущего тракта не определен, используется адрес по умолчанию 224.0.1.3."));
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

        switch (m_powerTestTargetTrmType) {
        case 2:
            m_powerTestSequenceFreqsHz = kPowerTestFrequenciesType2Hz;
            break;
        case 3:
            m_powerTestSequenceFreqsHz = kPowerTestFrequenciesType3Hz;
            break;
        case 4:
            m_powerTestSequenceFreqsHz = kPowerTestFrequenciesType4Hz;
            break;
        default:
            m_powerTestSequenceFreqsHz = kPowerTestFrequenciesType2Hz;
            break;
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
            ui->pushButtonStartTestingPower->setChecked(false);
            setPowerTestControlsIdle();
            return;
        }
        if (ui->emissionAntennaWidget) {
            ui->emissionAntennaWidget->setVisible(true);
        }
    } else {
        ++m_resumeAfterExternalWorkModeSerial;
        m_testsPausedForExternalWorkMode = false;
        m_externalWorkModePauseTract = -1;
        m_powerTestAutoPausedByExternalWorkMode = false;
        setPowerTestControlsIdle();
        m_powerTestPaused = false;
        m_powerTestBlockedByPpm = false;
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
            onDeviceLogMessage(QStringLiteral("⏹ Остановка теста мощности: остановка генератора трафика..."));
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
    }
}

void MainWindow::onPowerTestPauseClicked()
{
    if (!ui || !ui->pushButtonStartTestingPower || !ui->pushButtonPowerTestPause) {
        return;
    }

    // Пауза: останавливаем таймеры/трафик, сохраняем индекс/последовательность, чтобы продолжить с той же частоты.
    if (!m_powerTestPaused) {
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

    // Продолжение: используем штатный resume-путь через onPowerTestingToggled(true).
    setPowerTestControlsRunning(false);
    ui->pushButtonStartTestingPower->setChecked(true);
}

void MainWindow::onPowerTestStopClicked()
{
    if (!ui || !ui->pushButtonStartTestingPower) {
        return;
    }
    // Стоп: полный сброс через onPowerTestingToggled(false).
    if (ui->pushButtonStartTestingPower->isChecked()) {
        ui->pushButtonStartTestingPower->setChecked(false);
        return;
    }
    if (m_powerTestPaused) {
        m_powerTestPaused = false;
    }
    setPowerTestControlsIdle();
}

void MainWindow::onTabWidgetCurrentChanged(int index)
{
    if (index >= 0 && !m_tabWidgetWasLocked) {
        m_lastUnlockedTabIndex = index;
    }

    bool isHands = false;
    bool isPower = false;
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

    m_startSpectrumOnHands = isHands;

    if (isHands || isPower) {
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

    // Важно: после setEditable(true) Qt создаёт внутренний QLineEdit со своим шрифтом.
    // Принудительно синхронизируем шрифт с lineEditSpectrumCenterMHz.
    if (ui->lineEditSpectrumCenterMHz) {
        const QFont f = ui->lineEditSpectrumCenterMHz->font();
        ui->comboBoxSpectrumSpanMHz->setFont(f);
        if (ui->comboBoxSpectrumSpanMHz->view()) {
            ui->comboBoxSpectrumSpanMHz->view()->setFont(f);
        }
    }
    ui->comboBoxSpectrumSpanMHz->setEditable(true);
    ui->comboBoxSpectrumSpanMHz->setInsertPolicy(QComboBox::NoInsert);
    if (QLineEdit *line = ui->comboBoxSpectrumSpanMHz->lineEdit()) {
        if (ui->lineEditSpectrumCenterMHz) {
            line->setFont(ui->lineEditSpectrumCenterMHz->font());
        }
        line->setReadOnly(true);
        line->setFrame(false);
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
    if (ui->labelSpectrumPeakFreqValue && ui->labelSpectrumPeakPowerValue) {
        if (m_spectrumLatestFreqs.isEmpty()
            || m_spectrumLatestAmps.size() != m_spectrumLatestFreqs.size()) {
            ui->labelSpectrumPeakFreqValue->display(QStringLiteral("----"));
            ui->labelSpectrumPeakPowerValue->display(QStringLiteral("----"));
        } else {
            int iMax = 0;
            double maxAmp = m_spectrumLatestAmps[0];
            for (int i = 1; i < m_spectrumLatestAmps.size(); ++i) {
                if (m_spectrumLatestAmps[i] > maxAmp) {
                    maxAmp = m_spectrumLatestAmps[i];
                    iMax = i;
                }
            }
            ui->labelSpectrumPeakFreqValue->display(QString::number(m_spectrumLatestFreqs[iMax], 'f', 6));
            ui->labelSpectrumPeakPowerValue->display(QString::number(maxAmp, 'f', 2));
        }
    }
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
    return ui->pushButtonSpectrumClearHold && ui->pushButtonSpectrumClearHold->isChecked();
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
    if (!ui->labelPeakFreqValue || !ui->labelPeakPowerValue) {
        return;
    }
    if (m_spectrumLatestFreqs.isEmpty()
        || m_spectrumLatestAmps.size() != m_spectrumLatestFreqs.size()) {
        ui->labelPeakFreqValue->display(QStringLiteral("----"));
        ui->labelPeakPowerValue->display(QStringLiteral("----"));
        return;
    }
    int best = 0;
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
                best = i;
            }
        }
    } else {
        double bestAmp = m_spectrumLatestAmps[0];
        for (int i = 1; i < m_spectrumLatestAmps.size(); ++i) {
            if (m_spectrumLatestAmps[i] > bestAmp) {
                bestAmp = m_spectrumLatestAmps[i];
                best = i;
            }
        }
    }
    const double bestAmp = m_spectrumLatestAmps[best];
    const quint64 bestHz = static_cast<quint64>(std::llround(m_spectrumLatestFreqs[best] * 1e6));
    ui->labelPeakFreqValue->display(formatGroupedWithDots(bestHz));
    ui->labelPeakPowerValue->display(QString::number(bestAmp, 'f', 2));
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

void MainWindow::stopSpectrumStream()
{
    if (!m_spectrumStreaming) {
        return;
    }

    m_spectrumGridAlignPending = false;
    m_spectrumGridAlignAttemptsLeft = 0;

    m_spectrumUiTimer.stop();
    m_spectrumDisplayDirty = false;
    m_analyzerController->stopSpectrumStream();
    m_spectrumStreaming = false;
}
