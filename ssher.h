#ifndef SSHER_H
#define SSHER_H

#include <QObject>
#include <QString>

struct _LIBSSH2_SESSION;
typedef struct _LIBSSH2_SESSION LIBSSH2_SESSION;

class SSHer : public QObject
{
    Q_OBJECT

public:
    explicit SSHer(QObject *parent = nullptr);
    ~SSHer();

    void cleanup();
    bool connectToHost(const QString &host, int port = 22);
    bool authenticate(const QString &username, const QString &password);
    QString executeCommand(const QString &command, int *exitCode = nullptr);
    bool downloadFile(const QString &remoteFilePath, const QString &localFilePath);
    bool uploadFile(const QString &localFilePath, const QString &remoteFilePath, int permissions = 0644);

    void setAllowLegacyAlgorithms(bool enabled);
    QString hostKeyFingerprintSha256() const;
    bool isConnected() const;
    QString lastError() const;

signals:
    void logMessage(const QString &message, const QString &color);

private:
    int createAndConnectSocket(const QString &host, int port);
    QString sessionError(const QString &prefix) const;
    void setError(const QString &message);

    LIBSSH2_SESSION *m_session;
    int m_sock;
    bool m_libssh2Initialized;
    bool m_allowLegacyAlgorithms;
    QString m_lastError;
};

#endif // SSHER_H
