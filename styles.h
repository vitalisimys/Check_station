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

/// Рамка выбора тракта (ППМ) в шапке — в тон общему стилю frameStation / вкладок.
const QString styleSheetFramePpm = R"(
    #framePPM {
        color: #e2e8f0;
        border-radius: 8px;
        border: 2px solid #334155;
        background-color: #1e293b;
        font-family: \"Consolas\";
        padding: 2px 8px;
    }
)";

const QString styleSheetPpmRadioON = R"(
QRadioButton {
        color: #e2e8f0;
        font-size: 8pt;
        font-weight: bold;
        spacing: 8px;
        background: transparent;
    }
    QRadioButton::indicator {
        width: 16px;
        height: 16px;
        border-radius: 8px;
        border:3px solid #10b981;
        background: #10b981;
    }
    QRadioButton::indicator:hover {
        border-color: #3b82f6;
    }
    QRadioButton::indicator:checked {
        background: #10b981;
    }
)";

const QString styleSheetPpmRadioOFF = R"(
   QRadioButton {
        color: #e2e8f0;
        font-size: 8pt;
        font-weight: bold;
        spacing: 8px;
        background: transparent;
    }
    QRadioButton::indicator {
        width: 16px;
        height: 16px;
        border-radius: 8px;
        border:3px solid #FFFF99;
        background: #FFFF99;
    }
    QRadioButton::indicator:hover {
        border-color: #3b82f6;
    }
    QRadioButton::indicator:checked {
        background: #FFFF99;
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
