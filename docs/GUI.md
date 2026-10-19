# QTllama GUI guide

![QTllama dark theme](images/gui-dark.png)

## Layout

```text
┌──────────────────────────────────────────────────────────────────┐
│ ☰  New chat  Export                               Qwen3.5 4B    │ ← top bar
├────────────────┬─────────────────────────────────────────────────┤
│ MODEL          │                         ┌──────────────┐        │
│ [Qwen3.5 4B ▾] │                         │ user message │        │
│ Load/Download  │                         └──────────────┘        │
│ CONVERSATION   │ Assistant                     Copy  Regenerate  │
│ SAMPLING       │ ▸ Thought process                               │
│ PERFORMANCE    │ Answer rendered as Markdown…                    │
│ APPEARANCE     │ 652 tokens · 24.4 tok/s                         │
│   (sidebar)    │ ┌─────────────────────────────────────────┐ (↑) │
│                │ │ Message…                                 │     │ ← composer
├────────────────┴─────────────────────────────────────────────────┤
│ Ready                       24.4 tok/s · prompt 136 tok/s · ctx… │ ← status bar
└──────────────────────────────────────────────────────────────────┘
```

- **Top bar.** ☰ toggles the sidebar. **New chat** clears the conversation.
  **Export** saves it as Markdown. The right side shows the loaded model;
  hover over it for the full description.
- **Chat.** Your messages appear as bubbles on the right. Assistant replies use
  the full column width, which is capped at a readable size on wide screens.
- **Composer.** Grows up to 8 lines. The round button sends, and turns into a
  red ■ **Stop** button while the model is generating.
- **Status bar.** Shows the current state on the left, and generation speed,
  prompt speed and context use on the right.

## Keyboard shortcuts

| Keys | Action |
|---|---|
| Enter | Send |
| Shift+Enter | New line |
| Esc | Stop generation |
| Ctrl+N (⌘N) | New chat |
| Ctrl+B (⌘B) | Toggle sidebar |

## Messages

- **Streaming.** Text appears as it's generated. Markdown is re-rendered at most
  every 60 ms, so long answers stay smooth.
- **Thinking.** With *Think before answering* on, the reasoning streams live under
  **▾ Thinking…** and collapses to **▸ Thought process** when the answer starts.
  Click to expand it again; once you've toggled it yourself, it stays the way you left it.
- **Copy** copies the answer, without reasoning, as Markdown.
- **Regenerate** appears on the latest reply. It discards that reply and samples a
  new one with the current settings.
- **Footer.** Shows tokens generated and speed, plus "stopped" if you interrupted it.
- **Errors**, such as a full context window, appear in red inside the reply.

## Sidebar reference

### Model

| Control | Description |
|---|---|
| Model list | Registry models (marked *not downloaded* when missing), local `.gguf` files in the models folder, and files you've opened. |
| Description line | Size, quantization, and whether the model is downloaded or loaded. |
| **Load / Switch / Unload** | Load the selected model. *Switch* replaces the loaded one and keeps the conversation. |
| **Download / Cancel** | Download the selected registry model, with resume support. It loads automatically when done if nothing else is loaded. |
| **Open .gguf…** | Pick any GGUF file. The last 8 are remembered in the list. |
| **Models folder** | Open the models directory in your file manager. |

You can also just type: if a downloaded model is selected but not loaded, sending
a message loads it first.

### Conversation

| Control | Description |
|---|---|
| System prompt | Instructions sent before the conversation. Changes apply on the next message. |
| Think before answering | Reasoning on or off. Toggling also loads that mode's recommended sampling. |

### Sampling

Temperature, Top-p, Top-k, Min-p, Presence penalty, Repeat penalty and Max tokens.
These work the same way as the [CLI flags](CLI.md#sampling) and apply from the
next message. **Recommended** resets them to the model's values for the current mode.

### Performance (applies on reload)

| Control | Description |
|---|---|
| Context | 2K–128K tokens. Bigger contexts hold longer chats but use more memory. |
| Threads | `Auto` uses half the cores when there are more than 4, otherwise all of them. |
| GPU layers | `All` offloads everything (Metal/Vulkan/CUDA builds). `0` = CPU only. |
| Load model on startup | Load the last selected model when the app opens. |

### Appearance

| Control | Description |
|---|---|
| Theme | System, Light or Dark. On Qt ≥ 6.5, *System* follows the OS setting. |
| Text size | Base font size for the whole app. Useful on tablets. |

![QTllama light theme](images/gui-light.png)

## Command-line options

```text
qtllama [options] [prompt]
  prompt                     Send this message on startup (loads the model first)
  --verbose                  Print llama.cpp logs to the terminal
  --screenshot <file>        Save a PNG of the window after a delay, then quit
  --screenshot-delay <ms>    Delay for --screenshot (default 3000)
```

`--screenshot` captures only the QTllama window. It's used to make the images in
these docs and can be used for UI smoke tests.

## Files and settings

| What | macOS | Linux / Termux |
|---|---|---|
| Settings | `~/Library/Preferences/com.qtllama.QTllama.plist` | `~/.config/QTllama/QTllama.conf` |
| Models (installed builds) | `~/Library/Application Support/QTllama/QTllama/models` | `~/.local/share/QTllama/QTllama/models` |
| Models (built from the workspace) | `<workspace>/models` | `<workspace>/models` |

`QTLLAMA_MODELS_DIR` overrides the models location for both apps. To reset all GUI
settings, quit the app and delete the settings file (on macOS:
`defaults delete com.qtllama.QTllama`).

Saved settings: selected model, opened files, system prompt, thinking toggle, all
sampling and performance values, theme, text size, window geometry and sidebar width.
Conversations aren't saved automatically; use **Export**.

## Tablet use (Termux:X11)

- Start with the sidebar hidden (☰) and raise **Text size** to 16–18 pt, or launch
  with `QT_SCALE_FACTOR=2`.
- The chat column fills narrow screens and centres on wide ones.
- Long-press selection on touch screens follows X11 mouse emulation: drag to
  select text in replies, or use **Copy**.
- If the on-screen keyboard's Enter key sends too early, type with a hardware
  keyboard or use the send button. Shift+Enter inserts a new line.
