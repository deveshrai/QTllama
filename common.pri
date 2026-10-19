# Settings shared by every sub-project.

QTLLAMA_ROOT = $$PWD
QTLLAMA_VERSION = 0.1.0

CONFIG += c++17 warn_on
VERSION = $$QTLLAMA_VERSION

DEFINES += \
    QTLLAMA_VERSION=\\\"$$QTLLAMA_VERSION\\\" \
    QTLLAMA_SOURCE_DIR=\\\"$$QTLLAMA_ROOT\\\" \
    QT_NO_KEYWORDS \
    QT_DISABLE_DEPRECATED_BEFORE=0x060000

# Newer clang warns about Qt's own headers.
clang: QMAKE_CXXFLAGS_WARN_ON += -Wno-deprecated-literal-operator -Wno-unknown-warning-option

# Keep moc/obj files out of the way in shadow builds.
MOC_DIR     = .moc
OBJECTS_DIR = .obj
RCC_DIR     = .rcc
UI_DIR      = .ui

# Qt < 6.6 on macOS links the AGL framework (via mkspecs and QtGui.prl),
# which recent macOS SDKs no longer ship. Shared Qt frameworks don't need .prl
# processing, and our own static libs are linked explicitly.
macx {
    QMAKE_LIBS_OPENGL = -framework OpenGL
    !contains(QT_CONFIG, static): CONFIG -= link_prl
    CONFIG += sdk_no_version_check
}
