#ifndef STYLES_H
#define STYLES_H

#include <QColor>
#include <QString>

// =====================================================================================
//  Check_station — единая дизайн-система GUI
// -------------------------------------------------------------------------------------
//  Дизайн-токены (палитра, радиусы, типографика) собраны в одном месте, чтобы стиль
//  приложения был согласованным и предсказуемым, а быстрые правки не требовали ручного
//  обхода десятков .ui-блоков. Локальные стили в .ui сохраняют приоритет — этот файл
//  задаёт уровень приложения (qApp->setStyleSheet) и стили динамических состояний.
// =====================================================================================

/// Семантическая палитра приложения. Все цвета — единая система:
/// фон (canvas/surface), границы, текст, акценты и сигнальные цвета.
namespace AppPalette {

// Поверхности
inline const QColor canvas       = QColor(QStringLiteral("#0b1220")); // самый тёмный фон (за вкладками, под графиком)
inline const QColor surface      = QColor(QStringLiteral("#0f172a")); // фон вкладки/диалога
inline const QColor surfaceAlt   = QColor(QStringLiteral("#111c33")); // карточка/панель
inline const QColor surfaceRaised = QColor(QStringLiteral("#1e293b")); // приподнятая карточка / шапка
inline const QColor surfaceHover = QColor(QStringLiteral("#243349")); // hover

// Границы
inline const QColor borderSubtle = QColor(QStringLiteral("#1f2a44"));
inline const QColor border       = QColor(QStringLiteral("#334155"));
inline const QColor borderStrong = QColor(QStringLiteral("#475569"));
inline const QColor focus        = QColor(QStringLiteral("#3b82f6"));

// Текст
inline const QColor textPrimary   = QColor(QStringLiteral("#e2e8f0"));
inline const QColor textSecondary = QColor(QStringLiteral("#94a3b8"));
inline const QColor textMuted     = QColor(QStringLiteral("#64748b"));
inline const QColor textDisabled  = QColor(QStringLiteral("#475569"));

// Акцент (синий)
inline const QColor accent        = QColor(QStringLiteral("#3b82f6"));
inline const QColor accentHover   = QColor(QStringLiteral("#2563eb"));
inline const QColor accentPressed = QColor(QStringLiteral("#1d4ed8"));
inline const QColor accentSoft    = QColor(QStringLiteral("#1e3a8a"));

// Сигнальные цвета (стандартизованные)
inline const QColor success       = QColor(QStringLiteral("#22c55e")); // норма
inline const QColor successDeep   = QColor(QStringLiteral("#15803d"));
inline const QColor successSoft   = QColor(QStringLiteral("#14532d"));

inline const QColor warning       = QColor(QStringLiteral("#f59e0b")); // ожидание / предупреждение
inline const QColor warningSoft   = QColor(QStringLiteral("#78350f"));

inline const QColor danger        = QColor(QStringLiteral("#ef4444")); // авария / ошибка
inline const QColor dangerDeep    = QColor(QStringLiteral("#b91c1c"));
inline const QColor dangerSoft    = QColor(QStringLiteral("#7f1d1d"));

inline const QColor info          = QColor(QStringLiteral("#38bdf8")); // активное действие / выбор
inline const QColor infoSoft      = QColor(QStringLiteral("#075985"));

inline const QColor neutral       = QColor(QStringLiteral("#64748b")); // нейтральное / недоступно

// Цвета режимов передачи (как в пульте)
inline const QColor txActive      = QColor(QStringLiteral("#ec4899")); // активная передача (TRAKT_TX_WRK)
inline const QColor rxActive      = QColor(QStringLiteral("#22c55e")); // активный приём (TRAKT_RX_WRK)
inline const QColor waitActive    = QColor(QStringLiteral("#3b82f6")); // промежуточное ожидание (TRAKT_WAIT)

} // namespace AppPalette

