#include "sweep_plot.h"
#include "styles.h"

#include <QBrush>
#include <QFont>
#include <QLinearGradient>
#include <QPen>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace {

QFont monoFont(int pointSize, bool bold = false)
{
    QFont f(QStringLiteral("Consolas"), pointSize);
    if (bold) {
        f.setBold(true);
    }
    if (!f.exactMatch()) {
        f = QFont(QStringLiteral("monospace"), pointSize);
        f.setStyleHint(QFont::TypeWriter);
        f.setBold(bold);
    }
    return f;
}

struct IndexRange {
    int lo = 0;
    int hi = -1;
};

bool freqAxisAscending(const QVector<double> &f)
{
    if (f.size() < 2) {
        return true;
    }
    return f.first() <= f.last();
}

IndexRange visibleSampleRange(const QVector<double> &f, double xMin, double xMax)
{
    const int n = f.size();
    IndexRange r;
    if (n == 0 || xMax < xMin) {
        return r;
    }
    if (freqAxisAscending(f)) {
        auto itL = std::lower_bound(f.begin(), f.end(), xMin);
        auto itU = std::upper_bound(f.begin(), f.end(), xMax);
        if (itL == f.end() || itU == f.begin()) {
            return r;
        }
        r.lo = int(itL - f.begin());
        r.hi = int(itU - f.begin()) - 1;
        r.lo = qBound(0, r.lo, n - 1);
        r.hi = qBound(0, r.hi, n - 1);
        if (r.lo > r.hi) {
            return r;
        }
        return r;
    }
    r.hi = -1;
    for (int i = 0; i < n; ++i) {
        if (f[i] >= xMin && f[i] <= xMax) {
            if (r.hi < 0) {
                r.lo = i;
            }
            r.hi = i;
        }
    }
    return r;
}

void downsampleVisible(const QVector<double> &f,
                       const QVector<double> &yLive,
                       const QVector<double> &yMem,
                       bool useMem,
                       int iLo,
                       int iHi,
                       int maxOut,
                       QVector<double> &outF,
                       QVector<double> &outLive,
                       QVector<double> *outMem)
{
    const int m = iHi - iLo + 1;
    if (m <= 0) {
        outF.clear();
        outLive.clear();
        if (outMem) {
            outMem->clear();
        }
        return;
    }
    if (m <= maxOut) {
        outF.resize(m);
        outLive.resize(m);
        for (int i = 0; i < m; ++i) {
            outF[i] = f[iLo + i];
            outLive[i] = yLive[iLo + i];
        }
        if (outMem && useMem && yMem.size() == f.size()) {
            outMem->resize(m);
            for (int i = 0; i < m; ++i) {
                (*outMem)[i] = yMem[iLo + i];
            }
        } else if (outMem) {
            outMem->clear();
        }
        return;
    }

    const int buckets = maxOut;
    outF.resize(buckets);
    outLive.resize(buckets);
    if (outMem && useMem && yMem.size() == f.size()) {
        outMem->resize(buckets);
    } else if (outMem) {
        outMem->clear();
    }

    for (int b = 0; b < buckets; ++b) {
        const int j0 = iLo + static_cast<int>((static_cast<std::int64_t>(b) * m) / buckets);
        const int j1 = iLo + static_cast<int>((static_cast<std::int64_t>(b + 1) * m) / buckets);
        const int jEnd = qMax(j0, j1 - 1);
        outF[b] = 0.5 * (f[j0] + f[jEnd]);
        double maxL = yLive[j0];
        for (int j = j0 + 1; j < j1; ++j) {
            maxL = std::max(maxL, yLive[j]);
        }
        outLive[b] = maxL;
        if (outMem && useMem && yMem.size() == f.size()) {
            double maxM = yMem[j0];
            for (int j = j0 + 1; j < j1; ++j) {
                maxM = std::max(maxM, yMem[j]);
            }
            (*outMem)[b] = maxM;
        }
    }
}

} // namespace

