#ifndef STYLES_H
#define STYLES_H

#include <QString>

const QString styleSheetConnectStation = R"(
    #frameStation {
        color: #10b981;
        border-radius: 8px;
        border: 2px solid #8AE08A;
        font-family: \"Consolas\";
    }
)";

const QString styleSheetDisconnectStation = R"(
    #frameStation {
        color: #10b981;
        border-radius: 8px;
        border: 2px solid #ff5252;
        font-family: \"Consolas\";
    }
)";

const QString styleSheetConnectAnalyzer = R"(
    #frameR3 {
        color: #10b981;
        border-radius: 8px;
        border: 2px solid #8AE08A;
        font-family: \"Consolas\";
    }
)";

const QString styleSheetDisconnectAnalyzer = R"(
    #frameR3 {
        color: #10b981;
        border-radius: 8px;
        border: 2px solid #ff5252;
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
