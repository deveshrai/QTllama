# Contributing

## Development setup

```sh
git clone --recurse-submodules <repo-url> QTllama && cd QTllama
scripts/build_llama.sh
mkdir build && cd build && qmake6 ../QTllama.pro && make -j8
```

Qt Creator can open `QTllama.pro` directly. Point its build directory at `build/`.

Before committing, build **both** apps:

```sh
cd build && qmake6 ../QTllama.pro && make -j8
./cli/qtllama-cli --no-think "Say hi"          # quick end-to-end check
```

## Code style

Match the surrounding code:

- **C++17**, Qt 6 APIs only. Keep it compiling on **Qt 6.2**; guard newer APIs with
  `#if QT_VERSION >= QT_VERSION_CHECK(...)` (see `Theme.cpp`).
- `QT_NO_KEYWORDS` is defined: use `Q_SIGNALS`, `Q_SLOTS`, `Q_EMIT`.
- Classes and files in `PascalCase`, members `m_camelCase`, 4-space indent, braces
  as in the existing files.
- Engine and core types live in `namespace qtllama`. GUI classes are global.
- Wrap user-visible strings in `tr()`.
- Comments explain *why*, not *what*. Keep the existing light density.
- `core/` must not depend on QtGui/QtWidgets.
- All llama.cpp calls go in `core/LlamaEngine.cpp`.
- Shell scripts are POSIX `sh` (they run on Termux and minimal Linux).

## Project conventions

- **New engine features** go through `Types.h` (params/stats) → `LlamaEngine` →
  CLI flag → `SettingsPanel` control, with persistence in `saveSettings`/`loadSettings`.
- **New models**: edit `models/models.json`, using exact file names and sizes from the
  Hugging Face API (`https://huggingface.co/api/models/<repo>?blobs=true`).
- **Docs**: update the matching page in `docs/` and `CHANGELOG.md` with every
  user-visible change.
- **Screenshots** for docs: `qtllama --screenshot docs/images/x.png --screenshot-delay 30000 "prompt"`.
  It only captures the app window.

## Commits

- Imperative subject, ≤ 72 characters (`Add DRY sampler to engine and CLI`).
- One logical change per commit, and build both apps before committing.
- Never commit models (`*.gguf`), build directories, or `third_party/llama-*`.
  `.gitignore` covers these.

## Updating llama.cpp

llama.cpp is a submodule pinned to a release tag (currently **b11739**).

```sh
cd third_party/llama.cpp
git fetch --depth 1 origin tag b12345
git checkout b12345
cd ../..
rm -rf third_party/llama-build third_party/llama-install
scripts/build_llama.sh
cd build && make -j8
```

If the build breaks, the C API has changed. Compare `include/llama.h` and fix
`core/LlamaEngine.cpp`. API changes we've already adapted to in b11739:

- `llama_model_params.use_mmap` → `load_mode` (`LLAMA_LOAD_MODE_*`)
- `llama_sampler_init_penalties` takes `n_vocab` as its first argument
- KV cache API: `llama_memory_clear(llama_get_memory(ctx), true)`
- New CMake targets to turn off: `LLAMA_BUILD_APP`

Then smoke-test thinking on and off, a multi-turn chat (the second turn's prompt
token count should be small), stopping mid-reply, and regenerate. Commit the
submodule bump along with any engine fixes, and update the tag mentioned in
README, BUILDING and this file.
