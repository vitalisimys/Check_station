#ifndef ANALYZER_CONTROLLER_H
#define ANALYZER_CONTROLLER_H

#include <QObject>
#include <QThread>
#include <QElapsedTimer>

class AnalyzerWorker;

class AnalyzerController final : public QObject
{
    Q_OBJECT
public:
    explicit AnalyzerController(QObject *parent = nullptr);
    ~AnalyzerController() override;

    void connectToDefaultPort();
    void disconnectFromPort();

signals:
    void analyzerConnected();
    void analyzerDisconnected(const QString &reason);
    void logMessage(const QString &msg);

private:
    QThread m_thread;
    AnalyzerWorker *m_worker = nullptr;
};

#endif // ANALYZER_CONTROLLER_H

