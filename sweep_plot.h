#pragma once

#include "qcustomplot.h"
#include <QVector>

struct SweepPlotTraces {
    /// Нижняя граница заливки «полигона» (невидимая линия по нижней границе оси Y).
    QCPGraph *fillBaselineGraph = nullptr;
    QCPGraph *liveTrace = nullptr;
    QCPGraph *memoryTrace = nullptr;
};

void setupFrequencySweepPlot(QCustomPlot *plot, double xMinMHz, double xMaxMHz);

SweepPlotTraces createSweepTraces(QCustomPlot *plot);

/// Полное разрешение: обновляет только буфер max-hold
void accumulateSpectrumMemory(QVector<double> &memoryMaxDbm,
                              const QVector<double> &frequenciesMHz,
                              const QVector<double> &amplitudesDbm);

/// Отрисовка: даунсэмплинг по видимому диапазону + лимит точек
void updateSweepSpectrumVisual(SweepPlotTraces &traces,
                               const QVector<double> &frequenciesMHz,
                               const QVector<double> &amplitudesDbm,
                               bool showMemory,
                               const QVector<double> &memoryMaxDbm,
                               QCustomPlot *plot,
                               int maxPointsPerTrace);
