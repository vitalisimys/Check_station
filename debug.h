#ifndef DEBUG_H
#define DEBUG_H

#include <QDebug>
#include <QString>
#include <functional>

extern bool debug;
extern QString fullLog;

#define DEBUG if (debug) qDebug()

/// Привязка журнала Qt (qDebug/qInfo/…) к виджету logTextEdit. nullptr — отключить (на выходе из MainWindow).
void setApplicationLogTextSink(std::function<void(const QString &)> sink);

#endif // DEBUG_H
