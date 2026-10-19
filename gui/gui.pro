# qtllama: Qt Widgets chat client.

include(../core.pri)

TEMPLATE = app
TARGET = qtllama
QT += core gui widgets network

HEADERS += \
    ChatView.h \
    MainWindow.h \
    MessageWidget.h \
    SettingsPanel.h \
    Theme.h

SOURCES += \
    main.cpp \
    ChatView.cpp \
    MainWindow.cpp \
    MessageWidget.cpp \
    SettingsPanel.cpp \
    Theme.cpp


unix:!macx {
    target.path = /usr/local/bin
    INSTALLS += target
}
