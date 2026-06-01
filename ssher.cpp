#include "ssher.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
QMutex g_libssh2Mutex;
int g_libssh2RefCount = 0;
}

SSHer::SSHer(QObject *parent)
    : QObject(parent)
    , m_session(nullptr)
    , m_sock(-1)
    , m_libssh2Initialized(false)
    , m_allowLegacyAlgorithms(false)
{
    QMutexLocker lock(&g_libssh2Mutex);
    if (g_libssh2RefCount == 0) {
        const int initRc = libssh2_init(0);
        if (initRc != 0) {
            m_lastError = QString("Ошибка инициализации libssh2 (код %1)").arg(initRc);
            emit logMessage(m_lastError, "red");
            return;
        }
    }

    ++g_libssh2RefCount;
    m_libssh2Initialized = true;
}

SSHer::~SSHer()
{
    cleanup();

    QMutexLocker lock(&g_libssh2Mutex);
    if (m_libssh2Initialized && g_libssh2RefCount > 0) {
        --g_libssh2RefCount;
        if (g_libssh2RefCount == 0) {
            libssh2_exit();
        }
    }
}

void SSHer::setAllowLegacyAlgorithms(bool enabled)
{
    m_allowLegacyAlgorithms = enabled;
}

bool SSHer::isConnected() const
{
    return (m_session != nullptr) && (m_sock >= 0);
}

QString SSHer::lastError() const
{
    return m_lastError;
}

