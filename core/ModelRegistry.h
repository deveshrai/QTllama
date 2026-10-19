#pragma once

#include "Types.h"

#include <QString>
#include <QUrl>
#include <QVector>

namespace qtllama {

struct ModelInfo
{
    QString id;
    QString name;
    QString description;
    QString repo;  // Hugging Face repo, e.g. "bartowski/Qwen_Qwen3.5-4B-GGUF"
    QString file;  // GGUF file name inside the repo / models directory
    QString quant;
    qint64 sizeMB = 0;
    int contextLength = 8192;
    bool supportsThinking = false;
    bool isCustom = false; // a .gguf found on disk that isn't in the registry

    SamplingParams thinkingSampling;
    SamplingParams instructSampling;

    QUrl downloadUrl() const;
    // Recommended sampling for the requested mode; keeps `thinking` in sync.
    SamplingParams recommendedSampling(bool thinking) const;
};

// Known models (from models.json) plus any extra .gguf files in the models directory.
class ModelRegistry
{
public:
    // Loads the built-in registry, then <modelsDir>/models.json if present
    // (entries with the same id override the built-in ones), then scans
    // modelsDir for unlisted .gguf files.
    explicit ModelRegistry(const QString &modelsDir = defaultModelsDir());

    void reload();

    QString modelsDir() const { return m_modelsDir; }
    void setModelsDir(const QString &dir);

    const QVector<ModelInfo> &models() const { return m_models; }
    QString defaultModelId() const { return m_defaultId; }

    // Finds by id, by file name, or returns nullptr.
    const ModelInfo *find(const QString &idOrFile) const;
    const ModelInfo *defaultModel() const { return find(m_defaultId); }

    QString localPath(const ModelInfo &model) const;
    bool isDownloaded(const ModelInfo &model) const;

    // Resolves a CLI/GUI model argument: a registry id, a file in the models
    // directory, or a path to a .gguf. Returns an empty string if not found.
    QString resolvePath(const QString &idOrPath) const;

    // QTLLAMA_MODELS_DIR, else <source>/models when building from the
    // workspace, else the per-user app data directory.
    static QString defaultModelsDir();

private:
    void loadJson(const QByteArray &json);
    void scanDirectory();

    QString m_modelsDir;
    QString m_defaultId;
    QVector<ModelInfo> m_models;
};

} // namespace qtllama
