# QTllama — llama.cpp + Qt 6 workspace.
#
#   scripts/build_llama.sh          # once per machine: builds llama.cpp static libs
#   mkdir build && cd build
#   qmake6 ../QTllama.pro && make -j8
#
# Headless machines can skip the GUI:  qmake6 ../QTllama.pro CONFIG+=no_gui

TEMPLATE = subdirs

SUBDIRS = core cli
cli.depends = core

!no_gui {
    SUBDIRS += gui
    gui.depends = core
}

OTHER_FILES += \
    README.md \
    LICENSE \
    CHANGELOG.md \
    CONTRIBUTING.md \
    docs/ARCHITECTURE.md \
    docs/BUILDING.md \
    docs/CLI.md \
    docs/GUI.md \
    docs/MODELS.md \
    docs/TROUBLESHOOTING.md \
    models/models.json \
    scripts/build_llama.sh \
    scripts/download_model.sh
