# Architecture

This document covers how QTllama is put together: who owns what, how a
message becomes tokens and comes back as streamed text, and the design decisions
behind it.

## Overview

```text
           ┌──────────────────────┐        ┌──────────────────────────┐
           │  qtllama-cli         │        │  qtllama (GUI)           │
           │  cli/main.cpp        │        │  MainWindow, ChatView,   │
           │  main thread, sync   │        │  MessageWidget, ...      │
           └─────────┬────────────┘        └────────────┬─────────────┘
                     │ direct calls                     │ queued invokeMethod
                     │ + direct signals                 │ + queued signals
           ┌─────────▼──────────────────────────────────▼─────────────┐
           │  qtllama-core (static lib)                                │
           │  ┌──────────────┐ ┌───────────────┐ ┌──────────────────┐ │
           │  │ LlamaEngine  │ │ ModelRegistry │ │ ModelDownloader  │ │
           │  └──────┬───────┘ └───────────────┘ └──────────────────┘ │
           └─────────┼─────────────────────────────────────────────────┘
                     │ C API (llama.h)
           ┌─────────▼─────────────────────────────────────────────────┐
           │ llama.cpp / ggml static libs (CPU + Metal/Vulkan/CUDA)     │
           └────────────────────────────────────────────────────────────┘
```

Design rules:

- **`core/` depends only on QtCore and QtNetwork**, so the CLI builds on headless
  machines and Termux without Widgets.
- **All llama.cpp API use is in `core/LlamaEngine.cpp`.** When llama.cpp changes
  its API, that's the only file to fix.
- **The engine is blocking and single-threaded internally.** Concurrency is the
  front end's job: the CLI calls it directly, and the GUI runs it on a `QThread`.
  This keeps the engine free of locks, apart from one atomic stop flag.

## Build structure

```text
QTllama.pro (subdirs)
├── core/core.pro   includes common.pri + llama.pri        → libqtllama-core.a
├── cli/cli.pro     includes core.pri (→ common + llama)   → qtllama-cli
└── gui/gui.pro     includes core.pri                      → qtllama(.app)
```

- `common.pri` sets C++17, the version, `QTLLAMA_SOURCE_DIR` (used to find the
  workspace `models/`), `QT_NO_KEYWORDS` (code uses `Q_EMIT`/`Q_SIGNALS`), and
  platform fixes.
- `llama.pri` adds include paths and links, in this order: `llama`, `ggml`,
  optional backends (`ggml-metal`, `ggml-blas`, `ggml-vulkan`, `ggml-cuda`, each only
  if its `.a` exists), `ggml-cpu`, `ggml-base`, then system frameworks and libs.
  Static link order matters.
- `core.qrc` embeds `models/models.json`. Resources in a static library have to be
  registered explicitly; `ModelRegistry.cpp` does this with `Q_INIT_RESOURCE(core)`
  at global scope.
- llama.cpp is built by CMake (`scripts/build_llama.sh`) with OpenMP off, so
  binaries don't depend on `libgomp`/`libomp`. Backends are statically registered, so
  there's no `ggml_backend_load_all()` and no plugin `.so` files to ship.

## Core components

### `Types.h`

Plain value types shared by all layers and registered as Qt metatypes so they can
cross threads through queued signals:

| Type | Purpose |
|---|---|
| `ChatMessage` | `role` (System/User/Assistant), `content`, `reasoning` |
| `ChatHistory` | `QVector<ChatMessage>` |
| `LoadParams` | model path, context, threads, GPU layers, mmap, flash attention (reload needed) |
| `SamplingParams` | temperature, top-p/k, min-p, penalties, max tokens, seed, `thinking` (per message) |
| `GenerationStats` | prompt and generated token counts and timings, context used, `stopped` |

`defaultThreadCount()` returns half the cores when there are more than 4,
otherwise all of them. On big.LITTLE phones and Apple Silicon, running on the
efficiency cores slows generation down.