/// Палитра для QCustomPlot — производная от AppPalette, с учётом читаемости графиков.
namespace PlotPalette {

inline const QColor bgCanvas   = QColor(QStringLiteral("#0b1220"));
inline const QColor bgAxisArea = QColor(QStringLiteral("#0f172a"));

inline const QColor gridMajor  = QColor(QStringLiteral("#27324a"));
inline const QColor gridMinor  = QColor(QStringLiteral("#1a2336"));
inline const QColor axisLine   = QColor(QStringLiteral("#475569"));
inline const QColor axisText   = QColor(QStringLiteral("#cbd5e1"));
inline const QColor axisLabel  = QColor(QStringLiteral("#94a3b8"));

inline const QColor traceLive   = QColor(QStringLiteral("#22d3ee")); // cyan-400 — выразительнее, лучше читается
inline const QColor traceMemory = QColor(QStringLiteral("#a78bfa")); // violet-400 — max-hold
inline const QColor selection   = QColor(QStringLiteral("#60a5fa"));

// Зоны нормы/предупреждения/аварии для графа мощности
inline const QColor zoneOk      = QColor(QStringLiteral("#22c55e"));
inline const QColor zoneWarn    = QColor(QStringLiteral("#f59e0b"));
inline const QColor zoneAlarm   = QColor(QStringLiteral("#ef4444"));

constexpr double alphaMemory = 0.35;
constexpr double alphaGrid   = 0.55;

} // namespace PlotPalette

// =====================================================================================
//  Глобальный stylesheet приложения (qApp->setStyleSheet)
// -------------------------------------------------------------------------------------
//  Задаёт стиль элементов, не имеющих локального QSS в .ui (tooltip, menu, scrollbar,
//  диалоги, статус-бар и т.д.), и формирует «общий вкус» приложения.
// =====================================================================================
inline QString buildAppStyleSheet()
{
    return QString::fromUtf8(R"(
/* -------- Базовый фон/текст -------- */
QWidget {
    color: #e2e8f0;
    selection-background-color: #1e40af;
    selection-color: #f8fafc;
}

QMainWindow, QDialog {
    background-color: #0f172a;
}

/* -------- Меню и панели -------- */
QMenuBar {
    background-color: #0f172a;
    color: #cbd5e1;
    border-bottom: 1px solid #1f2a44;
    padding: 2px 6px;
}
QMenuBar::item {
    background: transparent;
    padding: 4px 10px;
    border-radius: 4px;
}
QMenuBar::item:selected {
    background: #1e293b;
    color: #f8fafc;
}

QMenu {
    background-color: #0f172a;
    color: #e2e8f0;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 4px;
}
QMenu::item {
    padding: 6px 18px 6px 12px;
    border-radius: 4px;
}
QMenu::item:selected {
    background-color: #1e293b;
    color: #f8fafc;
}
QMenu::separator {
    height: 1px;
    background: #1f2a44;
    margin: 4px 8px;
}

QStatusBar {
    background-color: #0f172a;
    color: #94a3b8;
    border-top: 1px solid #1f2a44;
}
QStatusBar::item { border: none; }

/* -------- Tooltip — premium look -------- */
QToolTip {
    background-color: #1e293b;
    color: #e2e8f0;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 6px 8px;
    font-family: "Consolas", "DejaVu Sans Mono", monospace;
    font-size: 9pt;
}

/* -------- ScrollBar — единый slate-стиль (заменяет старые градиентные) -------- */
QScrollBar:vertical {
    background: #0f172a;
    width: 12px;
    margin: 0;
    border: none;
    border-left: 1px solid #1f2a44;
}
QScrollBar::handle:vertical {
    background: #334155;
    min-height: 28px;
    border-radius: 5px;
    margin: 2px 2px;
}
QScrollBar::handle:vertical:hover { background: #475569; }
QScrollBar::handle:vertical:pressed { background: #5b6b85; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
    background: transparent;
    border: none;
}
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }

QScrollBar:horizontal {
    background: #0f172a;
    height: 12px;
    margin: 0;
    border: none;
    border-top: 1px solid #1f2a44;
}
QScrollBar::handle:horizontal {
    background: #334155;
    min-width: 28px;
    border-radius: 5px;
    margin: 2px 2px;
}
QScrollBar::handle:horizontal:hover { background: #475569; }
QScrollBar::handle:horizontal:pressed { background: #5b6b85; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0;
    background: transparent;
    border: none;
}
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

/* -------- Заголовки QGroupBox -------- */
QGroupBox {
    color: #cbd5e1;
    border: 1px solid #334155;
    border-radius: 8px;
    margin-top: 12px;
    padding: 8px 10px;
    background-color: #111c33;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 0 6px;
    color: #94a3b8;
    font-weight: bold;
}

/* -------- QHeaderView (если когда-то появятся таблицы) -------- */
QHeaderView::section {
    background-color: #1e293b;
    color: #cbd5e1;
    border: none;
    border-right: 1px solid #1f2a44;
    border-bottom: 1px solid #1f2a44;
    padding: 6px 10px;
    font-weight: bold;
}

/* -------- QToolButton -------- */
QToolButton {
    background-color: transparent;
    color: #cbd5e1;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 4px 8px;
}
QToolButton:hover {
    background-color: #1e293b;
    border-color: #334155;
    color: #f8fafc;
}
QToolButton:pressed { background-color: #0f172a; }
QToolButton:disabled { color: #475569; }

/* -------- QCheckBox / QRadioButton (без локального стиля) -------- */
QCheckBox, QRadioButton {
    color: #e2e8f0;
    spacing: 8px;
}
QCheckBox::indicator, QRadioButton::indicator {
    width: 16px;
    height: 16px;
}
QCheckBox::indicator:unchecked, QRadioButton::indicator:unchecked {
    background: #0f172a;
    border: 1px solid #475569;
    border-radius: 4px;
}
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background: #3b82f6;
    border: 1px solid #60a5fa;
}

/* -------- Прогресс-бары без локального стиля -------- */
QProgressBar {
    border: 1px solid #1f2a44;
    border-radius: 5px;
    background-color: #0b1220;
    color: #e2e8f0;
    text-align: center;
    font-size: 9pt;
}
QProgressBar::chunk {
    border-radius: 4px;
    background-color: qlineargradient(
        x1:0, y1:0, x2:1, y2:0,
        stop:0 #2563eb,
        stop:0.5 #3b82f6,
        stop:1 #38bdf8
    );
}
)");
}

// =====================================================================================
//  Стили динамических состояний (выставляются programmatically из MainWindow)
// -------------------------------------------------------------------------------------

/// Шапка: станция/анализатор — общий стиль для "modern industrial card".
inline const QString styleSheetConnectStation = R"(
    #frameStation {
        background-color: #131f3a;
        border: 1px solid #16653a;
        border-left: 4px solid #22c55e;
        border-radius: 8px;
        padding: 2px 8px;
    }
)";

