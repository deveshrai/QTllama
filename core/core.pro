# qtllama-core: llama.cpp engine, model registry and downloader shared by
# the CLI and GUI.

include(../common.pri)
include(../llama.pri)

TEMPLATE = lib
CONFIG += staticlib
TARGET = qtllama-core

QT = core network

HEADERS += \
    Types.h \
    LlamaEngine.h \
    ModelDownloader.h \
    ModelRegistry.h

SOURCES += \
    LlamaEngine.cpp \
    ModelDownloader.cpp \
    ModelRegistry.cpp

RESOURCES += core.qrc
