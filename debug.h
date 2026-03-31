#ifndef DEBUG_H
#define DEBUG_H

#include <QDebug>
#include <QString>

extern bool debug;
extern QString fullLog;

#ifdef DEBUG_ENABLED
#define DEBUG if (debug) qDebug()
#else
#define DEBUG if (false) qDebug()
#endif

#endif // DEBUG_H
