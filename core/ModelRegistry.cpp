#include "ModelRegistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QThread>

// Resources in a static library must be registered explicitly, outside any namespace.
static void initCoreResources()
{
    static const bool done = [] { Q_INIT_RESOURCE(core); return true; }();
    Q_UNUSED(done);
}

namespace qtllama {

const char *ChatMessage::roleName(Role role)
{
    switch (role) {
    case Role::System: return "system";
    case Role::User: return "user";
    case Role::Assistant: return "assistant";
    }
    return "user";
}

int defaultThreadCount()
{
    // On 8+ core big.LITTLE SoCs (and Apple Silicon) using every core is
    // slower than using the performance cores, so use half above 4 cores.
    const int cores = qMax(1, QThread::idealThreadCount());
    return cores > 4 ? cores / 2 : cores;
}

static SamplingParams samplingFromJson(const QJsonObject &o, bool thinking)
{
    SamplingParams p;
    p.temperature = float(o.value("temperature").toDouble(p.temperature));
    p.topP = float(o.value("topP").toDouble(p.topP));
    p.topK = o.value("topK").toInt(p.topK);
    p.minP = float(o.value("minP").toDouble(p.minP));
    p.presencePenalty = float(o.value("presencePenalty").toDouble(p.presencePenalty));
    p.repeatPenalty = float(o.value("repeatPenalty").toDouble(p.repeatPenalty));
    p.thinking = thinking;
    return p;
}

QUrl ModelInfo::downloadUrl() const
{
    if (repo.isEmpty() || file.isEmpty())
        return {};
    return QUrl(QStringLiteral("https://huggingface.co/%1/resolve/main/%2").arg(repo, file));
}

SamplingParams ModelInfo::recommendedSampling(bool thinking) const
{
    SamplingParams p = (thinking && supportsThinking) ? thinkingSampling : instructSampling;
    p.thinking = thinking && supportsThinking;
    return p;
}

ModelRegistry::ModelRegistry(const QString &modelsDir)
    : m_modelsDir(modelsDir)
{
    reload();
}

void ModelRegistry::setModelsDir(const QString &dir)
{
    m_modelsDir = dir;
    reload();
}

QString ModelRegistry::defaultModelsDir()
{
    const QByteArray env = qgetenv("QTLLAMA_MODELS_DIR");
    if (!env.isEmpty())
        return QDir::cleanPath(QString::fromLocal8Bit(env));

#ifdef QTLLAMA_SOURCE_DIR
    const QFileInfo workspace(QStringLiteral(QTLLAMA_SOURCE_DIR "/models"));
    if (workspace.isDir() && workspace.isWritable())
        return workspace.absoluteFilePath();
#endif

    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/models");
}

void ModelRegistry::reload()
{
    m_models.clear();
    m_defaultId.clear();

    initCoreResources();
    QFile builtin(QStringLiteral(":/qtllama/models.json"));
    if (builtin.open(QIODevice::ReadOnly))
        loadJson(builtin.readAll());

    QFile user(QDir(m_modelsDir).filePath(QStringLiteral("models.json")));
    if (user.open(QIODevice::ReadOnly))
        loadJson(user.readAll());

    scanDirectory();
}

void ModelRegistry::loadJson(const QByteArray &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (root.contains("default"))
        m_defaultId = root.value("default").toString();

    const QJsonArray models = root.value("models").toArray();
    for (const QJsonValue &v : models) {
        const QJsonObject o = v.toObject();
        ModelInfo m;
        m.id = o.value("id").toString();
        if (m.id.isEmpty())
            continue;
        m.name = o.value("name").toString(m.id);
        m.description = o.value("description").toString();
        m.repo = o.value("repo").toString();
        m.file = o.value("file").toString();
        m.quant = o.value("quant").toString();
        m.sizeMB = o.value("sizeMB").toInteger();
        m.contextLength = o.value("contextLength").toInt(8192);
        m.supportsThinking = o.value("thinking").toBool(false);

        const QJsonObject sampling = o.value("sampling").toObject();
        m.instructSampling = samplingFromJson(sampling.value("instruct").toObject(), false);
        m.thinkingSampling = sampling.contains("thinking")
                                 ? samplingFromJson(sampling.value("thinking").toObject(), true)
                                 : m.instructSampling;

        auto existing = std::find_if(m_models.begin(), m_models.end(),
                                     [&](const ModelInfo &x) { return x.id == m.id; });
        if (existing != m_models.end())
            *existing = m;
        else
            m_models.append(m);
    }
}

void ModelRegistry::scanDirectory()
{
    const QDir dir(m_modelsDir);
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.gguf")}, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        // Skip multimodal projectors and already-registered files.
        if (fi.fileName().startsWith(QLatin1String("mmproj"), Qt::CaseInsensitive))
            continue;
        if (find(fi.fileName()))
            continue;

        ModelInfo m;
        m.id = fi.completeBaseName();
        m.name = fi.completeBaseName();
        m.description = QStringLiteral("Local file");
        m.file = fi.fileName();
        m.sizeMB = fi.size() / (1024 * 1024);
        m.isCustom = true;
        // Unknown models: assume a reasoning-capable chat model and let the
        // user switch thinking off; neutral sampling.
        m.supportsThinking = true;
        m.instructSampling = SamplingParams{};
        m.instructSampling.thinking = false;
        m.thinkingSampling = SamplingParams{};
        m.thinkingSampling.thinking = true;
        m_models.append(m);
    }
}

const ModelInfo *ModelRegistry::find(const QString &idOrFile) const
{
    for (const ModelInfo &m : m_models) {
        if (m.id.compare(idOrFile, Qt::CaseInsensitive) == 0 || m.file == idOrFile)
            return &m;
    }
    return nullptr;
}

QString ModelRegistry::localPath(const ModelInfo &model) const
{
    return QDir(m_modelsDir).filePath(model.file);
}

bool ModelRegistry::isDownloaded(const ModelInfo &model) const
{
    return QFileInfo::exists(localPath(model));
}

QString ModelRegistry::resolvePath(const QString &idOrPath) const
{
    if (idOrPath.isEmpty()) {
        const ModelInfo *m = defaultModel();
        return m && isDownloaded(*m) ? localPath(*m) : QString();
    }
    if (const ModelInfo *m = find(idOrPath))
        return isDownloaded(*m) ? localPath(*m) : QString();

    const QFileInfo direct(idOrPath);
    if (direct.isFile())
        return direct.absoluteFilePath();

    const QFileInfo inDir(QDir(m_modelsDir).filePath(idOrPath));
    if (inDir.isFile())
        return inDir.absoluteFilePath();

    return {};
}

} // namespace qtllama