QString SSHer::hostKeyFingerprintSha256() const
{
    if (!m_session) {
        return QString();
    }

    const char *hash = libssh2_hostkey_hash(m_session, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (!hash) {
        return QString();
    }

    static constexpr int sha256Size = 32;
    const QByteArray rawHash(hash, sha256Size);
    return QString::fromLatin1(rawHash.toHex(':'));
}

void SSHer::setError(const QString &message)
{
    m_lastError = message;
    emit logMessage(m_lastError, "red");
}

QString SSHer::sessionError(const QString &prefix) const
{
    if (!m_session) {
        return prefix;
    }

    char *errMsg = nullptr;
    const int errCode = libssh2_session_last_error(m_session, &errMsg, nullptr, 0);
    const QString details = errMsg ? QString::fromUtf8(errMsg) : QString("неизвестная ошибка");
    return QString("%1 (код %2): %3").arg(prefix).arg(errCode).arg(details);
}

int SSHer::createAndConnectSocket(const QString &host, int port)
{
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *result = nullptr;
    const QByteArray hostUtf8 = host.toUtf8();
    const QByteArray portUtf8 = QByteArray::number(port);
    const int gaiRc = getaddrinfo(hostUtf8.constData(), portUtf8.constData(), &hints, &result);
    if (gaiRc != 0) {
        setError(QString("Ошибка разрешения адреса %1: %2")
                     .arg(host, QString::fromUtf8(gai_strerror(gaiRc))));
        return -1;
    }

    int socketFd = -1;
    QString lastConnectError;
    for (addrinfo *entry = result; entry != nullptr; entry = entry->ai_next) {
        socketFd = ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        if (socketFd < 0) {
            lastConnectError = QString::fromUtf8(std::strerror(errno));
            continue;
        }

        if (::connect(socketFd, entry->ai_addr, entry->ai_addrlen) == 0) {
            freeaddrinfo(result);
            return socketFd;
        }

        lastConnectError = QString::fromUtf8(std::strerror(errno));
        ::close(socketFd);
        socketFd = -1;
    }

    freeaddrinfo(result);
    setError(QString("Ошибка подключения к %1:%2: %3")
                 .arg(host)
                 .arg(port)
                 .arg(lastConnectError.isEmpty() ? QString("неизвестная ошибка") : lastConnectError));
    return -1;
}

void SSHer::cleanup()
{
    if (m_session) {
        libssh2_session_disconnect(m_session, "Normal shutdown");
        libssh2_session_free(m_session);
        m_session = nullptr;
    }

    if (m_sock >= 0) {
        ::close(m_sock);
        m_sock = -1;
    }
}

bool SSHer::connectToHost(const QString &host, int port)
{
    if (!m_libssh2Initialized) {
        setError("libssh2 не инициализирована.");
        return false;
    }

    cleanup();
    m_lastError.clear();

    m_sock = createAndConnectSocket(host, port);
    if (m_sock < 0) {
        return false;
    }

    m_session = libssh2_session_init_ex(nullptr, nullptr, nullptr, nullptr);
    if (!m_session) {
        setError("Не удалось создать SSH-сессию.");
        cleanup();
        return false;
    }

    libssh2_session_set_blocking(m_session, 1);

    if (m_allowLegacyAlgorithms) {
        static const char *hostKeys = "rsa-sha2-512,rsa-sha2-256,ssh-rsa,ssh-dss";
        static const char *kex = "curve25519-sha256,diffie-hellman-group14-sha256,diffie-hellman-group14-sha1,diffie-hellman-group1-sha1";
        libssh2_session_method_pref(m_session, LIBSSH2_METHOD_HOSTKEY, hostKeys);
        libssh2_session_method_pref(m_session, LIBSSH2_METHOD_KEX, kex);
    }

    const int handshakeRc = libssh2_session_handshake(m_session, m_sock);
    if (handshakeRc != 0) {
        setError(sessionError(QString("Ошибка SSH handshake для %1:%2").arg(host).arg(port)));
        cleanup();
        return false;
    }

    emit logMessage(QString("SSH соединение установлено с %1:%2").arg(host).arg(port), "green");
    const QString fingerprint = hostKeyFingerprintSha256();
    if (!fingerprint.isEmpty()) {
        emit logMessage(QString("SHA256 host key fingerprint: %1").arg(fingerprint), "gray");
    }
    return true;
}

bool SSHer::authenticate(const QString &username, const QString &password)
{
    if (!m_session) {
        setError("SSH сессия не инициализирована.");
        return false;
    }

    const QByteArray userUtf8 = username.toUtf8();
    const QByteArray passUtf8 = password.toUtf8();
    const int authRc = libssh2_userauth_password(m_session, userUtf8.constData(), passUtf8.constData());
    if (authRc != 0) {
        setError(sessionError(QString("Ошибка аутентификации пользователя %1").arg(username)));
        return false;
    }

    emit logMessage(QString("Аутентификация прошла успешно: %1").arg(username), "green");
    return true;
}

QString SSHer::executeCommand(const QString &command, int *exitCode)
{
    if (!m_session) {
        setError("SSH сессия не инициализирована.");
        return QString();
    }

    LIBSSH2_CHANNEL *channel = libssh2_channel_open_session(m_session);
    if (!channel) {
        setError(sessionError("Не удалось открыть SSH канал."));
        return QString();
    }

    const QByteArray cmdUtf8 = command.toUtf8();
    if (libssh2_channel_exec(channel, cmdUtf8.constData()) != 0) {
        setError(sessionError(QString("Не удалось выполнить команду: %1").arg(command)));
        libssh2_channel_close(channel);
        libssh2_channel_free(channel);
        return QString();
    }

    QByteArray output;
    QByteArray errorOutput;
    char buffer[4096];

    while (true) {
        const ssize_t outBytes = libssh2_channel_read(channel, buffer, sizeof(buffer));
        if (outBytes > 0) {
            output.append(buffer, static_cast<int>(outBytes));
        } else if (outBytes < 0 && outBytes != LIBSSH2_ERROR_EAGAIN) {
            setError(sessionError("Ошибка чтения stdout команды."));
            libssh2_channel_close(channel);
            libssh2_channel_free(channel);
            return QString();
        }

        const ssize_t errBytes = libssh2_channel_read_stderr(channel, buffer, sizeof(buffer));
        if (errBytes > 0) {
            errorOutput.append(buffer, static_cast<int>(errBytes));
        } else if (errBytes < 0 && errBytes != LIBSSH2_ERROR_EAGAIN) {
            setError(sessionError("Ошибка чтения stderr команды."));
            libssh2_channel_close(channel);
            libssh2_channel_free(channel);
            return QString();
        }

        if (outBytes == 0 && errBytes == 0 && libssh2_channel_eof(channel)) {
            break;
        }

        // Если данных пока нет (а EOF не выставлен) — не сжигаем CPU в busy-loop.
        if (outBytes <= 0 && errBytes <= 0) {
            usleep(2000); // 2 мс
        }
    }

    libssh2_channel_send_eof(channel);
    libssh2_channel_wait_eof(channel);
    libssh2_channel_wait_closed(channel);

    if (exitCode) {
        *exitCode = libssh2_channel_get_exit_status(channel);
    }

    libssh2_channel_free(channel);

    if (!errorOutput.isEmpty()) {
        output.append("\n[stderr]\n");
        output.append(errorOutput);
    }

    return QString::fromUtf8(output);
}

bool SSHer::downloadFile(const QString &remoteFilePath, const QString &localFilePath)
{
    if (!m_session) {
        setError("SSH сессия не инициализирована.");
        return false;
    }

    LIBSSH2_SFTP *sftp = libssh2_sftp_init(m_session);
    if (!sftp) {
        setError(sessionError("Не удалось создать SFTP сессию."));
        return false;
    }

    const QByteArray remoteUtf8 = remoteFilePath.toUtf8();
    LIBSSH2_SFTP_HANDLE *remoteFile = libssh2_sftp_open(
        sftp,
        remoteUtf8.constData(),
        LIBSSH2_FXF_READ,
        0);

    if (!remoteFile) {
        const unsigned long sftpErr = libssh2_sftp_last_error(sftp);
        libssh2_sftp_shutdown(sftp);
        setError(QString("Не удалось открыть удаленный файл %1 (SFTP код %2)")
                     .arg(remoteFilePath)
                     .arg(sftpErr));
        return false;
    }

    const QFileInfo localInfo(localFilePath);
    if (!localInfo.absolutePath().isEmpty()) {
        QDir().mkpath(localInfo.absolutePath());
    }

    QFile localFile(localFilePath);
    if (!localFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        libssh2_sftp_close(remoteFile);
        libssh2_sftp_shutdown(sftp);
        setError(QString("Не удалось открыть локальный файл %1 для записи").arg(localFilePath));
        return false;
    }

    char buffer[8192];
    while (true) {
        const ssize_t bytesRead = libssh2_sftp_read(remoteFile, buffer, sizeof(buffer));
        if (bytesRead > 0) {
            const qint64 bytesWritten = localFile.write(buffer, bytesRead);
            if (bytesWritten != bytesRead) {
                localFile.close();
                libssh2_sftp_close(remoteFile);
                libssh2_sftp_shutdown(sftp);
                setError(QString("Ошибка записи локального файла %1").arg(localFilePath));
                return false;
            }
        } else if (bytesRead == 0) {
            break;
        } else {
            localFile.close();
            libssh2_sftp_close(remoteFile);
            libssh2_sftp_shutdown(sftp);
            setError(QString("Ошибка чтения удаленного файла %1").arg(remoteFilePath));
            return false;
        }
    }

    localFile.close();
    libssh2_sftp_close(remoteFile);
    libssh2_sftp_shutdown(sftp);
    return true;
}

bool SSHer::uploadFile(const QString &localFilePath, const QString &remoteFilePath, int permissions)
{
    if (!m_session) {
        setError("SSH сессия не инициализирована.");
        return false;
    }

    QFile localFile(localFilePath);
    if (!localFile.open(QIODevice::ReadOnly)) {
        setError(QString("Не удалось открыть локальный файл %1").arg(localFilePath));
        return false;
    }

    LIBSSH2_SFTP *sftp = libssh2_sftp_init(m_session);
    if (!sftp) {
        localFile.close();
        setError(sessionError("Не удалось создать SFTP сессию."));
        return false;
    }

    const QByteArray remoteUtf8 = remoteFilePath.toUtf8();
    LIBSSH2_SFTP_HANDLE *remoteFile = libssh2_sftp_open(
        sftp,
        remoteUtf8.constData(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        permissions);

    if (!remoteFile) {
        const unsigned long sftpErr = libssh2_sftp_last_error(sftp);
        localFile.close();
        libssh2_sftp_shutdown(sftp);
        setError(QString("Не удалось открыть удаленный файл %1 для записи (SFTP код %2)")
                     .arg(remoteFilePath)
                     .arg(sftpErr));
        return false;
    }

    char buffer[8192];
    while (true) {
        const qint64 bytesRead = localFile.read(buffer, sizeof(buffer));
        if (bytesRead > 0) {
            qint64 offset = 0;
            while (offset < bytesRead) {
                const ssize_t bytesWritten = libssh2_sftp_write(
                    remoteFile,
                    buffer + offset,
                    static_cast<size_t>(bytesRead - offset));

                if (bytesWritten <= 0) {
                    localFile.close();
                    libssh2_sftp_close(remoteFile);
                    libssh2_sftp_shutdown(sftp);
                    setError(QString("Ошибка записи удаленного файла %1").arg(remoteFilePath));
                    return false;
                }

                offset += bytesWritten;
            }
        } else if (bytesRead == 0) {
            break;
        } else {
            localFile.close();
            libssh2_sftp_close(remoteFile);
            libssh2_sftp_shutdown(sftp);
            setError(QString("Ошибка чтения локального файла %1").arg(localFilePath));
            return false;
        }
    }

    localFile.close();
    libssh2_sftp_close(remoteFile);
    libssh2_sftp_shutdown(sftp);
    return true;
}