inline const QString styleSheetDisconnectStation = R"(
    #frameStation {
        background-color: #131f3a;
        border: 1px solid #7f1d1d;
        border-left: 4px solid #ef4444;
        border-radius: 8px;
        padding: 2px 8px;
    }
)";

inline const QString styleSheetConnectAnalyzer = R"(
    #frameR3 {
        background-color: #131f3a;
        border: 1px solid #16653a;
        border-left: 4px solid #22c55e;
        border-radius: 8px;
        padding: 2px 8px;
    }
)";

inline const QString styleSheetDisconnectAnalyzer = R"(
    #frameR3 {
        background-color: #131f3a;
        border: 1px solid #7f1d1d;
        border-left: 4px solid #ef4444;
        border-radius: 8px;
        padding: 2px 8px;
    }
)";

/// Кнопки в QMessageBox: единый primary-like вид.
inline const QString stylesheetButtonMessBox = R"(
    QMessageBox QPushButton {
        background-color: #1e293b;
        color: #e2e8f0;
        border: 1px solid #334155;
        border-radius: 6px;
        padding: 6px 14px;
        font-weight: bold;
        min-width: 84px;
    }
    QMessageBox QPushButton:hover {
        background-color: #243349;
        border-color: #475569;
        color: #f8fafc;
    }
    QMessageBox QPushButton:pressed {
        background-color: #0f172a;
    }
    QMessageBox QPushButton:default {
        background-color: #2563eb;
        border-color: #60a5fa;
        color: #f8fafc;
    }
    QMessageBox QPushButton:default:hover { background-color: #1d4ed8; }
)";

/// Рамка выбора тракта (ППМ) в шапке — единый стиль, без излишнего "pill".
inline const QString styleSheetFramePpm = R"(
    #framePPM {
        background-color: #131f3a;
        border: 1px solid #334155;
        border-radius: 8px;
        padding: 4px 10px;
    }
)";

inline const QString styleSheetPpmRadioON = R"(
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
        border-color: #10b981;
    }
    QRadioButton::indicator:checked {
        background: #10b981;
    }
)";

inline const QString styleSheetPpmRadioOFF = R"(
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
        border-color: #10b981;
    }
    QRadioButton::indicator:checked {
        background: #FFFF99;
    }
)";

/// Текст статуса передатчика (IND_ERROR) в labelPPMStatus.
inline const QString stylesheetPPMLabelTxOk = R"(
    color: #22c55e;
    font-family: Consolas;
    font-weight: bold;
)";

