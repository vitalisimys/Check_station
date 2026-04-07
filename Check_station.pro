QT += core gui widgets network concurrent serialport printsupport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

LIBS += -lssh2

DEFINES += DEBUG_ENABLED

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    analyzer_controller.cpp \
    device_controller.cpp \
    finder.cpp \
    main.cpp \
    mainwindow.cpp \
    qcustomplot.cpp \
    settingsdialog.cpp \
    ssher.cpp \
    sweep_plot.cpp

HEADERS += \
    analyzer_controller.h \
    device_controller.h \
    debug.h \
    finder.h \
    mainwindow.h \
    protocol_consts.h \
    qcustomplot.h \
    settingsdialog.h \
    ssher.h \
    styles.h \
    sweep_plot.h

FORMS += \
    mainwindow.ui \
    settingsdialog.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES +=

RESOURCES += \
    res.qrc

#########################################################################################
#                                                                                       #
# Проекты -> Сборка -> Сборка, Этапы -> Сборка, добавить этап -> Особый этап обработки  #
# Команда:          sudo                                                                #
# Параметры:        setcap cap_net_raw,cap_net_bind_service+ep %{buildDir}/flasher_bku  #
# Рабочий каталог:  %{buildDir}                                                         #
#                                                                                       #
#########################################################################################
