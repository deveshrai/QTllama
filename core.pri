# Include from an application project to use the qtllama-core static library.

include($$PWD/common.pri)

QT += network
INCLUDEPATH += $$PWD/core

CORE_BUILD_DIR = $$shadowed($$PWD)/core
LIBS += -L$$CORE_BUILD_DIR -lqtllama-core
PRE_TARGETDEPS += $$CORE_BUILD_DIR/libqtllama-core.a

include($$PWD/llama.pri)