### `ModelRegistry`

Builds the model list from three sources, in order:

1. the built-in `:/qtllama/models.json` resource,
2. `<modelsDir>/models.json`, if present (same `id` = override),
3. a scan of `<modelsDir>/*.gguf` for unlisted files (`isCustom = true`).

`resolvePath()` turns user input (`id`, file name, or path) into a model file path.
`defaultModelsDir()` implements the `QTLLAMA_MODELS_DIR` → workspace → app-data
fallback. `ModelInfo::recommendedSampling(thinking)` returns the right preset.

### `ModelDownloader`

A `QNetworkAccessManager` GET to `https://huggingface.co/<repo>/resolve/main/<file>`:
- writes `<dest>.part`, then renames it on success;
- resumes with a `Range` header if a `.part` file exists, and restarts if the
  server ignores the range (HTTP 200 instead of 206); HTTP 416 means it's already complete;
- follows redirects (Hugging Face → CDN) using `NoLessSafeRedirectPolicy`.

### `LlamaEngine`

Owns `llama_model`, `llama_context`, the chat template, and the conversation
(`m_systemPrompt` + `m_messages`).

#### Loading

`loadModel()`: `llama_model_load_from_file` (with a progress callback that emits
`loadProgress` and can be cancelled) → `llama_init_from_model` with
`n_ctx = min(requested, n_ctx_train)`, `n_batch = n_ubatch = 512`, flash attention
`AUTO`, and an **abort callback** tied to `m_abort` so long prompt processing can be
interrupted. The GGUF's chat template is read with `llama_model_chat_template`.
`m_templateThinks` is set if the template contains `<think>`.

#### One turn

```text
sendMessage(text)
 └─ m_messages += User(text)
 └─ generate(params)
     1. formatted = apply_template(system + messages, add_assistant=true)
     2. if cache valid and formatted starts with m_prevFormatted:
            toDecode = m_pendingSuffix + formatted[m_prevFormatted.size():]   ← only the new part
        else:
            clear KV; toDecode = formatted                                     ← full re-process
     3. toDecode += "<think>\n"                 (thinking on, template supports it)
                 or "<think>\n\n</think>\n\n"    (thinking off)
     4. if it won't fit (n_past + tokens + reserve > n_ctx):
            clear KV, drop oldest turns until it fits, re-format
     5. tokenize → decode in n_batch chunks (emit promptProgress, check m_abort)
     6. loop: sample → stop on EOG / max tokens / full context / m_abort
              token_to_piece → output parser → reasoningText / contentText
              decode the sampled token
     7. m_messages += Assistant(content, reasoning)
     8. after = apply_template(all, add_assistant=false)
        if after starts with formatted + content:
            m_prevFormatted = after
            m_pendingSuffix = rest of after   (e.g. "<|im_end|>\n": the end-of-turn
                                                text the model sampled but we never decoded)
        else: invalidate cache
     9. emit generationFinished(stats, reply)
```

**Why the template-diff approach?** llama.cpp's built-in templates render the
*whole* conversation. Comparing the new rendering with the previous one finds
exactly what's new, without the engine knowing any template's syntax. The
pending-suffix trick lets the KV cache match the template byte-for-byte across
turns, so later turns cost only their own tokens.

**Why not partial KV removal?** Qwen3.5 is a hybrid recurrent model. Its
recurrent state can't be rolled back to an arbitrary position
(`llama_memory_seq_rm` with a partial range fails). So any edit to past history
(regenerate, new system prompt, trimming) clears the cache and re-processes the
prompt, which is simple and correct for every architecture.

**Context overflow.** `reserve` is `max_tokens` (capped at n_ctx/2) or n_ctx/4.
When a turn won't fit, the oldest messages are dropped, always starting the
kept history at a user message, until prompt + reserve fits.

#### Output parser

Tokens arrive as raw bytes and pass through three stages:

1. **UTF-8 assembly.** Bytes are buffered until they form complete code points
   (`completeUtf8Prefix`), because a single emoji or CJK character can span tokens.
