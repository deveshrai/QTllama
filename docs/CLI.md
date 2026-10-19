# qtllama-cli reference

`qtllama-cli` is the terminal front end. It has three modes:

| Mode | When | Behaviour |
|---|---|---|
| **Interactive** | No prompt given and stdin is a terminal | REPL with history kept across turns |
| **One-shot** | A prompt argument or `-p` is given | Answers once, then exits |
| **Pipe** | stdin is a pipe or redirected file | Reads all of stdin, appends it to any prompt, answers, exits |

Reasoning ("thinking") is printed dimmed under a `[thinking]` header, followed
by the answer. Status messages, the loading indicator, errors and `--stats` go to
**stderr**; the model's text goes to **stdout**. So `qtllama-cli "..." > answer.txt`
captures only the model output: the reasoning (unless `--hide-thinking`) and the answer.

## Synopsis

```text
qtllama-cli [options] [prompt...]
```

## Options

### Model selection

| Option | Description |
|---|---|
| `-m, --model <model>` | Registry id (`qwen3.5-2b`), a file name inside the models dir, or a path to any `.gguf`. Defaults to the registry default (`qwen3.5-4b`). |
| `--models-dir <dir>` | Where to look for and download models. Default: `$QTLLAMA_MODELS_DIR`, then the workspace's `models/`, then the per-user data dir. |
| `-l, --list` | List registry models and local `.gguf` files. `[x]` marks downloaded ones. |
| `-d, --download <id>` | Download a registry model (resumable) and exit. |

If the selected registry model isn't downloaded and you're at a terminal, the CLI
offers to download it.

### Prompt and conversation

| Option | Description |
|---|---|
| `-p, --prompt <text>` | One-shot prompt. Positional words are appended, and piped stdin is appended after a blank line. |
| `-s, --system <text>` | System prompt. |
| `--think` | Let reasoning models think before answering (default). |
| `--no-think` | Answer directly. Faster; uses the model's non-thinking sampling. |
| `--hide-thinking` | Generate reasoning but don't print it. |

### Sampling

Defaults come from the model's registry entry, per mode (thinking/instruct).
Any flag you pass overrides them.

| Option | Description | Qwen3.5 thinking / instruct default |
|---|---|---|
| `--temp <f>` | Temperature. `0` = greedy (deterministic). | 1.0 / 0.7 |
| `--top-p <f>` | Nucleus sampling cut-off. `1.0` = off. | 0.95 / 0.8 |
| `--top-k <n>` | Keep the k most likely tokens. `0` = off. | 20 / 20 |
| `--min-p <f>` | Drop tokens below `p × max`. `0` = off. | 0.0 / 0.0 |
| `--presence-penalty <f>` | Penalise tokens that already appeared. Discourages loops. | 1.5 / 1.5 |
| `--repeat-penalty <f>` | Classic repetition penalty. `1.0` = off. | 1.0 |
| `-n, --max-tokens <n>` | Cap reply length (includes reasoning). `-1` = unlimited. | -1 |
| `--seed <n>` | RNG seed for reproducible sampling. Default: random. | |

### Performance

| Option | Description |
|---|---|
| `-c, --ctx <n>` | Context window in tokens (default 8192, capped at the model's training length). Larger windows use more RAM. |
| `-t, --threads <n>` | Generation threads. Default: half the cores when there are more than 4, otherwise all of them. |
| `--ngl <n>` | Layers to offload to the GPU. `-1` = all (default), `0` = CPU only. Ignored on CPU-only builds. |

### Output

| Option | Description |
|---|---|
| `--stats` | After each reply, print prompt and generation speed and context usage. |
| `--no-color` | Disable ANSI colours. Also disabled when stdout isn't a TTY or `NO_COLOR` is set. |
| `--verbose` | Show llama.cpp's own log output (model loading details, backend info). |
| `-h, --help` / `--help-all` | Help (with Qt options). |
| `-v, --version` | Version. |

## Interactive commands

| Command | Description |
|---|---|
| `/help` | List commands. |
| `/reset` (`/clear`) | Forget the conversation. The system prompt is kept. |
| `/system` | Show the current system prompt. |
| `/system <text>` | Set a new system prompt. The next turn reprocesses the history. |
| `/think` | Toggle thinking. `/think on` and `/think off` set it explicitly. Switching also applies that mode's recommended sampling. |
| `/regen` | Discard the last reply and generate a new one. |
| `/stats [on\|off]` | Toggle per-reply statistics. |
| `/save <file.md>` | Write the conversation, including collapsed reasoning, as Markdown. |
| `/info` | Model, context used/size, thinking mode and sampling values. |
| `/quit` (`/exit`) | Exit. Ctrl+D at an empty prompt also exits. |

**Multi-line input.** End a line with `\` to continue it. The prompt changes to `. `.

**Non-interactive callers.** If stdin isn't a terminal and also isn't a pipe or
file (for example a device inherited from an IDE or a service), it's ignored. Pass
the prompt as an argument in that case.

**Stopping.** Ctrl+C during prompt processing or generation stops the reply and
keeps what was produced. Ctrl+C at the prompt exits.

## Recipes

```sh
# Ask about a file
qtllama-cli -p "Summarise this log and list the errors:" < /var/log/syslog

# Code review of staged changes, no reasoning text, plain output
git diff --cached | qtllama-cli --no-think --no-color -p "Review this diff:"

# Deterministic output for tests
qtllama-cli --temp 0 --seed 42 --no-think "Give three prime numbers, comma-separated."

# Bound the cost of a reasoning answer
qtllama-cli -n 1024 "Prove that sqrt(2) is irrational."

# Use a model outside the registry with a bigger context
qtllama-cli -m ~/models/Mistral-7B-Instruct-Q4_K_M.gguf -c 16384

# CPU-only with an explicit thread count (e.g. on a tablet)
qtllama-cli --ngl 0 -t 6 --stats

# Download every small model for an offline machine
for m in qwen3.5-0.8b qwen3.5-2b; do qtllama-cli --download $m; done
```

## Exit codes

| Code | Meaning |
|---|---|
| `0` | Success, or the reply was stopped by the user |
| `1` | Model not found or failed to load, download failed, generation error, or no prompt when stdin isn't a terminal |
| `130` | Interrupted by Ctrl+C at the prompt |

## Environment

| Variable | Effect |
|---|---|
| `QTLLAMA_MODELS_DIR` | Models directory (overridden by `--models-dir`). |
| `NO_COLOR` | Disable colours. |
