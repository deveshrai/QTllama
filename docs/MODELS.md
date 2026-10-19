# Models

QTllama runs any model in **GGUF** format that the pinned llama.cpp version supports.
Known models are listed in a JSON registry with download locations and tuned
sampling. Any other `.gguf` file works too.

## Where models live

The models directory is chosen in this order:

1. `--models-dir <dir>` (CLI only)
2. `$QTLLAMA_MODELS_DIR`
3. `<workspace>/models` when the binary was built from this workspace and the folder is writable
4. The per-user data directory (see [GUI.md](GUI.md#files-and-settings))

`*.gguf` and `*.part` files are git-ignored.

## Getting a model

```sh
qtllama-cli --list                       # what's available, [x] = downloaded
qtllama-cli --download qwen3.5-4b        # built-in downloader (resumable)
scripts/download_model.sh qwen3.5-4b     # curl fallback
scripts/download_model.sh --list
```

The GUI's **Download** button uses the same downloader. Interrupted downloads
leave a `<file>.part` and resume where they stopped.

## Built-in registry

| Id | File | Size | Min RAM* |
|---|---|---|---|
| `qwen3.5-4b` (default) | `Qwen_Qwen3.5-4B-Q4_K_M.gguf` | 2.9 GB | 6 GB |
| `qwen3.5-2b` | `Qwen_Qwen3.5-2B-Q4_K_M.gguf` | 1.3 GB | 4 GB |
| `qwen3.5-0.8b` | `Qwen_Qwen3.5-0.8B-Q4_K_M.gguf` | 0.6 GB | 2 GB |
| `qwen3.5-9b` | `Qwen_Qwen3.5-9B-Q4_K_M.gguf` | 5.9 GB | 12 GB |

\*Rough total system RAM for an 8K context alongside a desktop session.

All four come from [bartowski's](https://huggingface.co/bartowski) imatrix GGUF
conversions of the official [Qwen3.5](https://huggingface.co/Qwen) weights (Apache 2.0).
Qwen3.5 uses a hybrid architecture (gated delta-net plus attention, llama.cpp arch
`qwen35`), supports 262K context natively, and **thinks by default**.

## Thinking mode

Reasoning models write a `<think>…</think>` block before answering. QTllama:

- **Detects** support when the model's chat template mentions `<think>`.
- **Thinking on:** opens the block itself (`<think>\n`) after the assistant prefix,
  so the reasoning streams straight into the *Thought process* section.
- **Thinking off:** pre-fills an empty block (`<think>\n\n</think>\n\n`), which is
  the standard way to make Qwen-style models answer directly.
- Keeps the reasoning out of the stored history. It stays in the KV cache while
  the conversation grows turn by turn, but is dropped when the history is
  re-processed (regenerate, system-prompt change, model switch, context trim),
  which matches how Qwen's own template treats earlier turns.
- For models whose template has no `<think>`, it still detects a `<think>` block
  if the model emits one on its own.

Thinking gives better answers to hard questions but can use hundreds of tokens.
Use `-n/--max-tokens` to cap it, or turn it off for chat and simple lookups.
The 0.8B model is unreliable with thinking on: it often loops until the context
is full, so use it with thinking off.

## Registry format

The registry is compiled into the binaries from `models/models.json`. To add or
override entries without rebuilding, put a `models.json` in your **models
directory**: entries with the same `id` replace built-in ones, new ids are added,
and `default` can be changed.

```json
{
    "default": "qwen3.5-4b",
    "models": [
        {
            "id": "qwen3.5-4b",
            "name": "Qwen3.5 4B",
            "description": "Default. Good balance of quality and speed.",
            "repo": "bartowski/Qwen_Qwen3.5-4B-GGUF",
            "file": "Qwen_Qwen3.5-4B-Q4_K_M.gguf",
            "quant": "Q4_K_M",
            "sizeMB": 2873,
            "contextLength": 8192,
            "thinking": true,
            "sampling": {
                "thinking": { "temperature": 1.0, "topP": 0.95, "topK": 20, "minP": 0.0, "presencePenalty": 1.5 },
                "instruct": { "temperature": 0.7, "topP": 0.8,  "topK": 20, "minP": 0.0, "presencePenalty": 1.5 }
            }
        }
    ]
}
```

| Field | Required | Description |
|---|---|---|
| `id` | ✅ | Unique, used by `-m` and `--download`. Matching is case-insensitive. |
| `name` | | Display name (defaults to `id`). |
| `description` | | One line shown in the GUI and `--list`. |
| `repo` | for download | Hugging Face repo, `owner/name`. |
| `file` | ✅ | GGUF file name in the repo and in the models directory. |
| `quant` | | Shown next to the name in the GUI. |
| `sizeMB` | | Shown in lists, and used for progress when the server sends no length. |
| `contextLength` | | Default context for this model (the CLI uses it unless `-c` is given). |
| `thinking` | | `true` if the model supports `<think>` reasoning. |
| `sampling.thinking` | | Sampling used with thinking on. Falls back to `instruct`. |
| `sampling.instruct` | | Sampling used with thinking off. |

Sampling keys: `temperature`, `topP`, `topK`, `minP`, `presencePenalty`, `repeatPenalty`.
Omitted keys use the engine defaults (0.7 / 0.8 / 20 / 0.0 / 0.0 / 1.0).

The download URL is built as `https://huggingface.co/<repo>/resolve/main/<file>`.

### Example: adding a non-reasoning model

```json
{
    "models": [
        {
            "id": "gemma-3-4b",
            "name": "Gemma 3 4B",
            "repo": "bartowski/google_gemma-3-4b-it-GGUF",
            "file": "google_gemma-3-4b-it-Q4_K_M.gguf",
            "quant": "Q4_K_M",
            "sizeMB": 2374,
            "contextLength": 8192,
            "thinking": false,
            "sampling": { "instruct": { "temperature": 1.0, "topP": 0.95, "topK": 64, "minP": 0.0 } }
        }
    ]
}
```

Check repo and file names on Hugging Face (`https://huggingface.co/api/models/<repo>` lists the files) before adding an entry.

### Unlisted files

Any `*.gguf` in the models directory that isn't in the registry appears as a
*local file* model, named after the file. Files starting with `mmproj` (vision
projectors) are skipped. Local files default to neutral sampling and allow
thinking, so set them up in the sidebar or with CLI flags.

## Chat templates

Prompts are formatted with `llama_chat_apply_template`, using the template stored
in the GGUF. llama.cpp recognises many common template families (ChatML/Qwen,
Llama 3, Mistral, Gemma, Phi, DeepSeek, Command-R and others). Anything it doesn't
recognise falls back to **ChatML**. Tool calling and image input aren't
supported yet.

## Choosing a quantization

| Quant | Size vs Q4_K_M | Quality | When to use |
|---|---|---|---|
| Q8_0 | ~1.7× | Near-lossless | Plenty of RAM, want best quality |
| Q6_K | ~1.3× | Excellent | Good default if RAM allows |
| Q5_K_M | ~1.15× | Very good | |
| **Q4_K_M** | 1× | Good | **Registry default**: best size/quality trade-off |
| IQ4_XS | ~0.9× | Good | Slightly smaller alternative |
| Q3_K_M / IQ3 | ~0.8× | Noticeably worse | Only when memory is very tight |

On phones and tablets, `Q4_0` can be faster than `Q4_K_M` because llama.cpp's
ARM CPU backend repacks it for faster matrix multiplication. Try both.

## Memory budget

Total memory is about **the model file size + KV cache + ~0.3–0.5 GB overhead**.
Qwen3.5's hybrid design keeps the KV cache small. Halving the context (`-c 4096`)
is still the quickest way to cut memory on small machines.
