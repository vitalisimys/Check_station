#ifndef DEBUG_H
#define DEBUG_H

#include <QDebug>
#include <QString>

extern bool debug;
extern QString fullLog;

#define DEBUG if (debug) qDebug()

#endif // DEBUG_H
