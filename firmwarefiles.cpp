#include "firmwarefiles.h"

#include <QCoreApplication>
#include <QFile>

namespace FirmwareFiles {

namespace {

constexpr const char *kCanonicalDtb = "bku-p2020.dtb";
constexpr const char *kCanonicalRootfs = "rootfs.bin";
constexpr const char *kCanonicalKernel = "kernel.bin";
constexpr const char *kCanonicalUboot = "u-boot-spi-spl.bin";

const QStringList kRequiredPrefixes = {
    QStringLiteral("bku-p2020"),
    QStringLiteral("rootfs"),
    QStringLiteral("kernel"),
};

bool matchesPrefix(const QString &fileName, const QString &prefix)
{
    const QString lowerName = fileName.trimmed().toLower();
    const QString lowerPrefix = prefix.trimmed().toLower();
    return lowerName == lowerPrefix || lowerName.startsWith(lowerPrefix + QLatin1Char('.'));
}

QString findByPrefixImpl(const QDir &dir, const QString &prefix)
{
    if (!dir.exists()) {
        return {};
    }
    for (const QString &fileName : dir.entryList(QDir::Files)) {
        if (matchesPrefix(fileName, prefix)) {
            return fileName;
        }
    }
    return {};
}

QString canonicalNameForRequest(const QString &requestedName)
{
    const QString lowerName = requestedName.trimmed().toLower();
    if (lowerName == QLatin1String(kCanonicalDtb) || matchesPrefix(lowerName, QStringLiteral("bku-p2020"))) {
        return QString::fromLatin1(kCanonicalDtb);
    }
    if (lowerName == QLatin1String(kCanonicalRootfs) || matchesPrefix(lowerName, QStringLiteral("rootfs"))) {
        return QString::fromLatin1(kCanonicalRootfs);
    }
    if (lowerName == QLatin1String(kCanonicalKernel) || matchesPrefix(lowerName, QStringLiteral("kernel"))) {
        return QString::fromLatin1(kCanonicalKernel);
    }
    if (lowerName == QLatin1String(kCanonicalUboot) || matchesPrefix(lowerName, QStringLiteral("u-boot"))) {
        return QString::fromLatin1(kCanonicalUboot);
    }
    return requestedName.trimmed();
}

QString prefixForCanonicalName(const QString &canonicalName)
{
    const QString lowerName = canonicalName.trimmed().toLower();
    if (lowerName == QLatin1String(kCanonicalDtb)) {
        return QStringLiteral("bku-p2020");
    }
    if (lowerName == QLatin1String(kCanonicalRootfs)) {
        return QStringLiteral("rootfs");
    }
    if (lowerName == QLatin1String(kCanonicalKernel)) {
        return QStringLiteral("kernel");
    }
    if (lowerName == QLatin1String(kCanonicalUboot)) {
        return QStringLiteral("u-boot");
    }
    return {};
}

} // namespace

QString directory()
{
    const QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/update_files");
    QDir().mkpath(path);
    return path;
}

bool hasRequiredFirmwareFiles(const QDir &dir)
{
    for (const QString &prefix : kRequiredPrefixes) {
        if (findByPrefix(dir, prefix).isEmpty()) {
            return false;
        }
    }
    return true;
}

QString findByPrefix(const QDir &dir, const QString &prefix)
{
    return findByPrefixImpl(dir, prefix);
}

QString resolveTftpRequestPath(const QDir &dir, const QString &requestedName)
{
    const QString canonicalName = canonicalNameForRequest(requestedName);
    const QString canonicalPath = dir.absoluteFilePath(canonicalName);
    if (QFile::exists(canonicalPath)) {
        return canonicalPath;
    }

    const QString prefix = prefixForCanonicalName(canonicalName);
    if (!prefix.isEmpty()) {
        const QString matched = findByPrefix(dir, prefix);
        if (!matched.isEmpty()) {
            return dir.absoluteFilePath(matched);
        }
    }

    const QString directPath = dir.absoluteFilePath(requestedName.trimmed());
    if (QFile::exists(directPath)) {
        return directPath;
    }

    return {};
}

bool ensureCanonicalFile(QDir &dir, const QString &prefix, const QString &canonicalName, QString *errorText)
{
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorText) {
            *errorText = QStringLiteral("Не удалось создать каталог update_files.");
        }
        return false;
    }

    const QString canonicalPath = dir.absoluteFilePath(canonicalName);
    if (QFile::exists(canonicalPath)) {
        return true;
    }

    const QString matched = findByPrefix(dir, prefix);
    if (matched.isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("Не найден файл с префиксом «%1».").arg(prefix);
        }
        return false;
    }

    if (matched == canonicalName) {
        return true;
    }

    // Канонического файла на диске точно нет (проверено выше): QFile::copy
    // не перезатрёт существующий файл, поэтому удалять нечего.
    if (!QFile::copy(dir.absoluteFilePath(matched), canonicalPath)) {
        if (errorText) {
            *errorText = QStringLiteral("Не удалось подготовить файл «%1» из «%2».")
                             .arg(canonicalName, matched);
        }
        return false;
    }

    return true;
}

bool prepareForTftp(QString *errorText)
{
    QDir dir(directory());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorText) {
            *errorText = QStringLiteral("Каталог update_files недоступен: %1").arg(dir.absolutePath());
        }
        return false;
    }

    struct Entry {
        QString prefix;
        QString canonicalName;
    };
    const Entry entries[] = {
        {QStringLiteral("bku-p2020"), QString::fromLatin1(kCanonicalDtb)},
        {QStringLiteral("rootfs"), QString::fromLatin1(kCanonicalRootfs)},
        {QStringLiteral("kernel"), QString::fromLatin1(kCanonicalKernel)},
    };

    for (const Entry &entry : entries) {
        if (!ensureCanonicalFile(dir, entry.prefix, entry.canonicalName, errorText)) {
            return false;
        }
    }

    const QString ubootMatched = findByPrefix(dir, QStringLiteral("u-boot"));
    if (!ubootMatched.isEmpty()) {
        if (!ensureCanonicalFile(dir, QStringLiteral("u-boot"), QString::fromLatin1(kCanonicalUboot), errorText)) {
            return false;
        }
    }

    for (const Entry &entry : entries) {
        if (!QFile::exists(dir.absoluteFilePath(entry.canonicalName))) {
            if (errorText) {
                *errorText = QStringLiteral("Файл «%1» не найден в %2.")
                                 .arg(entry.canonicalName, dir.absolutePath());
            }
            return false;
        }
    }

    return true;
}

} // namespace FirmwareFiles
