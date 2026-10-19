# Building QTllama

A build has two stages:

1. **llama.cpp** is built once per machine with CMake, by `scripts/build_llama.sh`,
   into static libraries in `third_party/llama-install/`.
2. **QTllama** (core, cli and gui) is built with **qmake** and links those libraries
   through `llama.pri`.

Keeping llama.cpp in CMake lets us use its own build system for CPU feature
detection and GPU backends. qmake builds everything we write.

- [Requirements](#requirements)
- [Getting the source](#getting-the-source)
- [macOS](#macos)
- [Linux (including older machines)](#linux-including-older-machines)
- [Android tablet via Termux](#android-tablet-via-termux)
- [GPU backends](#gpu-backends)
- [Build options reference](#build-options-reference)
- [Installing](#installing)
- [Rebuilding and cleaning](#rebuilding-and-cleaning)
- [Build troubleshooting](#build-troubleshooting)

---

## Requirements

| Tool | Minimum | Used for |
|---|---|---|
| Qt | **6.2** (Core, Network; Widgets for the GUI) | Both apps |
| C++ compiler | C++17: GCC 9+, Clang 10+, Apple Clang 13+ | Everything |
| CMake | 3.14 | llama.cpp only |
| git | any | Submodule |
| make | GNU or BSD make | qmake output |
| Disk | ~1 GB build + model size | |

The CLI needs only `QtCore` and `QtNetwork`, so a headless machine doesn't need
Qt Widgets.

## Getting the source

```sh
git clone --recurse-submodules <repo-url> QTllama
cd QTllama
```

If you cloned without `--recurse-submodules`:

```sh
git submodule update --init --depth 1
```

`scripts/build_llama.sh` also fetches the submodule if it's missing.

---

## macOS

Tested on macOS 26 / Apple M2 with Homebrew Qt 6.5.

```sh
brew install qt cmake
scripts/build_llama.sh                  # Metal + Accelerate, shaders embedded
mkdir build && cd build
qmake ../QTllama.pro                    # Homebrew's qmake is Qt 6
make -j$(sysctl -n hw.ncpu)
open gui/qtllama.app                    # or ./gui/qtllama.app/Contents/MacOS/qtllama
./cli/qtllama-cli --help
```

Notes:
- `build_llama.sh` sets `CMAKE_OSX_DEPLOYMENT_TARGET=13.0` to match Qt, which avoids
  hundreds of "built for newer macOS version" linker warnings. Override it with
  `MACOSX_DEPLOYMENT_TARGET=14.0 scripts/build_llama.sh`.
- Qt < 6.6 tries to link the removed **AGL** framework. `common.pri` works around
  this by overriding `QMAKE_LIBS_OPENGL` and disabling `.prl` processing for shared
  Qt builds. If you build against a *static* Qt, see
  [troubleshooting](#framework-agl-not-found).
- The Qt online installer (`~/Qt/6.x/macos/bin/qmake`) works too.

## Linux (including older machines)

### Debian / Ubuntu (22.04+, Debian 12+)

```sh
sudo apt install build-essential cmake git qt6-base-dev qt6-base-dev-tools
scripts/build_llama.sh
mkdir build && cd build
qmake6 ../QTllama.pro                  # Debian/Ubuntu name it qmake6
make -j$(nproc)
./gui/qtllama
```

### Fedora

```sh
sudo dnf install gcc-c++ cmake git qt6-qtbase-devel
# qmake is at /usr/bin/qmake6 or /usr/lib64/qt6/bin/qmake
```

### Arch

```sh
sudo pacman -S base-devel cmake git qt6-base
```

### Headless or CLI-only

```sh
qmake6 ../QTllama.pro CONFIG+=no_gui
```

### Older machines

- **Qt 6.2 is the floor.** Ubuntu 20.04 and Debian 11 don't package Qt 6. On
  those, install Qt 6 with the [Qt online installer](https://www.qt.io/download-qt-installer)
  or `aqtinstall` (`pip install aqtinstall && aqt install-qt linux desktop 6.5.3 gcc_64`),
  then call that Qt's `qmake` by its full path.
- **Compiler.** llama.cpp needs a reasonably modern C++17 compiler. On old distros,
  install `gcc-10`/`g++-10` and run
  `CC=gcc-10 CXX=g++-10 scripts/build_llama.sh` and `qmake6 QMAKE_CXX=g++-10 QMAKE_LINK=g++-10 ...`.
- **CPU features.** `GGML_NATIVE=ON` (the default in the script) tunes for the CPU
  that builds it. Build *on* the old machine rather than copying binaries from a newer one.
  If you have to cross-deploy, use `scripts/build_llama.sh -DGGML_NATIVE=OFF`.
- **RAM.** For machines with 4–8 GB of RAM, use `qwen3.5-2b` or `qwen3.5-0.8b`, and
  consider `-c 4096`.

## Android tablet via Termux

Tested target: Samsung Galaxy Tab S12+ (ARM64). Install Termux from F-Droid or
GitHub; the Play Store build is outdated.

```sh
pkg update
pkg install x11-repo                       # Qt lives in the X11 repository
pkg install git cmake clang make qt6-qtbase termux-x11-nightly
pkg search qt6                             # check the exact package names on your device

git clone --recurse-submodules <repo-url> QTllama && cd QTllama
scripts/build_llama.sh                     # native build; detects dotprod/i8mm
mkdir build && cd build
qmake6 ../QTllama.pro                      # if qmake6 is missing: ls $PREFIX/bin | grep qmake
make -j8
```

**CLI.** Run `./cli/qtllama-cli` directly in Termux.

**GUI.** Install the **Termux:X11** companion app, then:

```sh
termux-x11 :0 &
DISPLAY=:0 QT_SCALE_FACTOR=2 ./gui/qtllama
```

Tablet tips:
- Collapse the sidebar with **☰** to give the chat the full width.
- Increase **Text size** in *Appearance*, or adjust `QT_SCALE_FACTOR`.
- Keep the device plugged in and run `termux-wake-lock` during long generations;
  Android throttles background processes.
- Storage: keep models inside Termux (`~/QTllama/models`). Shared storage
  (`/sdcard`) is much slower for memory-mapped files.
- Threads: by default the engine uses half the cores on 8-core SoCs, which
  matches the performance cores. Try `-t 6` or `-t 8` and compare tok/s.

> The package names above come from Termux's x11-repo (Qt 6.11 at the time of
> writing). If they differ on your device, `pkg search qt6` lists the right ones.

---

## GPU backends

`scripts/build_llama.sh` passes any extra arguments to CMake. `llama.pri`
automatically links whatever backend libraries end up in `third_party/llama-install/lib`.

| Backend | Command | Extra requirements |
|---|---|---|
| Metal (macOS) | default | none |
| Vulkan (Linux, some Android) | `scripts/build_llama.sh -DGGML_VULKAN=ON` | Vulkan SDK/headers, `glslc` |
| CUDA (NVIDIA) | `scripts/build_llama.sh -DGGML_CUDA=ON` | CUDA toolkit |
| OpenBLAS (CPU prompt speed-up) | `scripts/build_llama.sh -DGGML_BLAS=ON -DGGML_BLAS_VENDOR=OpenBLAS` | `libopenblas-dev` |

After switching backends, clear the CMake cache first:
`rm -rf third_party/llama-build third_party/llama-install`.

GPU offload is controlled at runtime with `--ngl` (CLI) or **GPU layers** (GUI).
`-1` offloads everything and `0` keeps the model on the CPU.

## Build options reference

### `scripts/build_llama.sh`

| Variable / argument | Default | Meaning |
|---|---|---|
| `JOBS` | CPU count | Parallel build jobs |
| `BUILD_TYPE` | `Release` | CMake build type |
| `MACOSX_DEPLOYMENT_TARGET` | `13.0` | macOS minimum version |
| extra args | none | Passed to CMake (`-DGGML_VULKAN=ON`, `-DGGML_NATIVE=OFF`, ...) |

What it always sets: static libraries, PIC, OpenMP off (no libgomp/libomp
mismatch across platforms), and no tools, tests, server, app or network deps.

### qmake

| Option | Example | Meaning |
|---|---|---|
| `CONFIG+=no_gui` | `qmake6 ../QTllama.pro CONFIG+=no_gui` | Build core and CLI only |
| `CONFIG+=debug` | | Debug build of the apps (llama.cpp stays Release) |
| `LLAMA_PREFIX=` | `qmake6 ../QTllama.pro LLAMA_PREFIX=/opt/llama` | Use a llama.cpp install somewhere else |

Always build out of tree (`build/`); the `.gitignore` expects it.

## Installing

```sh
make install          # Linux/Termux: copies binaries to /usr/local/bin (or $PREFIX)
```

Installed binaries don't find the workspace's `models/` folder unless they were
built from that workspace. They fall back to the per-user data directory
(see [GUI.md → Files and settings](GUI.md#files-and-settings)). Set
`QTLLAMA_MODELS_DIR` to share one models folder between builds.

On macOS, `gui/qtllama.app` can be copied to `/Applications`. To make it
self-contained for other Macs, run `macdeployqt gui/qtllama.app`.

## Rebuilding and cleaning

| Situation | Command |
|---|---|
| Edited QTllama sources | `make -j8` in `build/` |
| Added/removed source files or edited `.pro`/`.pri` | `qmake6 ../QTllama.pro && make -j8` |
| Updated llama.cpp or changed backends | `rm -rf third_party/llama-build third_party/llama-install && scripts/build_llama.sh`, then `make` |
| Start from scratch | `rm -rf build third_party/llama-build third_party/llama-install` |

The apps relink automatically when `libllama.a` changes (`PRE_TARGETDEPS`).

## Build troubleshooting

### `llama.cpp not found in .../third_party/llama-install`
`scripts/build_llama.sh` hasn't been run, or it failed. Run it and check the last
lines of its output.

### `fatal error: 'build-info.h' file not found` while building llama.cpp
An older copy of the script built llama.cpp's own app target. Pull the current
script (it passes `-DLLAMA_BUILD_APP=OFF`) and remove `third_party/llama-build`.

### `framework 'AGL' not found`
This happens with Qt < 6.6 on a recent macOS SDK. `common.pri` handles shared
Qt builds. With a static Qt, remove `-framework AGL` from
`$QTDIR/mkspecs/modules/qt_lib_gui_private.pri` and from `QtGui.prl`, or upgrade Qt.

### `Project WARNING: Qt has only been tested with version 13 of the platform SDK`
This is harmless. `common.pri` already adds `sdk_no_version_check`; re-run qmake.

### Undefined references to `ggml_*` or `llama_*` on Linux
The libraries are linked in the wrong order, or a backend is missing. Check that
`third_party/llama-install/lib` contains `libggml-cpu.a` and `libggml-base.a`, then
re-run qmake. If you built Vulkan/CUDA/BLAS, make sure their system libraries
(`-lvulkan`, `-lcudart`, `-lopenblas`) are installed.

### `qmake: command not found` / qmake is Qt 5
Use `qmake6`, or call the Qt 6 qmake by its full path. Check with `qmake -query QT_VERSION`.

### Unknown llama.cpp symbols after updating the submodule
The llama.cpp C API changes over time. All direct API use is in
`core/LlamaEngine.cpp`; see [CONTRIBUTING.md](../CONTRIBUTING.md#updating-llamacpp).
