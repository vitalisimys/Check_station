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
    station_controller.cpp \
    emission_antenna.cpp \
    finder.cpp \
    flasher.cpp \
    main.cpp \
    mainwindow.cpp \
    qcustomplot.cpp \
    settingsdialog.cpp \
    ssher.cpp \
    traffic_generator.cpp \
    sweep_plot.cpp \
    tftpserver.cpp \
    update_bku.cpp \
    firmwarefiles.cpp

HEADERS += \
    analyzer_controller.h \
    station_controller.h \
    debug.h \
    emission_antenna.h \
    finder.h \
    flasher.h \
    mainwindow.h \
    protocol_consts.h \
    qcustomplot.h \
    settingsdialog.h \
    ssher.h \
    styles.h \
    traffic_generator.h \
    sweep_plot.h \
    tftpserver.h \
    update_bku.h \
    firmwarefiles.h

FORMS += \
    mainwindow.ui \
    receiveresultstrip.ui \
    settingsdialog.ui \
    update_bku.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES +=

RESOURCES += \
    res.qrc

###########################################################################################
#                                                                                         #
# Проекты -> Сборка -> Сборка, Этапы -> Сборка, добавить этап -> Особый этап обработки    #
# Команда:          sudo                                                                  #
# Параметры:        setcap cap_net_raw,cap_net_bind_service+ep %{buildDir}/Check_station  #
# Рабочий каталог:  %{buildDir}                                                           #
#                                                                                         #
###########################################################################################
