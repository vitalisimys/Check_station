#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "debug.h"
#include "styles.h"
#include "sweep_plot.h"
#include "qcustomplot.h"
#include "protocol_consts.h"
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
#include <QAbstractButton>
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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_deviceController(new DeviceController(this))
    , m_analyzerController(new AnalyzerController(this))
    , m_finder(new FindManager(this))
{
    ui->setupUi(this);
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

    m_powerTrafficGenerator = new PowerTrafficGenerator(this);
    connect(m_powerTrafficGenerator, &PowerTrafficGenerator::logMessage,
            this, &MainWindow::onDeviceLogMessage);
    connect(m_powerTrafficGenerator, &PowerTrafficGenerator::errorOccurred,
            this, &MainWindow::onDeviceError);

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
    onTabWidgetCurrentChanged(ui->tabWidget->currentIndex());

    if (QPushButton *holdBtn = ui->pushButtonSpectrumClearHold) {
        holdBtn->setCheckable(true);
        holdBtn->setAutoDefault(false);
        holdBtn->setDefault(false);
        connect(holdBtn, &QPushButton::toggled, this, &MainWindow::onSpectrumMaxHoldToggled);
    }

    initPowerTestingUi();

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
}

MainWindow::~MainWindow()
{
    cleanupAddedSelfIp();
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

void MainWindow::on_actionSettings_triggered()
{
    // В настройки передаём уже просканированные интерфейсы (и, опционально,
    // найденные IP для единственного интерфейса), чтобы не пересканировать заново.
    const QString preselectedIface = (m_cachedIfaces.size() == 1) ? m_cachedIfaces.value(0) : QString();
    const QVector<QString> cachedIps =
        (!preselectedIface.isEmpty() && m_cachedFoundIpsByIface.contains(preselectedIface))
            ? m_cachedFoundIpsByIface.value(preselectedIface)
            : QVector<QString>();

    SettingsDialog dialog(this, m_cachedIfaces, preselectedIface, cachedIps);
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
    if (stationCount == 0) {
        onDeviceLogMessage(QString("Радиостанции на %1 не найдены. Откройте настройки и выберите станцию/интерфейс.").arg(iface));
        return;
    }

    onDeviceLogMessage(QString("Найдено станций на %1: %2").arg(iface).arg(stationCount));

    // Если по итоговой логике выбора станция ровно одна — подключаемся автоматически.
    if (stationCount == 1) {
        const QString stationIp = chosenBySubnet.cbegin().value();
        QString selfIp;
        QString err;
        if (!ensureStationIpsConfigured(iface, stationIp, &selfIp, &err)) {
            onDeviceLogMessage(QString("Автоподключение не выполнено: %1").arg(err));
            return;
        }
        onDeviceLogMessage(QString("Автоподключение к станции %1 (интерфейс %2)...").arg(stationIp, iface));
        onStationConnectRequested(stationIp, selfIp, iface);
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

void MainWindow::onStationConnectRequested(const QString &stationIp, const QString &selfIp, const QString &interfaceName) {
    if (m_deviceController->isConnected()) {
        m_deviceController->disconnectFromDevice();
    }

    ui->frameStation->setVisible(false);
    if (!selfIp.trimmed().isEmpty()) {
        m_deviceController->setSelfIp(selfIp);
        onDeviceLogMessage(QString("Выбран self IP контроллера: %1").arg(selfIp));
    }
    m_deviceController->setStationIp(stationIp);
    onDeviceLogMessage(QString("Запрос подключения к станции %1").arg(stationIp));

    // Запоминаем для очистки при выходе (может быть несколько станций/несколько добавлений).
    const QStringList parts = stationIp.trimmed().split('.');
    const int cidr = (parts.size() == 4 && parts[3] == "193") ? 26 : 25;
    AddedIpEntry entry;
    entry.ip = selfIp.trimmed();
    entry.cidr = cidr;
    entry.iface = interfaceName.trimmed();
    if (!entry.iface.isEmpty()) {
        const QString cmd = QString("nmcli -t -f UUID,DEVICE connection show --active | grep -F \":%1\" | cut -d':' -f1")
                                .arg(entry.iface);
        const QPair<bool, QString> res = executeCommand(cmd);
        entry.connectionUuid = res.second.trimmed().split('\n', Qt::SkipEmptyParts).value(0).trimmed();
    }

    // Не дублируем одинаковые записи.
    const bool exists = std::any_of(m_addedIps.cbegin(), m_addedIps.cend(), [&entry](const AddedIpEntry &e) {
        return e.iface == entry.iface && e.ip == entry.ip && e.cidr == entry.cidr;
    });
    if (!exists && !entry.ip.isEmpty() && entry.cidr > 0 && !entry.iface.isEmpty()) {
        m_addedIps.push_back(entry);
    }

    m_deviceController->connectToDevice();
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
}

void MainWindow::onDeviceDisconnected() {
    if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
        m_powerTrafficGenerator->stop();
    }
    if (ui && ui->pushButtonStartTestingPower && ui->pushButtonStartTestingPower->isChecked()) {
        QSignalBlocker blocker(ui->pushButtonStartTestingPower);
        ui->pushButtonStartTestingPower->setChecked(false);
    }

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
    ui->frameStation->setStyleSheet(styleSheetDisconnectStation);
    ui->labelPixStation->setPixmap(QPixmap(":/led_red.png"));
    ui->labelStateStation->setText("Отключена");
    ui->labelStateStation->setStyleSheet("color: #ff5252;");
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

void MainWindow::initPpmUiStyle()
{
    if (!ui->framePPM) {
        return;
    }
    ui->framePPM->setStyleSheet(styleSheetFramePpm);
    if (ui->radioPPM1) {
        ui->radioPPM1->setStyleSheet(styleSheetPpmRadio);
    }
    if (ui->radioPPM2) {
        ui->radioPPM2->setStyleSheet(styleSheetPpmRadio);
    }
    ui->framePPM->setVisible(false);

    m_ppmButtonGroup = new QButtonGroup(this);
    m_ppmButtonGroup->setExclusive(true);
    if (ui->radioPPM1 && ui->radioPPM2) {
        m_ppmButtonGroup->addButton(ui->radioPPM1, 0);
        m_ppmButtonGroup->addButton(ui->radioPPM2, 1);
        ui->radioPPM1->setChecked(true);
    }
}

void MainWindow::applyTraktParamToPpmUi(const QVector<TraktParamEntry> &entries, int traktNum)
{
    m_maxTrLn = 0;
    for (const TraktParamEntry &e : entries) {
        m_maxTrLn = qMax(m_maxTrLn, e.trLn);
    }

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
    const int trNum = (traktNum > 0) ? traktNum : sorted.size();
    const QStringList labels = ppmLabelsForSortedTrakts(sorted);
    const int radioCount = qMax(trNum, 2);

    for (QRadioButton *ex : m_ppmExtraRadios) {
        m_ppmButtonGroup->removeButton(ex);
        hLay->removeWidget(ex);
        delete ex;
    }
    m_ppmExtraRadios.clear();

    auto setRadioText = [&](QRadioButton *rb, int idx) {
        if (idx < labels.size()) {
            rb->setText(labels.at(idx));
        } else {
            rb->setText(QStringLiteral("—"));
        }
    };

    setRadioText(ui->radioPPM1, 0);
    setRadioText(ui->radioPPM2, 1);

    for (int i = 2; i < radioCount; ++i) {
        QRadioButton *rb = new QRadioButton(ui->framePPM);
        rb->setFont(ui->radioPPM1->font());
        rb->setStyleSheet(styleSheetPpmRadio);
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
    ui->radioPPM1->setChecked(true);
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
                return;
            }
            // Станция могла смениться, пока готовили.
            if (!m_deviceController || m_deviceController->config().stationIp.trimmed() != stationIpTrimmed) {
                m_preparedProfileTar.reset();
                return;
            }
            m_preparedProfileTar = outTar;
            applyTraktParamToPpmUi(traktForPpm, traktNumForPpm);
            onDeviceLogMessage("Профиль подготовлен и готов к отправке (нажмите НАЧАТЬ ТЕСТИРОВАНИЕ).");
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
        ui->framePPM->setVisible(true);
    }

    setTestingUiBusy(true);
    onDeviceLogMessage(QString("Старт тестирования: отправка профиля на %1 ...").arg(stationIp));

    const QString localTarPath = m_preparedProfileTar->fileName();
    QtConcurrent::run([this, stationIp, localTarPath]() {
        QString err;
        const bool ok = uploadAndActivateTestProfileOverSsh(stationIp, localTarPath, &err);
        QMetaObject::invokeMethod(this, [this, ok, err]() {
            if (ok) {
                onDeviceLogMessage("Профиль отправлен и активирован; reboot отправлен.");
            } else {
                onDeviceLogMessage(QString("ОШИБКА тестирования: %1").arg(err.isEmpty() ? QString("неизвестная ошибка") : err));
            }
            setTestingUiBusy(false);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::initPowerTestingUi()
{
    if (ui->labelPower) {
        ui->labelPower->setScaledContents(true);
        ui->labelPower->setVisible(false);
    }

    m_powerTestMovie = new QMovie(QStringLiteral(":/antenna_power.gif"), QByteArray(), this);
    m_powerTestMovie->setCacheMode(QMovie::CacheAll);
    if (ui->labelPower) {
        ui->labelPower->setMovie(m_powerTestMovie);
    }

    if (QPushButton *btn = ui->pushButtonStartTestingPower) {
        btn->setCheckable(true);
        btn->setAutoDefault(false);
        btn->setDefault(false);
        connect(btn, &QPushButton::toggled, this, &MainWindow::onPowerTestingToggled);
    }
}

void MainWindow::onPowerTestingToggled(bool checked)
{
    if (!ui->pushButtonStartTestingPower) {
        return;
    }
    if (checked) {
        if (!m_deviceController || !m_deviceController->isConnected()) {
            onDeviceLogMessage(QStringLiteral("ОШИБКА: нет подключения к станции (нужно подключиться)."));
            QSignalBlocker blocker(ui->pushButtonStartTestingPower);
            ui->pushButtonStartTestingPower->setChecked(false);
            return;
        }
        if (!m_powerTrafficGenerator) {
            onDeviceLogMessage(QStringLiteral("ОШИБКА: генератор трафика не инициализирован."));
            QSignalBlocker blocker(ui->pushButtonStartTestingPower);
            ui->pushButtonStartTestingPower->setChecked(false);
            return;
        }

        onDeviceLogMessage(QStringLiteral("📡 Запуск теста мощности: генерация RTP/UDP трафика..."));

        m_powerTrafficGenerator->setBindIp(m_deviceController->config().selfIp);
        m_powerTrafficGenerator->setMulticastAddress(QString::fromLatin1(TRAFFIC_MCAST_IP));
        m_powerTrafficGenerator->setMulticastPort(TRAFFIC_DST_PORT);
        m_powerTrafficGenerator->setSourcePort(TRAFFIC_SRC_PORT);
        m_powerTrafficGenerator->setDscp(DSCP_STREAMVOICE);
        m_powerTrafficGenerator->setEcn(ECN_DEFAULT);
        m_powerTrafficGenerator->setPayloadType(RTP_PAYLOAD_TYPE);
        m_powerTrafficGenerator->setTractNumber(DEFAULT_TRACT_NUM);

        if (!m_powerTrafficGenerator->start()) {
            onDeviceLogMessage(QStringLiteral("ОШИБКА: не удалось запустить генератор трафика."));
            QSignalBlocker blocker(ui->pushButtonStartTestingPower);
            ui->pushButtonStartTestingPower->setChecked(false);
            return;
        }

        ui->pushButtonStartTestingPower->setText(QStringLiteral("Идет тестирование"));
        if (ui->labelPower) {
            ui->labelPower->setVisible(true);
        }
        if (m_powerTestMovie) {
            m_powerTestMovie->start();
        }
    } else {
        if (m_powerTrafficGenerator && m_powerTrafficGenerator->isRunning()) {
            onDeviceLogMessage(QStringLiteral("⏹ Остановка теста мощности: остановка генератора трафика..."));
            m_powerTrafficGenerator->stop();
        }
        ui->pushButtonStartTestingPower->setText(QStringLiteral("НАЧАТЬ ТЕСТ МОЩНОСТИ"));
        if (m_powerTestMovie) {
            m_powerTestMovie->stop();
        }
        if (ui->labelPower) {
            ui->labelPower->setVisible(false);
        }
    }
}

void MainWindow::onTabWidgetCurrentChanged(int index)
{
    bool isHands = false;
    if (m_tabHandsIndex >= 0) {
        isHands = (index == m_tabHandsIndex);
    } else {
        QWidget *w = ui->tabWidget ? ui->tabWidget->widget(index) : nullptr;
        isHands = (w && w->objectName() == QStringLiteral("tabHands"));
    }

    m_startSpectrumOnHands = isHands;

    if (isHands) {
        if (!m_spectrumPlotInitialized) {
            initSpectrumPlot();
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
        if (p.size() != 3) {
            return false;
        }
        bool ok = false;
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
    if (p.size() != 3) {
        return false;
    }
    bool ok = false;
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
    const double hz = a * 1e6 + b * 1e3 + c;
    if (!std::isfinite(hz) || hz <= 0.0 || hz > static_cast<double>(10000000000ULL)) {
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
    if (tu > static_cast<quint64>(10000000000ULL)) {
        return false;
    }
    *startHz = su;
    *stopHz = tu;
    return true;
}

void MainWindow::syncHandsFreqLineEdits(quint64 startHz, quint64 stopHz)
{
    if (ui->lineEditFreqStart) {
        ui->lineEditFreqStart->setText(formatHzTriplet(startHz));
    }
    if (ui->lineEditFreqStop) {
        ui->lineEditFreqStop->setText(formatHzTriplet(stopHz));
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
        ui->lineEditSpectrumCenterMHz->setText(formatHzTriplet(centerHz));
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
        ui->lineEditSpectrumCenterMHz->setText(formatHzTriplet(*lockCenterDisplayHz));
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
            "Диапазон: формат NNN.NNN.NNN Гц, начало < конец, разумные значения частоты."));
        return;
    }
    // Ручной диапазон должен применяться точно как введён, без автоподстройки в сетку прибора.
    m_spectrumGridAlignPending = false;
    m_spectrumGridAlignAttemptsLeft = 0;
    applySpectrumRangeHz(s, e);
    if (ui->labelSpectrumPeakFreqValue && ui->labelSpectrumPeakPowerValue) {
        if (m_spectrumLatestFreqs.isEmpty()
            || m_spectrumLatestAmps.size() != m_spectrumLatestFreqs.size()) {
            ui->labelSpectrumPeakFreqValue->setText(QStringLiteral("—"));
            ui->labelSpectrumPeakPowerValue->setText(QStringLiteral("—"));
        } else {
            int iMax = 0;
            double maxAmp = m_spectrumLatestAmps[0];
            for (int i = 1; i < m_spectrumLatestAmps.size(); ++i) {
                if (m_spectrumLatestAmps[i] > maxAmp) {
                    maxAmp = m_spectrumLatestAmps[i];
                    iMax = i;
                }
            }
            ui->labelSpectrumPeakFreqValue->setText(
                QString::number(m_spectrumLatestFreqs[iMax], 'f', 6));
            ui->labelSpectrumPeakPowerValue->setText(QString::number(maxAmp, 'f', 1));
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
        ui->labelPeakFreqValue->setText(QStringLiteral("—"));
        ui->labelPeakPowerValue->setText(QStringLiteral("—"));
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
    ui->labelPeakFreqValue->setText(
        QString::number(m_spectrumLatestFreqs[best], 'f', 6));
    ui->labelPeakPowerValue->setText(QString::number(bestAmp, 'f', 1));
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
