# Changelog

All notable changes to this project are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- MIT license.
- Termux build verified on a Samsung SM-X840 (MT6993); tablet performance numbers
  and Termux notes added to the docs.

### Fixed
- `scripts/build_llama.sh` no longer overwrites Termux's `$PREFIX`, which broke
  CMake configuration on Android.

## [0.1.0] - 2026-10-19

### Added
- `qtllama-core` static library: `LlamaEngine` (streaming generation, KV-cache
  reuse across turns, `<think>` reasoning split, automatic context trimming, abortable
  loading and decoding), `ModelRegistry` (JSON registry + `.gguf` directory scan),
  `ModelDownloader` (resumable Hugging Face downloads).
- `qtllama-cli`: interactive REPL with `/` commands, one-shot and pipe modes,
  model listing and download, and full sampling and performance flags.
- `qtllama` GUI: Qt Widgets chat with rendered Markdown, collapsible reasoning,
  light/dark/system themes, sidebar for model, sampling and performance settings,
  Copy/Regenerate/Export, persistent settings, and `--screenshot` for docs.
- Model registry with Qwen3.5 4B (default), 2B, 0.8B and 9B in Q4_K_M, plus
  Qwen's recommended sampling for thinking and non-thinking modes.
- `scripts/build_llama.sh` (static llama.cpp build for macOS, Linux and Termux) and
  `scripts/download_model.sh` (curl fallback).
- llama.cpp submodule pinned to `b11739`.
- Documentation: README, building, CLI, GUI, models, architecture,
  troubleshooting, contributing.
