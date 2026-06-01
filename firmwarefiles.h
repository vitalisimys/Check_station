#ifndef FIRMWAREFILES_H
#define FIRMWAREFILES_H

#include <QDir>
#include <QString>
#include <QStringList>

namespace FirmwareFiles {

QString directory();
bool hasRequiredFirmwareFiles(const QDir &dir);
QString findByPrefix(const QDir &dir, const QString &prefix);
QString resolveTftpRequestPath(const QDir &dir, const QString &requestedName);
bool ensureCanonicalFile(QDir &dir, const QString &prefix, const QString &canonicalName, QString *errorText = nullptr);
bool prepareForTftp(QString *errorText = nullptr);

} // namespace FirmwareFiles

#endif // FIRMWAREFILES_H
