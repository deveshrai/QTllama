#pragma once

#include "Types.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <string>
#include <vector>

struct llama_model;
struct llama_context;
struct llama_vocab;
struct llama_sampler;

namespace qtllama {

// Owns a llama.cpp model + context and one running conversation.
//
// All slots are blocking. The CLI calls them directly; the GUI moves the
// engine to a worker thread and invokes them with queued connections.
// Tokens are streamed through signals as they are produced.
//
// The KV cache is reused between turns: only the new part of the formatted
// chat is decoded each turn. Anything that rewrites history (setHistory,
// regenerate, a new system prompt, context overflow) re-processes the prompt.
class LlamaEngine : public QObject
{
    Q_OBJECT

public:
    explicit LlamaEngine(QObject *parent = nullptr);
    ~LlamaEngine() override;

    bool isLoaded() const { return m_model != nullptr; }
    QString modelPath() const { return m_loadParams.modelPath; }
    QString modelDescription() const { return m_description; }
    int contextSize() const;
    int contextUsed() const { return m_nPast; }
    bool templateSupportsThinking() const { return m_templateThinks; }

    // Only call while the engine is idle (or from the engine's own thread).
    ChatHistory history() const { return m_messages; }
    QString systemPrompt() const { return m_systemPrompt; }

    // Thread-safe: aborts prompt processing or generation as soon as possible.
    void requestStop();

    // llama.cpp/ggml log output goes to stderr only when verbose.
    static void setVerbose(bool verbose);

public Q_SLOTS:
    bool loadModel(const qtllama::LoadParams &params);
    void unloadModel();

    void setSystemPrompt(const QString &prompt);
    void setHistory(const qtllama::ChatHistory &history);
    void resetConversation();

    // Appends a user message and generates the assistant reply.
    bool sendMessage(const QString &text, const qtllama::SamplingParams &params);
    // Drops the last assistant reply (if any) and generates a new one.
    bool regenerate(const qtllama::SamplingParams &params);

Q_SIGNALS:
    void loadProgress(int percent);
    void modelLoaded(const QString &description, int contextSize);
    void modelLoadFailed(const QString &error);
    void modelUnloaded();

    void generationStarted();
    void promptProgress(int processed, int total);
    void reasoningText(const QString &text); // streamed "thinking" output
    void contentText(const QString &text);   // streamed answer output
    void generationFinished(const qtllama::GenerationStats &stats, const qtllama::ChatMessage &reply);
    void errorOccurred(const QString &message);

private:
    enum class Phase { Undecided, Reasoning, Content };

    bool generate(const SamplingParams &params);
    std::string formatChat(const ChatHistory &messages, bool addAssistant) const;
    std::vector<int> tokenize(const std::string &text, bool addSpecial) const;
    bool decodeTokens(const std::vector<int> &tokens, bool reportProgress);
    llama_sampler *createSampler(const SamplingParams &params) const;
    void invalidateCache();
    ChatHistory fullHistory() const;
    bool trimHistoryToFit(int reserveTokens);

    // Streaming output parser (splits <think>…</think> from the answer).
    void beginOutput(Phase initial);
    void feedOutput(const std::string &piece);
    void flushOutput();
    void emitText(const QString &text);

    llama_model *m_model = nullptr;
    llama_context *m_ctx = nullptr;
    const llama_vocab *m_vocab = nullptr;
    std::string m_template;
    bool m_templateThinks = false;
    QString m_description;
    LoadParams m_loadParams;

    QString m_systemPrompt;
    ChatHistory m_messages;

    // KV cache bookkeeping.
    bool m_cacheValid = false;
    int m_nPast = 0;
    std::string m_prevFormatted;  // formatted chat that the cache corresponds to
    std::string m_pendingSuffix;  // end-of-turn text still to decode (e.g. "<|im_end|>\n")

    std::atomic<bool> m_abort{false};

    // Output parser state.
    Phase m_phase = Phase::Content;
    std::string m_utf8Pending;
    QString m_textPending;
    QString m_reasoning;
    QString m_content;
};

} // namespace qtllama
