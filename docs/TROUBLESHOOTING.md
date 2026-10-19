# Troubleshooting

Build problems are covered in [BUILDING.md → Build troubleshooting](BUILDING.md#build-troubleshooting).
This page covers runtime problems.

When in doubt, run with `--verbose` (CLI or GUI) to see llama.cpp's own log.

## Models

**"Model not found" / "not downloaded yet"**
- Run `qtllama-cli --list` to see which directory is being used and what's in it.
- Check that `QTLLAMA_MODELS_DIR` points where you expect. Installed builds don't use
  the workspace `models/` folder.
- A leftover `<file>.part` is an unfinished download. Run the download again to resume it.

**"Failed to load model"**
- The file may be truncated: compare its size with `sizeMB` or the Hugging Face page.
  Delete it and download again.
- The model architecture may be newer than the pinned llama.cpp. Check with
  `--verbose` for `unknown model architecture`, then update llama.cpp
  ([CONTRIBUTING.md](../CONTRIBUTING.md#updating-llamacpp)).
- Not enough memory: try a smaller model or quant.

**"Failed to create a context (try a smaller context size)"**
The KV cache doesn't fit. Lower the context (`-c 4096` or the GUI's *Context*) or
offload fewer layers to the GPU (`--ngl 20`).

## Generation

**The model "thinks" for a long time**
Qwen3.5 reasons by default. Turn thinking off (`--no-think`, `/think off`, or
untick *Think before answering*), or cap it with `-n 1024`.

**Repetitive or looping output**
Use the recommended sampling (GUI **Recommended**, or `/think` in the CLI to
re-apply it). Keep the presence penalty around 1.0–1.5, and avoid `--temp 0` with
thinking models.

**"Context window is full"**
The reply hit `n_ctx`. Start a new chat, raise the context, or set *Max tokens*.
Older turns are trimmed automatically before each new message, but not in the
middle of a reply.

**Garbled characters**
This shouldn't happen, since multi-byte characters are reassembled. If it does,
check that your terminal is set to UTF-8 (`echo $LANG`), and on Termux install a
font with wide Unicode coverage.

**`graph_compute ... failed` / `llama_decode: failed to decode` in the terminal**
This is printed when a generation is aborted mid-batch, for example when you
close the window during a reply. It's harmless. Any other time, run `--verbose`
and report the output.

## Performance

**Slow generation**
- Use `--stats` or the status bar to measure tok/s.
- Threads: try `-t` equal to the number of *performance* cores. More threads is
  often slower on phones.
- On Macs, make sure GPU layers is `All` (`--ngl -1`).
- On Linux, consider a Vulkan or CUDA build ([BUILDING.md](BUILDING.md#gpu-backends)).
- Use a smaller model or quant (`qwen3.5-2b`, `Q4_0` on ARM).
- Android: keep the device plugged in, run `termux-wake-lock`, and avoid thermal throttling.

**Slow first reply**
The first decode includes backend warm-up (Metal shader compilation, memory
mapping). Later turns reuse the KV cache and start much faster.

**Slow model loading on Android**
Keep the model in Termux's own storage, not `/sdcard`.

## GUI

**Blank window or no display under Termux**
Start Termux:X11 first (`termux-x11 :0 &`) and run the app with `DISPLAY=:0`.
If you see `could not connect to display`, the X server isn't running.

**Everything is tiny on a tablet**
Use `QT_SCALE_FACTOR=2` (or 1.5) and/or raise **Text size** in the sidebar.

**Reset all GUI settings**
- macOS: `defaults delete com.qtllama.QTllama`
- Linux/Termux: `rm ~/.config/QTllama/QTllama.conf`

**The theme doesn't follow the system**
*System* follows the OS colour scheme on Qt ≥ 6.5. On older Qt it's decided at
startup from the platform palette; pick Light or Dark explicitly.

## Downloads

**Download fails immediately with a TLS/SSL error**
Qt Network needs an OpenSSL backend. On Linux, install `libssl3` (or `libssl1.1`
on older distros). On Termux, `pkg install openssl`. As a workaround, use
`scripts/download_model.sh`, which uses curl.

**Download is stuck at 0 %**
Some networks block `huggingface.co` or its CDN. Download the file another way and
drop it into the models directory with the same file name.
