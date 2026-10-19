// qtllama-cli — terminal front end for QTllama.
//
//   qtllama-cli                         interactive chat with the default model
//   qtllama-cli "Explain RAII"          one-shot answer
//   echo "text" | qtllama-cli -p "Summarise:"
//   qtllama-cli --list | --download qwen3.5-2b

#include "LlamaEngine.h"
#include "ModelDownloader.h"
#include "ModelRegistry.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>

#ifdef Q_OS_WIN
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace qtllama;

namespace {

LlamaEngine *g_engine = nullptr;
std::atomic<bool> g_busy{false};

void onSigint(int)
{
    if (g_busy.load() && g_engine) {
        g_engine->requestStop();
        return;
    }
    std::_Exit(130);
}

struct Style
{
    bool color = false;
    const char *dim() const { return color ? "\033[2m" : ""; }
    const char *bold() const { return color ? "\033[1m" : ""; }
    const char *green() const { return color ? "\033[1;32m" : ""; }
    const char *yellow() const { return color ? "\033[33m" : ""; }
    const char *red() const { return color ? "\033[31m" : ""; }
    const char *reset() const { return color ? "\033[0m" : ""; }
};

// True when stdin is a pipe or redirected file. Other non-terminal stdins (e.g.
// a device inherited from an IDE or service) may never reach EOF, so don't read them.
bool stdinIsPipeOrFile()
{
#ifdef Q_OS_WIN
    return !isatty(fileno(stdin));
#else
    struct stat st;
    return fstat(fileno(stdin), &st) == 0 && (S_ISFIFO(st.st_mode) || S_ISREG(st.st_mode));
#endif
}

void out(const QString &s)
{
    const QByteArray utf8 = s.toUtf8();
    std::fwrite(utf8.constData(), 1, size_t(utf8.size()), stdout);
    std::fflush(stdout);
}

void err(const QString &s)
{
    const QByteArray utf8 = s.toUtf8();
    std::fwrite(utf8.constData(), 1, size_t(utf8.size()), stderr);
    std::fflush(stderr);
}

QString humanSize(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("?");
    const double mb = double(bytes) / (1024.0 * 1024.0);
    return mb >= 1024 ? QStringLiteral("%1 GB").arg(mb / 1024.0, 0, 'f', 2)
                      : QStringLiteral("%1 MB").arg(mb, 0, 'f', 0);
}

bool download(ModelRegistry &registry, const ModelInfo &model)
{
    const QString dest = registry.localPath(model);
    err(QStringLiteral("Downloading %1\n  from %2\n  to   %3\n")
            .arg(model.name, model.downloadUrl().toString(), dest));

    ModelDownloader dl;
    QEventLoop loop;
    bool ok = false;
    int lastPct = -1;
    QObject::connect(&dl, &ModelDownloader::progress, [&](qint64 got, qint64 total) {
        const int pct = total > 0 ? int(got * 100 / total) : -1;
        if (pct == lastPct)
            return;
        lastPct = pct;
        err(QStringLiteral("\r  %1 / %2  (%3%)   ").arg(humanSize(got), humanSize(total)).arg(pct));
    });
    QObject::connect(&dl, &ModelDownloader::finished, [&](const QString &) {
        ok = true;
        loop.quit();
    });
    QObject::connect(&dl, &ModelDownloader::failed, [&](const QString &e) {
        err(QStringLiteral("\nDownload failed: %1\n").arg(e));
        loop.quit();
    });
    dl.start(model.downloadUrl(), dest);
    if (dl.isRunning())
        loop.exec();
    if (ok) {
        err(QStringLiteral("\nSaved %1\n").arg(dest));
        registry.reload();
    }
    return ok;
}

void listModels(const ModelRegistry &registry)
{
    out(QStringLiteral("Models directory: %1\n\n").arg(registry.modelsDir()));
    for (const ModelInfo &m : registry.models()) {
        const bool isDefault = m.id == registry.defaultModelId();
        out(QStringLiteral("  %1 %2  %3  %4  %5\n")
                .arg(registry.isDownloaded(m) ? QStringLiteral("[x]") : QStringLiteral("[ ]"))
                .arg(m.id, -18)
                .arg(m.name + (isDefault ? QStringLiteral(" (default)") : QString()), -26)
                .arg(QStringLiteral("%1 MB").arg(m.sizeMB), 9)
                .arg(m.description));
    }
    out(QStringLiteral("\n[x] = downloaded.  Get one with: qtllama-cli --download <id>\n"
                       "Any other .gguf placed in the models directory is listed automatically.\n"));
}

bool askYesNo(const QString &question)
{
    err(question + QStringLiteral(" [Y/n] "));
    std::string line;
    if (!std::getline(std::cin, line))
        return false;
    return line.empty() || line[0] == 'y' || line[0] == 'Y';
}

void printStats(const GenerationStats &s, const Style &st)
{
    err(QStringLiteral("%1[prompt %2 tok @ %3 t/s | gen %4 tok @ %5 t/s | ctx %6/%7]%8\n")
            .arg(QString::fromLatin1(st.dim()))
            .arg(s.promptTokens)
            .arg(s.promptTokensPerSecond(), 0, 'f', 1)
            .arg(s.generatedTokens)
            .arg(s.tokensPerSecond(), 0, 'f', 1)
            .arg(s.contextUsed)
            .arg(s.contextSize)
            .arg(QString::fromLatin1(st.reset())));
}

const char *kHelp =
    "Commands:\n"
    "  /help              show this help\n"
    "  /reset             start a new conversation\n"
    "  /system [text]     show or set the system prompt (resets nothing else)\n"
    "  /think on|off      toggle reasoning for thinking models\n"
    "  /regen             regenerate the last reply\n"
    "  /stats on|off      show speed after each reply\n"
    "  /save <file>       save the conversation as Markdown\n"
    "  /info              show model and sampling settings\n"
    "  /quit              exit\n"
    "End a line with \\ to continue typing on the next line.\n"
    "Ctrl+C stops a reply in progress; at the prompt it exits.\n";

bool saveMarkdown(const QString &path, const QString &system, const ChatHistory &history)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream ts(&f);
    if (!system.isEmpty())
        ts << "**System:** " << system << "\n\n";
    for (const ChatMessage &m : history) {
        ts << "### " << (m.role == ChatMessage::Role::User ? "You" : "Assistant") << "\n\n";
        if (!m.reasoning.isEmpty())
            ts << "<details><summary>Thinking</summary>\n\n" << m.reasoning << "\n\n</details>\n\n";
        ts << m.content << "\n\n";
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("QTllama"));
    QCoreApplication::setApplicationName(QStringLiteral("QTllama"));
    QCoreApplication::setApplicationVersion(QStringLiteral(QTLLAMA_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Chat with local LLMs via llama.cpp."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption modelOpt({"m", "model"}, "Model id from --list, a file in the models directory, or a path to a .gguf.", "model");
    const QCommandLineOption modelsDirOpt("models-dir", "Directory holding .gguf files (default: $QTLLAMA_MODELS_DIR or the workspace models/ folder).", "dir");
    const QCommandLineOption listOpt({"l", "list"}, "List known and local models.");
    const QCommandLineOption downloadOpt({"d", "download"}, "Download a model by id and exit.", "id");
    const QCommandLineOption promptOpt({"p", "prompt"}, "Prompt for a one-shot answer. Piped stdin is appended.", "text");
    const QCommandLineOption systemOpt({"s", "system"}, "System prompt.", "text");
    const QCommandLineOption ctxOpt({"c", "ctx"}, "Context size in tokens (default: per model, 8192).", "n");
    const QCommandLineOption threadsOpt({"t", "threads"}, "CPU threads (default: auto).", "n");
    const QCommandLineOption nglOpt("ngl", "Layers to offload to the GPU (-1 = all, 0 = CPU only).", "n", "-1");
    const QCommandLineOption tempOpt("temp", "Sampling temperature.", "f");
    const QCommandLineOption topPOpt("top-p", "Top-p sampling.", "f");
    const QCommandLineOption topKOpt("top-k", "Top-k sampling.", "n");
    const QCommandLineOption minPOpt("min-p", "Min-p sampling.", "f");
    const QCommandLineOption presenceOpt("presence-penalty", "Presence penalty.", "f");
    const QCommandLineOption repeatOpt("repeat-penalty", "Repeat penalty (1.0 = off).", "f");
    const QCommandLineOption maxTokOpt({"n", "max-tokens"}, "Maximum tokens per reply (-1 = no limit).", "n", "-1");
    const QCommandLineOption seedOpt("seed", "RNG seed (default: random).", "n");
    const QCommandLineOption thinkOpt("think", "Let thinking models reason before answering (default).");
    const QCommandLineOption noThinkOpt("no-think", "Answer directly without reasoning.");
    const QCommandLineOption hideThinkOpt("hide-thinking", "Don't print the reasoning text.");
    const QCommandLineOption statsOpt("stats", "Print speed statistics after each reply.");
    const QCommandLineOption noColorOpt("no-color", "Disable ANSI colours.");
    const QCommandLineOption verboseOpt("verbose", "Show llama.cpp log output.");

    parser.addOptions({modelOpt, modelsDirOpt, listOpt, downloadOpt, promptOpt, systemOpt, ctxOpt,
                       threadsOpt, nglOpt, tempOpt, topPOpt, topKOpt, minPOpt, presenceOpt, repeatOpt,
                       maxTokOpt, seedOpt, thinkOpt, noThinkOpt, hideThinkOpt, statsOpt, noColorOpt,
                       verboseOpt});
    parser.addPositionalArgument(QStringLiteral("prompt"), QStringLiteral("One-shot prompt (same as -p)."), QStringLiteral("[prompt...]"));
    parser.process(app);

    Style style;
    style.color = isatty(fileno(stdout)) && qEnvironmentVariableIsEmpty("NO_COLOR") && !parser.isSet(noColorOpt);
    const bool stdinTty = isatty(fileno(stdin));

    LlamaEngine::setVerbose(parser.isSet(verboseOpt));
    ModelRegistry registry(parser.isSet(modelsDirOpt) ? QDir(parser.value(modelsDirOpt)).absolutePath()
                                                      : ModelRegistry::defaultModelsDir());

    if (parser.isSet(listOpt)) {
        listModels(registry);
        return 0;
    }
    if (parser.isSet(downloadOpt)) {
        const ModelInfo *m = registry.find(parser.value(downloadOpt));
        if (!m || m->isCustom) {
            err(QStringLiteral("Unknown model id '%1'. See --list.\n").arg(parser.value(downloadOpt)));
            return 1;
        }
        return download(registry, *m) ? 0 : 1;
    }

    // --- Resolve the model
    const QString modelArg = parser.value(modelOpt);
    QString modelPath = registry.resolvePath(modelArg);
    const ModelInfo *info = modelArg.isEmpty() ? registry.defaultModel() : registry.find(modelArg);
    if (modelPath.isEmpty()) {
        if (info && !info->isCustom && !registry.isDownloaded(*info)) {
            err(QStringLiteral("%1 is not downloaded yet (%2 MB).\n").arg(info->name).arg(info->sizeMB));
            if (!stdinTty || !askYesNo(QStringLiteral("Download it now?")) || !download(registry, *info))
                return 1;
            modelPath = registry.localPath(*info);
        } else {
            err(QStringLiteral("Model not found: %1\nSee --list.\n").arg(modelArg));
            return 1;
        }
    }
    if (!info)
        info = registry.find(QFileInfo(modelPath).fileName());

    // --- Settings
    const bool thinking = !parser.isSet(noThinkOpt);
    SamplingParams sampling = info ? info->recommendedSampling(thinking) : SamplingParams{};
    if (!info)
        sampling.thinking = thinking;
    auto setFloat = [&](const QCommandLineOption &o, float &v) {
        if (parser.isSet(o)) v = parser.value(o).toFloat();
    };
    setFloat(tempOpt, sampling.temperature);
    setFloat(topPOpt, sampling.topP);
    setFloat(minPOpt, sampling.minP);
    setFloat(presenceOpt, sampling.presencePenalty);
    setFloat(repeatOpt, sampling.repeatPenalty);
    if (parser.isSet(topKOpt)) sampling.topK = parser.value(topKOpt).toInt();
    if (parser.isSet(seedOpt)) sampling.seed = parser.value(seedOpt).toUInt();
    sampling.maxTokens = parser.value(maxTokOpt).toInt();

    LoadParams load;
    load.modelPath = modelPath;
    load.contextSize = parser.isSet(ctxOpt) ? parser.value(ctxOpt).toInt() : (info ? info->contextLength : 8192);
    load.threads = parser.value(threadsOpt).toInt();
    load.gpuLayers = parser.value(nglOpt).toInt();

    // --- Prompt (one-shot mode)
    QString prompt = parser.value(promptOpt);
    if (!parser.positionalArguments().isEmpty())
        prompt = (prompt.isEmpty() ? QString() : prompt + QLatin1Char(' ')) + parser.positionalArguments().join(QLatin1Char(' '));
    if (!stdinTty && stdinIsPipeOrFile()) {
        QTextStream in(stdin);
        const QString piped = in.readAll();
        if (!piped.trimmed().isEmpty())
            prompt = prompt.isEmpty() ? piped : prompt + QStringLiteral("\n\n") + piped;
    }
    const bool oneShot = !prompt.trimmed().isEmpty() || !stdinTty;
    if (oneShot && prompt.trimmed().isEmpty()) {
        err(QStringLiteral("No prompt given (stdin is not a terminal). Pass a prompt or pipe text in.\n"));
        return 1;
    }
    bool showStats = parser.isSet(statsOpt);
    const bool hideThinking = parser.isSet(hideThinkOpt);

    // --- Load
    LlamaEngine engine;
    g_engine = &engine;
    std::signal(SIGINT, onSigint);

    const bool interactiveUi = !oneShot || isatty(fileno(stderr));
    if (interactiveUi) {
        QObject::connect(&engine, &LlamaEngine::loadProgress, [&](int pct) {
            if (pct % 10 != 0)
                return;
            err(QStringLiteral("\rLoading %1 ... %2%").arg(QFileInfo(modelPath).fileName()).arg(pct));
        });
    }
    g_busy = true;
    QString loadError;
    QObject::connect(&engine, &LlamaEngine::modelLoadFailed, [&](const QString &e) { loadError = e; });
    const bool loaded = engine.loadModel(load);
    g_busy = false;
    if (interactiveUi)
        err(QStringLiteral("\r\033[K"));
    if (!loaded) {
        err(QStringLiteral("%1\n").arg(loadError));
        return 1;
    }
    engine.setSystemPrompt(parser.value(systemOpt));

    // --- Streaming output
    enum class Stream { None, Reasoning, Content } stream = Stream::None;
    QObject::connect(&engine, &LlamaEngine::generationStarted, [&] { stream = Stream::None; });
    QObject::connect(&engine, &LlamaEngine::reasoningText, [&](const QString &t) {
        if (hideThinking)
            return;
        if (stream != Stream::Reasoning) {
            out(QString::fromLatin1(style.dim()) + QStringLiteral("[thinking]\n"));
            stream = Stream::Reasoning;
        }
        out(t);
    });
    QObject::connect(&engine, &LlamaEngine::contentText, [&](const QString &t) {
        if (stream == Stream::Reasoning)
            out(QString::fromLatin1(style.reset()) + QStringLiteral("\n\n"));
        stream = Stream::Content;
        out(t);
    });
    QObject::connect(&engine, &LlamaEngine::generationFinished, [&](const GenerationStats &s, const ChatMessage &) {
        if (stream == Stream::Reasoning)
            out(QString::fromLatin1(style.reset()));
        out(QStringLiteral("\n"));
        if (s.stopped)
            err(QStringLiteral("%1[stopped]%2\n").arg(QString::fromLatin1(style.yellow()), QString::fromLatin1(style.reset())));
        if (showStats)
            printStats(s, style);
    });
    QObject::connect(&engine, &LlamaEngine::errorOccurred, [&](const QString &e) {
        err(QStringLiteral("%1error: %2%3\n").arg(QString::fromLatin1(style.red()), e, QString::fromLatin1(style.reset())));
    });

    if (oneShot) {
        g_busy = true;
        const bool ok = engine.sendMessage(prompt.trimmed(), sampling);
        g_busy = false;
        return ok ? 0 : 1;
    }

    // --- Interactive chat
    err(QStringLiteral("%1%2%3  ctx %4  threads %5  thinking %6\nType /help for commands.\n\n")
            .arg(QString::fromLatin1(style.bold()), engine.modelDescription(), QString::fromLatin1(style.reset()))
            .arg(engine.contextSize())
            .arg(load.threads > 0 ? load.threads : defaultThreadCount())
            .arg(sampling.thinking ? QStringLiteral("on") : QStringLiteral("off")));

    std::string line;
    while (true) {
        out(QString::fromLatin1(style.green()) + QStringLiteral("> ") + QString::fromLatin1(style.reset()));
        QString input;
        bool eof = false;
        while (true) {
            if (!std::getline(std::cin, line)) {
                eof = true;
                break;
            }
            QString part = QString::fromStdString(line);
            if (part.endsWith(QLatin1Char('\\'))) {
                input += part.chopped(1) + QLatin1Char('\n');
                out(QStringLiteral(". "));
                continue;
            }
            input += part;
            break;
        }
        if (eof && input.isEmpty()) {
            out(QStringLiteral("\n"));
            break;
        }
        input = input.trimmed();
        if (input.isEmpty())
            continue;

        if (input.startsWith(QLatin1Char('/'))) {
            const QString cmd = input.section(QLatin1Char(' '), 0, 0).toLower();
            const QString arg = input.section(QLatin1Char(' '), 1).trimmed();
            if (cmd == QLatin1String("/quit") || cmd == QLatin1String("/exit")) {
                break;
            } else if (cmd == QLatin1String("/help")) {
                err(QString::fromLatin1(kHelp));
            } else if (cmd == QLatin1String("/reset") || cmd == QLatin1String("/clear")) {
                engine.resetConversation();
                err(QStringLiteral("New conversation.\n"));
            } else if (cmd == QLatin1String("/system")) {
                if (arg.isEmpty()) {
                    err(QStringLiteral("System prompt: %1\n").arg(engine.systemPrompt().isEmpty() ? QStringLiteral("(none)") : engine.systemPrompt()));
                } else {
                    engine.setSystemPrompt(arg);
                    err(QStringLiteral("System prompt set.\n"));
                }
            } else if (cmd == QLatin1String("/think")) {
                const bool on = arg.isEmpty() ? !sampling.thinking : arg == QLatin1String("on");
                const SamplingParams rec = info ? info->recommendedSampling(on) : sampling;
                // Switch to the model's recommended sampling for the new mode.
                const int maxTokens = sampling.maxTokens;
                const quint32 seed = sampling.seed;
                sampling = rec;
                sampling.thinking = on;
                sampling.maxTokens = maxTokens;
                sampling.seed = seed;
                err(QStringLiteral("Thinking %1.\n").arg(on ? QStringLiteral("on") : QStringLiteral("off")));
            } else if (cmd == QLatin1String("/regen")) {
                g_busy = true;
                engine.regenerate(sampling);
                g_busy = false;
            } else if (cmd == QLatin1String("/stats")) {
                showStats = arg.isEmpty() ? !showStats : arg == QLatin1String("on");
                err(QStringLiteral("Stats %1.\n").arg(showStats ? QStringLiteral("on") : QStringLiteral("off")));
            } else if (cmd == QLatin1String("/save")) {
                if (arg.isEmpty())
                    err(QStringLiteral("Usage: /save <file.md>\n"));
                else if (saveMarkdown(arg, engine.systemPrompt(), engine.history()))
                    err(QStringLiteral("Saved %1\n").arg(arg));
                else
                    err(QStringLiteral("Cannot write %1\n").arg(arg));
            } else if (cmd == QLatin1String("/info")) {
                err(QStringLiteral("Model:    %1\nContext:  %2 / %3 tokens\nThinking: %4\n"
                                   "Sampling: temp %5  top-p %6  top-k %7  min-p %8  presence %9\n")
                        .arg(engine.modelDescription())
                        .arg(engine.contextUsed())
                        .arg(engine.contextSize())
                        .arg(sampling.thinking ? QStringLiteral("on") : QStringLiteral("off"))
                        .arg(sampling.temperature)
                        .arg(sampling.topP)
                        .arg(sampling.topK)
                        .arg(sampling.minP)
                        .arg(sampling.presencePenalty));
            } else {
                err(QStringLiteral("Unknown command %1. Try /help.\n").arg(cmd));
            }
            continue;
        }

        g_busy = true;
        engine.sendMessage(input, sampling);
        g_busy = false;
        out(QStringLiteral("\n"));
        if (eof)
            break;
    }
    return 0;
}
