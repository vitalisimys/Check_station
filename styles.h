#ifndef STYLES_H
#define STYLES_H

#include <QColor>
#include <QString>

namespace PlotPalette {

inline const QColor bgCanvas = QColor(QStringLiteral("#0f172a"));
inline const QColor bgAxisArea = QColor(QStringLiteral("#1e293b"));

inline const QColor gridMajor = QColor(QStringLiteral("#334155"));
inline const QColor gridMinor = QColor(QStringLiteral("#1e293b"));
inline const QColor axisLine = QColor(QStringLiteral("#475569"));
inline const QColor axisText = QColor(QStringLiteral("#94a3b8"));
inline const QColor axisLabel = QColor(QStringLiteral("#64748b"));

inline const QColor traceLive = QColor(QStringLiteral("#4ade80"));
inline const QColor traceMemory = QColor(QStringLiteral("#86efac"));
inline const QColor selection = QColor(QStringLiteral("#60a5fa"));

constexpr double alphaMemory = 0.35;
constexpr double alphaGrid = 0.4;

} // namespace PlotPalette

const QString styleSheetSpectrumBwSlider = R"(
#horizontalSliderBW {
    min-height: 28px;
}
#horizontalSliderBW::groove:horizontal {
    height: 6px;
    background: #334155;
    border-radius: 3px;
    border: 1px solid #475569;
}
#horizontalSliderBW::sub-page:horizontal {
    background: #1e40af;
    border-radius: 3px;
}
#horizontalSliderBW::add-page:horizontal {
    background: #334155;
    border-radius: 3px;
}
#horizontalSliderBW::handle:horizontal {
    background: #3b82f6;
    width: 16px;
    height: 16px;
    margin: -6px 0;
    border-radius: 8px;
    border: 1px solid #60a5fa;
}
#horizontalSliderBW::handle:horizontal:hover {
    background: #60a5fa;
    border-color: #93c5fd;
}
#horizontalSliderBW::handle:horizontal:pressed {
    background: #2563eb;
}
)";

const QString styleSheetConnectStation = R"(
    #frameStation {
        color: #10b981;
        border-radius: 8px;
        border: 2px solid #8AE08A;
        background-color: #1e293b;
        font-family: \"Consolas\";
    }
)";

const QString styleSheetDisconnectStation = R"(
    #frameStation {
        color: #94a3b8;
        border-radius: 8px;
        border: 2px solid #ff5252;
        background-color: #1e293b;
        font-family: \"Consolas\";
    }
)";

const QString styleSheetConnectAnalyzer = R"(
    #frameR3 {
        color: #10b981;
        border-radius: 8px;
        border: 2px solid #8AE08A;
        background-color: #1e293b;
        font-family: \"Consolas\";
    }
)";

const QString styleSheetDisconnectAnalyzer = R"(
    #frameR3 {
        color: #94a3b8;
        border-radius: 8px;
        border: 2px solid #ff5252;
        background-color: #1e293b;
        font-family: \"Consolas\";
    }
)";

const QString stylesheetButtonMessBox = R"(
    QMessageBox QPushButton {
        background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4C4C4C, stop:1 #333333);
        color: #FFFFFF;
        border: none;
        border-radius: 8px;
        padding: 5px 10px;
    }

    QMessageBox QPushButton:hover {
        background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5A5A5A, stop:1 #444444);
    }

    QMessageBox QPushButton:pressed {
        background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #222222, stop:1 #1A1A1A);
    }
)";

const QString stylesheetMessBox = R"(
    QMessageBox {
        background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #333333, stop:1 #1A1A1A);
        color: #333;
    }

    QMessageBox QLabel {
        color: #d32f2f;
        font-weight: bold;
    }

    QMessageBox QPushButton {
        background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4C4C4C, stop:1 #333333);
        color: #FFFFFF;
        border: none;
        border-radius: 8px;
        padding: 5px 10px;
    }

    QMessageBox QPushButton:hover {
        background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5A5A5A, stop:1 #444444);
    }

    QMessageBox QPushButton:pressed {
        background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #222222, stop:1 #1A1A1A);
    }
)";

#endif // STYLES_H