void setupFrequencySweepPlot(QCustomPlot *plot, double xMinMHz, double xMaxMHz)
{
    if (!plot) {
        return;
    }

    // Премиальный фон: лёгкий вертикальный градиент от bgCanvas к bgAxisArea,
    // чтобы график «дышал» и не выглядел плоско-чёрным.
    QLinearGradient bgGrad(0, 0, 0, 1);
    bgGrad.setCoordinateMode(QGradient::ObjectBoundingMode);
    QColor topBg = PlotPalette::bgCanvas;
    QColor botBg = PlotPalette::bgAxisArea;
    bgGrad.setColorAt(0.0, topBg);
    bgGrad.setColorAt(1.0, botBg);
    plot->setBackground(QBrush(bgGrad));

    plot->setAutoAddPlottableToLegend(false);

    plot->setPlottingHints(QCP::phFastPolylines);
    plot->setNotAntialiasedElements(QCP::aeNone);
    plot->setNoAntialiasingOnDrag(true);

    auto setupAxis = [plot](QCPAxis *axis, const QString &label, double lower, double upper) {
        axis->setRange(lower, upper);
        axis->setBasePen(QPen(PlotPalette::axisLine, 1));
        axis->setTickPen(QPen(PlotPalette::axisLine, 1));
        axis->setSubTickPen(QPen(PlotPalette::gridMajor, 1));
        axis->setTickLabelColor(PlotPalette::axisText);
        axis->setLabelColor(PlotPalette::axisLabel);
        axis->setLabel(label);
        axis->setTickLabelFont(monoFont(9));
        axis->setLabelFont(monoFont(10, true));
        axis->setTickLengthIn(0);
        axis->setTickLengthOut(4);
        axis->setSubTickLengthIn(0);
        axis->setSubTickLengthOut(2);
        axis->setPadding(2);

        QPen majorPen(PlotPalette::gridMajor);
        majorPen.setStyle(Qt::SolidLine);
        QColor gmaj(PlotPalette::gridMajor);
        gmaj.setAlphaF(PlotPalette::alphaGrid);
        majorPen.setColor(gmaj);
        majorPen.setWidthF(1.0);

        QPen minorPen(PlotPalette::gridMinor);
        minorPen.setStyle(Qt::DotLine);
        QColor gmin(PlotPalette::gridMinor);
        gmin.setAlphaF(PlotPalette::alphaGrid * 0.55);
        minorPen.setColor(gmin);

        axis->grid()->setVisible(true);
        axis->grid()->setPen(majorPen);
        axis->grid()->setSubGridPen(minorPen);
        axis->grid()->setSubGridVisible(true);
        axis->grid()->setZeroLinePen(Qt::NoPen);
    };

    if (xMaxMHz <= xMinMHz) {
        xMaxMHz = xMinMHz + 1.0;
    }

    setupAxis(plot->xAxis, QStringLiteral("Frequency, MHz"), xMinMHz, xMaxMHz);
    setupAxis(plot->yAxis, QStringLiteral("Power, dBm"), -150.0, 20.0);

    plot->xAxis->setNumberFormat(QStringLiteral("f"));
    plot->xAxis->setNumberPrecision(3);
    plot->yAxis->setNumberFormat(QStringLiteral("f"));
    plot->yAxis->setNumberPrecision(1);

    // Двойная ось — верхняя/правая, без подписей: добавляет «рамку» инструмента
    // и поддерживает ощущение измерительного прибора.
    if (plot->xAxis2 && plot->yAxis2) {
        plot->xAxis2->setVisible(true);
        plot->yAxis2->setVisible(true);
        plot->xAxis2->setTicks(false);
        plot->yAxis2->setTicks(false);
        plot->xAxis2->setTickLabels(false);
        plot->yAxis2->setTickLabels(false);
        plot->xAxis2->setBasePen(QPen(PlotPalette::axisLine, 1));
        plot->yAxis2->setBasePen(QPen(PlotPalette::axisLine, 1));
    }

    plot->axisRect()->setMargins(QMargins(56, 14, 14, 42));
    plot->axisRect()->setBackground(QBrush(QColor(0, 0, 0, 0)));

    plot->setInteraction(QCP::iRangeDrag, true);
    plot->setInteraction(QCP::iRangeZoom, true);
    plot->setInteraction(QCP::iSelectPlottables, false);
    plot->axisRect()->setRangeDrag(Qt::Horizontal | Qt::Vertical);
    plot->axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);
}