2. **Phase state machine.**
   - `Undecided`: for models that may open `<think>` on their own. Leading
     whitespace and partial `<think` prefixes are held back until it's clear.
   - `Reasoning`: text goes to `reasoningText` until `</think>`. A trailing partial
     `</thi` is held back so a tag split across tokens is never shown.
   - `Content`: everything goes to `contentText`.
3. **Whitespace trimming** at the start of each section.

Special tokens are rendered (`special=true`) so `<think>` and `</think>` are visible
to the parser. EOG tokens end the loop and are never printed.

#### Stopping and errors

`requestStop()` is the only thread-safe method. It sets `m_abort`, which is checked:
- by llama.cpp's abort callback during `llama_decode` (prompt processing),
- by the model-load progress callback,
- between sampled tokens.

A stopped generation still records the partial reply and emits
`generationFinished` with `stats.stopped = true`. llama.cpp logs an error when a
decode is aborted; the log callback suppresses errors while a stop is in progress.
Without `--verbose`, only errors are printed.

## Front ends

### CLI threading

Everything runs on the main thread. `QCoreApplication` exists for Qt services
(settings paths, networking). The download loop uses a local `QEventLoop`. A
`SIGINT` handler calls `requestStop()` while busy, or exits otherwise. Signals are
connected directly to lambdas that write to stdout/stderr.

### GUI threading

```text
GUI thread                                  engine thread (QThread "llama-engine")
──────────                                  ──────────────────────────────────────
MainWindow::send()
  QMetaObject::invokeMethod(engine, λ) ───▶  engine->setSystemPrompt(); engine->sendMessage()
                                                 │ blocking: decode + sample loop
  MessageWidget::appendReasoning ◀── queued ─── reasoningText
  MessageWidget::appendContent   ◀── queued ─── contentText
  finish(), status bar           ◀── queued ─── generationFinished
Esc / Stop button
  engine->requestStop()  (direct, atomic) ──▶  loop sees m_abort
```

- Queued invocations run in order, so "stop, then load another model" doesn't
  need extra synchronisation.
- On close, `~MainWindow` stops the engine, quits and joins the thread, and then
  deletes the engine.
- `ModelDownloader` lives on the GUI thread; its network I/O is asynchronous.

### GUI rendering

- `ChatView` is a `QScrollArea` with a column of `MessageWidget`s. It follows the
  bottom only while the user hasn't scrolled up (`m_follow`), and centres a
  max-860 px column on wide windows.
- `MessageWidget` converts Markdown to HTML with `QTextDocument::setMarkdown`
  (GitHub dialect), then tints code blocks and scales headings before handing the
  HTML to a `QLabel`. Streaming updates are batched with a 60 ms single-shot timer.
- `Theme` applies the Fusion style plus a palette and stylesheet built from a
  colour-token table (`@surface`, `@accent`, ...). Widgets opt in with
  `objectName`s (`UserBubble`, `Primary`, `SendButton`, ...).
- `SettingsPanel` owns every setting widget and stores them with `QSettings`.

## Extending

| Want to... | Where |
|---|---|
| Add a model | `models/models.json` (see [MODELS.md](MODELS.md)) |
| Add a sampler (DRY, XTC, mirostat) | `SamplingParams` in `Types.h` → `LlamaEngine::createSampler` → CLI flag + `SettingsPanel` row |
| Persist conversations | Serialise `ChatView::history()` / `LlamaEngine::history()`, then `LlamaEngine::setHistory()` to restore |
| Multimodal (images) | Build llama.cpp's `mtmd` library and pass `mmproj-*.gguf`; the engine would need an image path in `ChatMessage` |
| Another front end (QML, server) | Link `qtllama-core` and drive `LlamaEngine` the way the GUI does |
| Translations | Strings are wrapped in `tr()`; add `TRANSLATIONS` to the `.pro` files and run `lupdate` |
