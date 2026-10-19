#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>

namespace qtllama {

struct ChatMessage
{
    enum class Role { System, User, Assistant };

    Role role = Role::User;
    QString content;
    QString reasoning; // assistant "thinking" text; never sent back to the model

    static const char *roleName(Role role);
};

using ChatHistory = QVector<ChatMessage>;

// How the model is loaded. Changing these requires a reload.
struct LoadParams
{
    QString modelPath;
    int contextSize = 8192;
    int threads = 0;       // 0 = pick automatically
    int batchThreads = 0;  // 0 = pick automatically
    int gpuLayers = -1;    // -1 = offload everything the backend can take
    bool useMmap = true;
    bool flashAttention = true;
};

// How each reply is sampled. Can change between turns.
struct SamplingParams
{
    float temperature = 0.7f;
    float topP = 0.8f;
    int topK = 20;
    float minP = 0.0f;
    float presencePenalty = 0.0f;
    float repeatPenalty = 1.0f;
    int repeatLastN = 64;
    int maxTokens = -1; // -1 = until end of turn or context is full
    quint32 seed = 0xFFFFFFFF; // 0xFFFFFFFF = random
    bool thinking = true;      // let reasoning models think before answering
};

struct GenerationStats
{
    int promptTokens = 0;
    int generatedTokens = 0;
    double promptMs = 0.0;
    double generateMs = 0.0;
    int contextUsed = 0;
    int contextSize = 0;
    bool stopped = false; // ended by user request rather than end-of-turn

    double promptTokensPerSecond() const
    {
        return promptMs > 0 ? promptTokens * 1000.0 / promptMs : 0.0;
    }
    double tokensPerSecond() const
    {
        return generateMs > 0 ? generatedTokens * 1000.0 / generateMs : 0.0;
    }
};

// Picks a thread count that suits big.LITTLE phones/tablets as well as desktops.
int defaultThreadCount();

} // namespace qtllama

Q_DECLARE_METATYPE(qtllama::ChatMessage)
Q_DECLARE_METATYPE(qtllama::ChatHistory)
Q_DECLARE_METATYPE(qtllama::LoadParams)
Q_DECLARE_METATYPE(qtllama::SamplingParams)
Q_DECLARE_METATYPE(qtllama::GenerationStats)