inline const QString stylesheetPPMLabelTxFault = R"(
    color: #ef4444;
    font-family: Consolas;
    font-weight: bold;
)";

inline const QString stylesheetPPMLabelTxWarning = R"(
    color: #f59e0b;
    font-family: Consolas;
    font-weight: bold;
)";

// -------------------------------------------------------------------------------------
//  Рамки framePPMStatus / frameRecievePPMStatus / framePPMStatusFHSS
//  Состояния TRAKT_* в виде "pill" с фирменным акцентом по цвету состояния.
//  Сохраняем border-radius 18 для контраста с прямоугольными карточками (статус-чип).
//  Цветовая семантика синхронизирована с AppPalette.
// -------------------------------------------------------------------------------------

inline QString makePpmStatusFrameStyle(const char *objectName, const QString &accentHex)
{
    return QStringLiteral(
               "    #%1 {\n"
               "        color: #94a3b8;\n"
               "        border-radius: 18px;\n"
               "        border: 2px solid %2;\n"
               "        background-color: #131f3a;\n"
               "        padding: 0px;\n"
               "    }\n")
        .arg(QString::fromLatin1(objectName), accentHex);
}

inline const QString stylesheetPPMFrameModeIdle    = makePpmStatusFrameStyle("framePPMStatus", QStringLiteral("#475569"));
inline const QString stylesheetPPMFrameModeWaiting = makePpmStatusFrameStyle("framePPMStatus", QStringLiteral("#f59e0b"));
inline const QString stylesheetPPMFrameModeReady   = makePpmStatusFrameStyle("framePPMStatus", QStringLiteral("#22c55e"));
inline const QString stylesheetPPMFrameModeFault   = makePpmStatusFrameStyle("framePPMStatus", QStringLiteral("#ef4444"));
inline const QString stylesheetPPMFrameModeTx      = makePpmStatusFrameStyle("framePPMStatus", QStringLiteral("#ec4899"));
inline const QString stylesheetPPMFrameModeRx      = makePpmStatusFrameStyle("framePPMStatus", QStringLiteral("#22c55e"));
inline const QString stylesheetPPMFrameModeWait    = makePpmStatusFrameStyle("framePPMStatus", QStringLiteral("#3b82f6"));

inline const QString stylesheetRecievePPMFrameModeIdle    = makePpmStatusFrameStyle("frameRecievePPMStatus", QStringLiteral("#475569"));
inline const QString stylesheetRecievePPMFrameModeWaiting = makePpmStatusFrameStyle("frameRecievePPMStatus", QStringLiteral("#f59e0b"));
inline const QString stylesheetRecievePPMFrameModeReady   = makePpmStatusFrameStyle("frameRecievePPMStatus", QStringLiteral("#22c55e"));
inline const QString stylesheetRecievePPMFrameModeFault   = makePpmStatusFrameStyle("frameRecievePPMStatus", QStringLiteral("#ef4444"));
inline const QString stylesheetRecievePPMFrameModeTx      = makePpmStatusFrameStyle("frameRecievePPMStatus", QStringLiteral("#ec4899"));
inline const QString stylesheetRecievePPMFrameModeRx      = makePpmStatusFrameStyle("frameRecievePPMStatus", QStringLiteral("#22c55e"));
inline const QString stylesheetRecievePPMFrameModeWait    = makePpmStatusFrameStyle("frameRecievePPMStatus", QStringLiteral("#3b82f6"));

/// QMessageBox — единый стиль (slate-карточка вместо градиентного "из 2010-х").
inline const QString stylesheetMessBox = R"(
    QMessageBox {
        background-color: #0f172a;
        color: #e2e8f0;
    }
    QMessageBox QLabel {
        color: #e2e8f0;
        font-weight: bold;
    }
    QMessageBox QPushButton {
        background-color: #1e293b;
        color: #e2e8f0;
        border: 1px solid #334155;
        border-radius: 6px;
        padding: 6px 14px;
        font-weight: bold;
        min-width: 84px;
    }
    QMessageBox QPushButton:hover {
        background-color: #243349;
        border-color: #475569;
        color: #f8fafc;
    }
    QMessageBox QPushButton:pressed { background-color: #0b1220; }
    QMessageBox QPushButton:default {
        background-color: #2563eb;
        border-color: #60a5fa;
        color: #f8fafc;
    }
    QMessageBox QPushButton:default:hover { background-color: #1d4ed8; }
)";

#endif // STYLES_H