SweepPlotTraces createSweepTraces(QCustomPlot *plot)
{
    SweepPlotTraces t;
    if (!plot) {
        return t;
    }

    t.fillBaselineGraph = plot->addGraph();
    t.fillBaselineGraph->setPen(Qt::NoPen);
    t.fillBaselineGraph->setBrush(Qt::NoBrush);
    t.fillBaselineGraph->setAdaptiveSampling(false);
    t.fillBaselineGraph->setScatterStyle(QCPScatterStyle::ssNone);

    t.liveTrace = plot->addGraph();
    t.liveTrace->setName(QStringLiteral("LIVE"));
    QPen livePen(PlotPalette::traceLive, 1.4, Qt::SolidLine);
    livePen.setCosmetic(true);
    t.liveTrace->setPen(livePen);
    t.liveTrace->setAdaptiveSampling(true);
    t.liveTrace->setScatterStyle(QCPScatterStyle::ssNone);

    QLinearGradient grad(0, 0, 0, 1);
    grad.setCoordinateMode(QGradient::ObjectBoundingMode);
    const QColor top(PlotPalette::traceLive.red(), PlotPalette::traceLive.green(), PlotPalette::traceLive.blue(), 150);
    const QColor mid(PlotPalette::traceLive.red(), PlotPalette::traceLive.green(), PlotPalette::traceLive.blue(), 60);
    const QColor bot(PlotPalette::traceLive.red(), PlotPalette::traceLive.green(), PlotPalette::traceLive.blue(), 8);
    grad.setColorAt(0.0, top);
    grad.setColorAt(0.55, mid);
    grad.setColorAt(1.0, bot);
    t.liveTrace->setBrush(QBrush(grad));
    t.liveTrace->setChannelFillGraph(t.fillBaselineGraph);

    t.memoryTrace = plot->addGraph();
    t.memoryTrace->setName(QStringLiteral("MAX HOLD"));
    QPen memPen(PlotPalette::traceMemory, 1.2, Qt::DashLine);
    memPen.setCosmetic(true);
    t.memoryTrace->setPen(memPen);
    t.memoryTrace->setBrush(Qt::NoBrush);
    t.memoryTrace->setAdaptiveSampling(true);
    t.memoryTrace->setVisible(false);

    plot->legend->setVisible(true);
    plot->legend->setFont(monoFont(8, true));
    plot->legend->setTextColor(PlotPalette::axisText);
    QColor legendBg = PlotPalette::bgAxisArea;
    legendBg.setAlpha(220);
    plot->legend->setBrush(QBrush(legendBg));
    QPen legendBorder(PlotPalette::gridMajor);
    legendBorder.setWidth(1);
    plot->legend->setBorderPen(legendBorder);
    plot->legend->setIconSize(22, 10);
    plot->legend->setRowSpacing(3);
    plot->legend->setMargins(QMargins(8, 6, 8, 6));
    // Якорим легенду в верхний правый угол, чтобы она не закрывала пик слева.
    plot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignRight);

    t.liveTrace->addToLegend();
    t.memoryTrace->addToLegend();

    return t;
}

void accumulateSpectrumMemory(QVector<double> &memoryMaxDbm,
                              const QVector<double> &frequenciesMHz,
                              const QVector<double> &amplitudesDbm)
{
    if (frequenciesMHz.isEmpty() || amplitudesDbm.size() != frequenciesMHz.size()) {
        return;
    }
    const int n = frequenciesMHz.size();
    if (memoryMaxDbm.size() != n) {
        memoryMaxDbm.resize(n);
        memoryMaxDbm.fill(-150.0);
    }
    for (int i = 0; i < n; ++i) {
        memoryMaxDbm[i] = std::max(memoryMaxDbm[i], amplitudesDbm[i]);
    }
}

void updateSweepSpectrumVisual(SweepPlotTraces &traces,
                               const QVector<double> &frequenciesMHz,
                               const QVector<double> &amplitudesDbm,
                               bool showMemory,
                               const QVector<double> &memoryMaxDbm,
                               QCustomPlot *plot,
                               int maxPointsPerTrace)
{
    if (!traces.liveTrace || !plot) {
        return;
    }
    if (frequenciesMHz.isEmpty() || amplitudesDbm.size() != frequenciesMHz.size()) {
        return;
    }

    const QCPRange xr = plot->xAxis->range();
    const double span = xr.size();
    const double pad = (span > 0.0) ? span * 0.06 : 1.0;
    const double xMin = xr.lower - pad;
    const double xMax = xr.upper + pad;

    const IndexRange ir = visibleSampleRange(frequenciesMHz, xMin, xMax);
    const int safeMax = qMax(64, maxPointsPerTrace);

    QVector<double> dispF;
    QVector<double> dispLive;
    QVector<double> dispMem;

    if (ir.hi < ir.lo) {
        dispF = frequenciesMHz;
        dispLive = amplitudesDbm;
        if (showMemory && memoryMaxDbm.size() == frequenciesMHz.size()) {
            dispMem = memoryMaxDbm;
        }
        if (dispF.size() > safeMax) {
            downsampleVisible(frequenciesMHz, amplitudesDbm, memoryMaxDbm, showMemory,
                              0, frequenciesMHz.size() - 1, safeMax,
                              dispF, dispLive, showMemory ? &dispMem : nullptr);
        }
    } else {
        downsampleVisible(frequenciesMHz, amplitudesDbm, memoryMaxDbm, showMemory,
                          ir.lo, ir.hi, safeMax,
                          dispF, dispLive, showMemory ? &dispMem : nullptr);
    }

    traces.liveTrace->setData(dispF, dispLive);

    if (traces.fillBaselineGraph && !dispF.isEmpty()) {
        const double y0 = plot->yAxis->range().lower;
        QVector<double> baselineY(dispF.size(), y0);
        traces.fillBaselineGraph->setData(dispF, baselineY);
    }

    if (showMemory && traces.memoryTrace && memoryMaxDbm.size() == frequenciesMHz.size()) {
        if (!dispMem.isEmpty() && dispMem.size() == dispF.size()) {
            traces.memoryTrace->setData(dispF, dispMem);
        } else {
            traces.memoryTrace->setData(frequenciesMHz, memoryMaxDbm);
        }
        traces.memoryTrace->setVisible(true);
    } else if (traces.memoryTrace) {
        traces.memoryTrace->setVisible(false);
    }

    plot->replot(QCustomPlot::rpQueuedReplot);
}
