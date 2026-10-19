#include "LlamaEngine.h"

#include <llama.h>

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <cstdio>
#include <mutex>
#include <vector>

namespace qtllama {

namespace {

std::atomic<bool> g_verbose{false};
std::atomic<bool> g_aborting{false}; // a user abort makes llama.cpp log "errors"
ggml_log_level g_lastLevel = GGML_LOG_LEVEL_INFO;

void logCallback(ggml_log_level level, const char *text, void *)
{
    if (level != GGML_LOG_LEVEL_CONT)
        g_lastLevel = level;
    if (g_verbose.load() || (g_lastLevel >= GGML_LOG_LEVEL_ERROR && !g_aborting.load())) {
        std::fputs(text, stderr);
        std::fflush(stderr);
    }
}

void initBackendOnce()
{
    static std::once_flag once;
    std::call_once(once, [] {
        llama_log_set(logCallback, nullptr);
        llama_backend_init();
    });
}

// Length of the longest prefix of `s` that doesn't end in a partial UTF-8
// sequence. Tokens can split multi-byte characters.
size_t completeUtf8Prefix(const std::string &s)
{
    const size_t n = s.size();
    // Look back at most 3 bytes for the start of the last sequence.
    for (size_t back = 1; back <= 4 && back <= n; ++back) {
        const unsigned char c = static_cast<unsigned char>(s[n - back]);
        if ((c & 0xC0) == 0x80)
            continue; // continuation byte
        size_t need = 1;
        if ((c & 0xE0) == 0xC0) need = 2;
        else if ((c & 0xF0) == 0xE0) need = 3;
        else if ((c & 0xF8) == 0xF0) need = 4;
        return back >= need ? n : n - back;
    }
    return n;
}

const QString kThinkOpen = QStringLiteral("<think>");
const QString kThinkClose = QStringLiteral("</think>");

// Number of trailing characters of `text` that could be the start of `tag`.
int partialTagSuffix(const QString &text, const QString &tag)
{
    for (int len = qMin(text.size(), tag.size() - 1); len > 0; --len) {
        if (tag.startsWith(QStringView(text).right(len)))
            return len;
    }
    return 0;
}

} // namespace

LlamaEngine::LlamaEngine(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<ChatMessage>();
    qRegisterMetaType<ChatHistory>();
    qRegisterMetaType<LoadParams>();
    qRegisterMetaType<SamplingParams>();
    qRegisterMetaType<GenerationStats>();
    initBackendOnce();
}

LlamaEngine::~LlamaEngine()
{
    unloadModel();
}

void LlamaEngine::setVerbose(bool verbose)
{
    g_verbose = verbose;
}

int LlamaEngine::contextSize() const
{
    return m_ctx ? int(llama_n_ctx(m_ctx)) : 0;
}

void LlamaEngine::requestStop()
{
    g_aborting = true;
    m_abort = true;
}

// --- Model lifecycle --------------------------------------------------------

bool LlamaEngine::loadModel(const LoadParams &params)
{
    unloadModel();
    m_abort = false;
    g_aborting = false;

    if (!QFileInfo::exists(params.modelPath)) {
        const QString err = tr("Model file not found: %1").arg(params.modelPath);
        Q_EMIT modelLoadFailed(err);
        return false;
    }

    struct Progress { LlamaEngine *self; int last; } progress{this, -1};

    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = params.gpuLayers;
    mp.load_mode = params.useMmap ? LLAMA_LOAD_MODE_AUTO : LLAMA_LOAD_MODE_NONE;
    mp.progress_callback = [](float p, void *ud) -> bool {
        auto *pr = static_cast<Progress *>(ud);
        const int pct = int(p * 100.0f);
        if (pct != pr->last) {
            pr->last = pct;
            Q_EMIT pr->self->loadProgress(pct);
        }
        return !pr->self->m_abort.load();
    };
    mp.progress_callback_user_data = &progress;

    const QByteArray path = QFile::encodeName(params.modelPath);
    m_model = llama_model_load_from_file(path.constData(), mp);
    if (!m_model) {
        Q_EMIT modelLoadFailed(m_abort ? tr("Loading cancelled")
                                       : tr("Failed to load model: %1").arg(params.modelPath));
        return false;
    }

    const int trainCtx = llama_model_n_ctx_train(m_model);
    const int threads = params.threads > 0 ? params.threads : defaultThreadCount();

    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = params.contextSize > 0 ? uint32_t(qMin(params.contextSize, trainCtx)) : 0;
    cp.n_batch = 512;
    cp.n_ubatch = 512;
    cp.n_threads = threads;
    cp.n_threads_batch = params.batchThreads > 0 ? params.batchThreads : qMax(threads, defaultThreadCount());
    cp.flash_attn_type = params.flashAttention ? LLAMA_FLASH_ATTN_TYPE_AUTO
                                               : LLAMA_FLASH_ATTN_TYPE_DISABLED;
    cp.abort_callback = [](void *data) -> bool {
        return static_cast<std::atomic<bool> *>(data)->load();
    };
    cp.abort_callback_data = &m_abort;
    cp.no_perf = true;

    m_ctx = llama_init_from_model(m_model, cp);
    if (!m_ctx) {
        llama_model_free(m_model);
        m_model = nullptr;
        Q_EMIT modelLoadFailed(tr("Failed to create a context (try a smaller context size)"));
        return false;
    }

    m_vocab = llama_model_get_vocab(m_model);
    const char *tmpl = llama_model_chat_template(m_model, nullptr);
    m_template = tmpl ? tmpl : "";
    m_templateThinks = m_template.find("<think>") != std::string::npos;
    m_loadParams = params;

    char desc[256] = {};
    llama_model_desc(m_model, desc, sizeof desc);
    m_description = QStringLiteral("%1 (%2)").arg(QFileInfo(params.modelPath).fileName(),
                                                   QString::fromUtf8(desc));

    invalidateCache();
    Q_EMIT modelLoaded(m_description, contextSize());
    return true;
}

void LlamaEngine::unloadModel()
{
    const bool wasLoaded = m_model != nullptr;
    if (m_ctx) {
        llama_free(m_ctx);
        m_ctx = nullptr;
    }
    if (m_model) {
        llama_model_free(m_model);
        m_model = nullptr;
    }
    m_vocab = nullptr;
    m_template.clear();
    m_templateThinks = false;
    m_description.clear();
    m_cacheValid = false;
    m_nPast = 0;
    if (wasLoaded)
        Q_EMIT modelUnloaded();
}

// --- Conversation -------------------------------------------------------------

void LlamaEngine::setSystemPrompt(const QString &prompt)
{
    if (prompt == m_systemPrompt)
        return;
    m_systemPrompt = prompt;
    invalidateCache();
}

void LlamaEngine::setHistory(const ChatHistory &history)
{
    m_messages = history;
    invalidateCache();
}

void LlamaEngine::resetConversation()
{
    m_messages.clear();
    invalidateCache();
}

bool LlamaEngine::sendMessage(const QString &text, const SamplingParams &params)
{
    if (!isLoaded()) {
        Q_EMIT errorOccurred(tr("No model loaded"));
        return false;
    }
    m_messages.append({ChatMessage::Role::User, text, {}});
    return generate(params);
}

bool LlamaEngine::regenerate(const SamplingParams &params)
{
    if (!isLoaded()) {
        Q_EMIT errorOccurred(tr("No model loaded"));
        return false;
    }
    if (!m_messages.isEmpty() && m_messages.last().role == ChatMessage::Role::Assistant)
        m_messages.removeLast();
    if (m_messages.isEmpty() || m_messages.last().role != ChatMessage::Role::User) {
        Q_EMIT errorOccurred(tr("Nothing to regenerate"));
        return false;
    }
    invalidateCache();
    return generate(params);
}

void LlamaEngine::invalidateCache()
{
    if (m_ctx)
        llama_memory_clear(llama_get_memory(m_ctx), true);
    m_cacheValid = false;
    m_nPast = 0;
    m_prevFormatted.clear();
    m_pendingSuffix.clear();
}

ChatHistory LlamaEngine::fullHistory() const
{
    ChatHistory all;
    all.reserve(m_messages.size() + 1);
    if (!m_systemPrompt.trimmed().isEmpty())
        all.append({ChatMessage::Role::System, m_systemPrompt, {}});
    all += m_messages;
    return all;
}

std::string LlamaEngine::formatChat(const ChatHistory &messages, bool addAssistant) const
{
    std::vector<std::string> contents;
    contents.reserve(messages.size());
    std::vector<llama_chat_message> chat;
    chat.reserve(messages.size());
    size_t total = 0;
    for (const ChatMessage &m : messages) {
        contents.push_back(m.content.toStdString());
        total += contents.back().size();
    }
    for (int i = 0; i < messages.size(); ++i)
        chat.push_back({ChatMessage::roleName(messages[i].role), contents[size_t(i)].c_str()});

    auto apply = [&](const char *tmpl) -> std::string {
        std::vector<char> buf(total * 2 + 1024);
        int n = llama_chat_apply_template(tmpl, chat.data(), chat.size(), addAssistant,
                                          buf.data(), int(buf.size()));
        if (n > int(buf.size())) {
            buf.resize(size_t(n) + 1);
            n = llama_chat_apply_template(tmpl, chat.data(), chat.size(), addAssistant,
                                          buf.data(), int(buf.size()));
        }
        return n < 0 ? std::string() : std::string(buf.data(), size_t(n));
    };

    // The built-in formatter recognises common templates from the model's Jinja
    // source; fall back to ChatML for anything it doesn't know.
    std::string out = m_template.empty() ? std::string() : apply(m_template.c_str());
    if (out.empty() && !chat.empty())
        out = apply("chatml");
    return out;
}

std::vector<int> LlamaEngine::tokenize(const std::string &text, bool addSpecial) const
{
    std::vector<llama_token> tokens(text.size() + 8);
    int n = llama_tokenize(m_vocab, text.data(), int(text.size()), tokens.data(),
                           int(tokens.size()), addSpecial, true);
    if (n < 0) {
        tokens.resize(size_t(-n));
        n = llama_tokenize(m_vocab, text.data(), int(text.size()), tokens.data(),
                           int(tokens.size()), addSpecial, true);
    }
    tokens.resize(size_t(qMax(0, n)));
    return tokens;
}

bool LlamaEngine::decodeTokens(const std::vector<int> &tokens, bool reportProgress)
{
    const int total = int(tokens.size());
    const int batch = int(llama_n_batch(m_ctx));
    std::vector<llama_token> buf(tokens.begin(), tokens.end());

    for (int i = 0; i < total; i += batch) {
        if (m_abort)
            return false;
        const int n = qMin(batch, total - i);
        if (llama_decode(m_ctx, llama_batch_get_one(buf.data() + i, n)) != 0)
            return false;
        m_nPast += n;
        if (reportProgress)
            Q_EMIT promptProgress(i + n, total);
    }
    return true;
}

llama_sampler *LlamaEngine::createSampler(const SamplingParams &p) const
{
    llama_sampler_chain_params sp = llama_sampler_chain_default_params();
    sp.no_perf = true;
    llama_sampler *chain = llama_sampler_chain_init(sp);

    if (p.repeatPenalty != 1.0f || p.presencePenalty != 0.0f)
        llama_sampler_chain_add(chain, llama_sampler_init_penalties(
                                           llama_vocab_n_tokens(m_vocab), p.repeatLastN, p.repeatPenalty, 0.0f, p.presencePenalty));

    if (p.temperature <= 0.0f) {
        llama_sampler_chain_add(chain, llama_sampler_init_greedy());
        return chain;
    }
    if (p.topK > 0)
        llama_sampler_chain_add(chain, llama_sampler_init_top_k(p.topK));
    if (p.topP < 1.0f)
        llama_sampler_chain_add(chain, llama_sampler_init_top_p(p.topP, 1));
    if (p.minP > 0.0f)
        llama_sampler_chain_add(chain, llama_sampler_init_min_p(p.minP, 1));
    llama_sampler_chain_add(chain, llama_sampler_init_temp(p.temperature));
    llama_sampler_chain_add(chain, llama_sampler_init_dist(p.seed));
    return chain;
}

bool LlamaEngine::trimHistoryToFit(int reserveTokens)
{
    const int nCtx = contextSize();
    auto fits = [&] {
        return int(tokenize(formatChat(fullHistory(), true), true).size()) + reserveTokens <= nCtx;
    };
    // Drop the oldest turns, always keeping the latest user message.
    while (m_messages.size() > 1 && !fits()) {
        m_messages.removeFirst();
        while (m_messages.size() > 1 && m_messages.first().role != ChatMessage::Role::User)
            m_messages.removeFirst();
    }
    return fits();
}

bool LlamaEngine::generate(const SamplingParams &params)
{
    m_abort = false;
    g_aborting = false;
    Q_EMIT generationStarted();

    GenerationStats stats;
    stats.contextSize = contextSize();
    const int nCtx = stats.contextSize;
    const int reserve = params.maxTokens > 0 ? qMin(params.maxTokens, nCtx / 2) : nCtx / 4;

    // Reasoning models: open the think block ourselves (or close it straight
    // away when thinking is off) so the mode doesn't depend on the template.
    std::string assistantPrefix;
    Phase initialPhase = Phase::Undecided;
    if (m_templateThinks) {
        if (params.thinking) {
            assistantPrefix = "<think>\n";
            initialPhase = Phase::Reasoning;
        } else {
            assistantPrefix = "<think>\n\n</think>\n\n";
            initialPhase = Phase::Content;
        }
    }

    std::string formatted = formatChat(fullHistory(), true);
    if (formatted.empty()) {
        Q_EMIT errorOccurred(tr("Failed to apply the chat template"));
        return false;
    }

    // Reuse the cache when the new prompt extends what's already decoded.
    std::string toDecode;
    if (m_cacheValid && formatted.size() >= m_prevFormatted.size()
        && formatted.compare(0, m_prevFormatted.size(), m_prevFormatted) == 0) {
        toDecode = m_pendingSuffix + formatted.substr(m_prevFormatted.size());
    } else {
        invalidateCache();
        toDecode = formatted;
    }
    toDecode += assistantPrefix;
    std::vector<int> tokens = tokenize(toDecode, m_nPast == 0);

    if (m_nPast + int(tokens.size()) + reserve > nCtx) {
        // Out of room: start over with as much recent history as fits.
        invalidateCache();
        if (!trimHistoryToFit(reserve + int(assistantPrefix.size()))) {
            Q_EMIT errorOccurred(tr("The message is too long for the context window (%1 tokens)").arg(nCtx));
            if (!m_messages.isEmpty() && m_messages.last().role == ChatMessage::Role::User)
                m_messages.removeLast();
            return false;
        }
        formatted = formatChat(fullHistory(), true);
        toDecode = formatted + assistantPrefix;
        tokens = tokenize(toDecode, true);
    }

    // --- Prompt processing
    QElapsedTimer timer;
    timer.start();
    stats.promptTokens = int(tokens.size());
    if (!decodeTokens(tokens, true)) {
        const bool aborted = m_abort.load();
        invalidateCache();
        if (aborted) {
            stats.stopped = true;
            Q_EMIT generationFinished(stats, {ChatMessage::Role::Assistant, {}, {}});
            return true;
        }
        Q_EMIT errorOccurred(tr("Failed to process the prompt"));
        return false;
    }
    llama_synchronize(m_ctx); // GPU backends decode asynchronously
    stats.promptMs = double(timer.nsecsElapsed()) / 1e6;

    // --- Generation
    timer.restart();
    llama_sampler *sampler = createSampler(params);
    beginOutput(initialPhase);
    bool ok = true;
    char piece[256];

    while (true) {
        if (m_abort) {
            stats.stopped = true;
            break;
        }
        if (params.maxTokens > 0 && stats.generatedTokens >= params.maxTokens)
            break;
        if (m_nPast >= nCtx) {
            Q_EMIT errorOccurred(tr("Context window is full"));
            break;
        }

        llama_token tok = llama_sampler_sample(sampler, m_ctx, -1);
        if (llama_vocab_is_eog(m_vocab, tok))
            break;

        const int n = llama_token_to_piece(m_vocab, tok, piece, sizeof piece, 0, true);
        if (n > 0)
            feedOutput(std::string(piece, size_t(n)));

        ++stats.generatedTokens;
        if (llama_decode(m_ctx, llama_batch_get_one(&tok, 1)) != 0) {
            stats.stopped = m_abort.load();
            ok = stats.stopped;
            if (!ok)
                Q_EMIT errorOccurred(tr("Decoding failed during generation"));
            break;
        }
        ++m_nPast;
    }
    llama_sampler_free(sampler);
    flushOutput();
    stats.generateMs = double(timer.nsecsElapsed()) / 1e6;

    ChatMessage reply{ChatMessage::Role::Assistant, m_content.trimmed(), m_reasoning.trimmed()};
    m_messages.append(reply);

    // Remember where the cache ends so the next turn only decodes the new
    // part. The formatted chat closes the reply with an end-of-turn marker the
    // model sampled but we never decoded; keep that as a pending suffix.
    const std::string after = formatChat(fullHistory(), false);
    const std::string expectedPrefix = formatted + reply.content.toStdString();
    if (ok && after.compare(0, expectedPrefix.size(), expectedPrefix) == 0) {
        m_prevFormatted = after;
        m_pendingSuffix = after.substr(expectedPrefix.size());
        m_cacheValid = true;
    } else {
        invalidateCache();
    }

    stats.contextUsed = m_nPast;
    Q_EMIT generationFinished(stats, reply);
    return ok;
}

// --- Output parsing -----------------------------------------------------------

void LlamaEngine::beginOutput(Phase initial)
{
    m_phase = initial;
    m_utf8Pending.clear();
    m_textPending.clear();
    m_reasoning.clear();
    m_content.clear();
}

void LlamaEngine::emitText(const QString &text)
{
    if (text.isEmpty())
        return;
    if (m_phase == Phase::Reasoning) {
        // Skip leading blank lines of the reasoning block.
        const QString t = m_reasoning.isEmpty() ? QString(text).remove(QRegularExpression(QStringLiteral("^\\s+"))) : text;
        if (t.isEmpty())
            return;
        m_reasoning += t;
        Q_EMIT reasoningText(t);
    } else {
        const QString t = m_content.isEmpty() ? QString(text).remove(QRegularExpression(QStringLiteral("^\\s+"))) : text;
        if (t.isEmpty())
            return;
        m_content += t;
        Q_EMIT contentText(t);
    }
}

void LlamaEngine::feedOutput(const std::string &piece)
{
    m_utf8Pending += piece;
    const size_t n = completeUtf8Prefix(m_utf8Pending);
    m_textPending += QString::fromUtf8(m_utf8Pending.data(), int(n));
    m_utf8Pending.erase(0, n);

    while (!m_textPending.isEmpty()) {
        switch (m_phase) {
        case Phase::Undecided: {
            // A model may open a think block on its own; wait until we know.
            const QString lead = QString(m_textPending).remove(QRegularExpression(QStringLiteral("^\\s+")));
            if (lead.isEmpty() || (lead.size() < kThinkOpen.size() && kThinkOpen.startsWith(lead)))
                return;
            if (lead.startsWith(kThinkOpen)) {
                m_phase = Phase::Reasoning;
                m_textPending = lead.mid(kThinkOpen.size());
            } else {
                m_phase = Phase::Content;
            }
            break;
        }
        case Phase::Reasoning: {
            const int close = m_textPending.indexOf(kThinkClose);
            if (close >= 0) {
                emitText(m_textPending.left(close));
                m_textPending.remove(0, close + kThinkClose.size());
                m_phase = Phase::Content;
                break;
            }
            const int hold = partialTagSuffix(m_textPending, kThinkClose);
            emitText(m_textPending.left(m_textPending.size() - hold));
            m_textPending = m_textPending.right(hold);
            return;
        }
        case Phase::Content:
            emitText(m_textPending);
            m_textPending.clear();
            return;
        }
    }
}

void LlamaEngine::flushOutput()
{
    m_textPending += QString::fromUtf8(m_utf8Pending.data(), int(m_utf8Pending.size()));
    m_utf8Pending.clear();
    if (m_phase == Phase::Undecided)
        m_phase = Phase::Content;
    emitText(m_textPending);
    m_textPending.clear();
}

} // namespace qtllama
