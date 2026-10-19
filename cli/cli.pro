# qtllama-cli: terminal chat client.

include(../core.pri)

TEMPLATE = app
TARGET = qtllama-cli
QT = core network
CONFIG += console
CONFIG -= app_bundle

SOURCES += main.cpp

target.path = $$[QT_INSTALL_PREFIX]/bin
unix: target.path = /usr/local/bin
INSTALLS += target
