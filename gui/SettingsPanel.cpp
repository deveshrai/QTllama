#include "SettingsPanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>

using namespace qtllama;

namespace {

const QString kFilePrefix = QStringLiteral("file:");

QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("SectionTitle"));
    return l;
}

QDoubleSpinBox *doubleSpin(double min, double max, double step, int decimals, QWidget *parent)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    return s;
}

QString sizeText(qint64 mb)
{
    return mb >= 1024 ? QStringLiteral("%1 GB").arg(mb / 1024.0, 0, 'f', 1)
                      : QStringLiteral("%1 MB").arg(mb);
}

} // namespace

SettingsPanel::SettingsPanel(ModelRegistry *registry, QWidget *parent)
    : QWidget(parent)
    , m_registry(registry)
{
    setObjectName(QStringLiteral("Sidebar"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("SidebarScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);

    auto *content = new QWidget(scroll);
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(16, 8, 16, 16);
    v->setSpacing(8);
    scroll->setWidget(content);

    // --- Model
    v->addWidget(sectionTitle(tr("Model"), content));
    m_modelCombo = new QComboBox(content);
    m_modelCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    v->addWidget(m_modelCombo);

    m_modelInfo = new QLabel(content);
    m_modelInfo->setObjectName(QStringLiteral("Muted"));
    m_modelInfo->setWordWrap(true);
    v->addWidget(m_modelInfo);

    auto *buttons = new QHBoxLayout;
    m_loadButton = new QPushButton(tr("Load"), content);
    m_loadButton->setObjectName(QStringLiteral("Primary"));
    m_downloadButton = new QPushButton(tr("Download"), content);
    buttons->addWidget(m_loadButton);
    buttons->addWidget(m_downloadButton);
    v->addLayout(buttons);

    m_progress = new QProgressBar(content);
    m_progress->setRange(0, 100);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(8);
    m_progress->setVisible(false);
    v->addWidget(m_progress);
    m_progressLabel = new QLabel(content);
    m_progressLabel->setObjectName(QStringLiteral("Muted"));
    m_progressLabel->setVisible(false);
    v->addWidget(m_progressLabel);

    auto *fileButtons = new QHBoxLayout;
    auto *openButton = new QPushButton(tr("Open .gguf…"), content);
    openButton->setToolTip(tr("Use any GGUF model file from disk"));
    auto *folderButton = new QPushButton(tr("Models folder"), content);
    folderButton->setToolTip(m_registry->modelsDir());
    fileButtons->addWidget(openButton);
    fileButtons->addWidget(folderButton);
    v->addLayout(fileButtons);

    // --- Conversation
    v->addWidget(sectionTitle(tr("Conversation"), content));
    v->addWidget(new QLabel(tr("System prompt"), content));
    m_systemPrompt = new QPlainTextEdit(content);
    m_systemPrompt->setObjectName(QStringLiteral("SystemPrompt"));
    m_systemPrompt->setPlaceholderText(tr("You are a helpful assistant."));
    m_systemPrompt->setFixedHeight(90);
    v->addWidget(m_systemPrompt);
    m_thinking = new QCheckBox(tr("Think before answering"), content);
    m_thinking->setToolTip(tr("Reasoning models (like Qwen3.5) work through the problem first. "
                              "Slower, but better for hard questions."));
    v->addWidget(m_thinking);

    // --- Sampling
    auto *samplingHeader = new QHBoxLayout;
    samplingHeader->addWidget(sectionTitle(tr("Sampling"), content));
    samplingHeader->addStretch(1);
    auto *resetSampling = new QPushButton(tr("Recommended"), content);
    resetSampling->setToolTip(tr("Reset to the model's recommended values for the current mode"));
    samplingHeader->addWidget(resetSampling, 0, Qt::AlignBottom);
    v->addLayout(samplingHeader);

    auto *sampling = new QFormLayout;
    sampling->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_temperature = doubleSpin(0.0, 2.0, 0.05, 2, content);
    m_topP = doubleSpin(0.0, 1.0, 0.05, 2, content);
    m_topK = new QSpinBox(content);
    m_topK->setRange(0, 200);
    m_topK->setSpecialValueText(tr("Off"));
    m_minP = doubleSpin(0.0, 1.0, 0.01, 2, content);
    m_presence = doubleSpin(0.0, 2.0, 0.1, 2, content);
    m_repeat = doubleSpin(0.5, 2.0, 0.05, 2, content);
    m_maxTokens = new QSpinBox(content);
    m_maxTokens->setRange(-1, 131072);
    m_maxTokens->setSingleStep(256);
    m_maxTokens->setSpecialValueText(tr("Unlimited"));
    sampling->addRow(tr("Temperature"), m_temperature);
    sampling->addRow(tr("Top-p"), m_topP);
    sampling->addRow(tr("Top-k"), m_topK);
    sampling->addRow(tr("Min-p"), m_minP);
    sampling->addRow(tr("Presence penalty"), m_presence);
    sampling->addRow(tr("Repeat penalty"), m_repeat);
    sampling->addRow(tr("Max tokens"), m_maxTokens);
    v->addLayout(sampling);

    // --- Performance
    v->addWidget(sectionTitle(tr("Performance"), content));
    auto *perf = new QFormLayout;
    perf->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_contextSize = new QComboBox(content);
    for (int n : {2048, 4096, 8192, 16384, 32768, 65536, 131072})
        m_contextSize->addItem(QStringLiteral("%1 tokens").arg(n), n);
    m_threads = new QSpinBox(content);
    m_threads->setRange(0, 256);
    m_threads->setSpecialValueText(tr("Auto (%1)").arg(defaultThreadCount()));
    m_gpuLayers = new QSpinBox(content);
    m_gpuLayers->setRange(-1, 999);
    m_gpuLayers->setSpecialValueText(tr("All"));
    m_gpuLayers->setToolTip(tr("Layers offloaded to the GPU. 0 = CPU only. Ignored on CPU-only builds."));
    perf->addRow(tr("Context"), m_contextSize);
    perf->addRow(tr("Threads"), m_threads);
    perf->addRow(tr("GPU layers"), m_gpuLayers);
    v->addLayout(perf);
    auto *perfNote = new QLabel(tr("Changes apply when the model is reloaded."), content);
    perfNote->setObjectName(QStringLiteral("Muted"));
    perfNote->setWordWrap(true);
    v->addWidget(perfNote);
    m_autoLoad = new QCheckBox(tr("Load model on startup"), content);
    v->addWidget(m_autoLoad);

    // --- Appearance
    v->addWidget(sectionTitle(tr("Appearance"), content));
    auto *look = new QFormLayout;
    look->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_theme = new QComboBox(content);
    m_theme->addItem(tr("System"), QStringLiteral("system"));
    m_theme->addItem(tr("Light"), QStringLiteral("light"));
    m_theme->addItem(tr("Dark"), QStringLiteral("dark"));
    m_fontSize = new QSpinBox(content);
    m_fontSize->setRange(8, 28);
    m_fontSize->setSuffix(QStringLiteral(" pt"));
    look->addRow(tr("Theme"), m_theme);
    look->addRow(tr("Text size"), m_fontSize);
    v->addLayout(look);
    v->addStretch(1);

    loadSettings();
    refreshModels();

    connect(m_modelCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        updateModelInfo();
        applyRecommendedSampling();
        Q_EMIT selectionChanged();
    });
    connect(m_loadButton, &QPushButton::clicked, this, [this] {
        if (m_state == ModelState::Loaded && selectedModelPath() == m_loadedPath)
            Q_EMIT unloadRequested();
        else
            Q_EMIT loadRequested();
    });
    connect(m_downloadButton, &QPushButton::clicked, this, [this] {
        if (m_downloading)
            Q_EMIT cancelDownloadRequested();
        else
            Q_EMIT downloadRequested();
    });
    connect(openButton, &QPushButton::clicked, this, &SettingsPanel::openFileRequested);
    connect(folderButton, &QPushButton::clicked, this, &SettingsPanel::openModelsFolderRequested);
    connect(m_thinking, &QCheckBox::toggled, this, [this] { applyRecommendedSampling(); });
    connect(resetSampling, &QPushButton::clicked, this, [this] { applyRecommendedSampling(); });
    connect(m_theme, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        Q_EMIT themeChanged(themeMode());
    });
    connect(m_fontSize, qOverload<int>(&QSpinBox::valueChanged), this, &SettingsPanel::fontSizeChanged);
}

// --- Accessors ------------------------------------------------------------------

const ModelInfo *SettingsPanel::selectedModel() const
{
    const QString key = m_modelCombo->currentData().toString();
    if (key.startsWith(kFilePrefix))
        return m_registry->find(QFileInfo(key.mid(kFilePrefix.size())).fileName());
    return m_registry->find(key);
}

QString SettingsPanel::selectedModelPath() const
{
    const QString key = m_modelCombo->currentData().toString();
    if (key.startsWith(kFilePrefix)) {
        const QString path = key.mid(kFilePrefix.size());
        return QFileInfo::exists(path) ? path : QString();
    }
    const ModelInfo *m = m_registry->find(key);
    return m && m_registry->isDownloaded(*m) ? m_registry->localPath(*m) : QString();
}

QString SettingsPanel::selectedModelName() const
{
    const QString key = m_modelCombo->currentData().toString();
    if (key.startsWith(kFilePrefix))
        return QFileInfo(key.mid(kFilePrefix.size())).completeBaseName();
    const ModelInfo *m = m_registry->find(key);
    return m ? m->name : key;
}

LoadParams SettingsPanel::loadParams() const
{
    LoadParams p;
    p.modelPath = selectedModelPath();
    p.contextSize = m_contextSize->currentData().toInt();
    p.threads = m_threads->value();
    p.gpuLayers = m_gpuLayers->value();
    return p;
}

SamplingParams SettingsPanel::samplingParams() const
{
    SamplingParams p;
    p.temperature = float(m_temperature->value());
    p.topP = float(m_topP->value());
    p.topK = m_topK->value();
    p.minP = float(m_minP->value());
    p.presencePenalty = float(m_presence->value());
    p.repeatPenalty = float(m_repeat->value());
    p.maxTokens = m_maxTokens->value();
    p.thinking = thinking();
    return p;
}

QString SettingsPanel::systemPrompt() const
{
    return m_systemPrompt->toPlainText().trimmed();
}

bool SettingsPanel::thinking() const
{
    return m_thinking->isChecked();
}

Theme::Mode SettingsPanel::themeMode() const
{
    return Theme::modeFromString(m_theme->currentData().toString());
}

int SettingsPanel::fontSize() const
{
    return m_fontSize->value();
}

bool SettingsPanel::autoLoad() const
{
    return m_autoLoad->isChecked();
}

// --- Model list -------------------------------------------------------------------

void SettingsPanel::refreshModels()
{
    const QString current = m_modelCombo->count() ? m_modelCombo->currentData().toString()
                                                  : QSettings().value("model/selected").toString();
    m_registry->reload();

    QSignalBlocker block(m_modelCombo);
    m_modelCombo->clear();
    for (const ModelInfo &m : m_registry->models()) {
        const bool have = m_registry->isDownloaded(m);
        QString label = m.name;
        if (!m.quant.isEmpty())
            label += QStringLiteral(" · ") + m.quant;
        if (!have)
            label += tr("  (not downloaded)");
        m_modelCombo->addItem(label, m.id);
    }
    for (const QString &path : std::as_const(m_extraFiles)) {
        if (m_registry->find(QFileInfo(path).fileName()) && QFileInfo(path).absolutePath() == m_registry->modelsDir())
            continue;
        m_modelCombo->addItem(QFileInfo(path).fileName(), kFilePrefix + path);
    }

    int idx = m_modelCombo->findData(current);
    if (idx < 0)
        idx = m_modelCombo->findData(m_registry->defaultModelId());
    m_modelCombo->setCurrentIndex(qMax(0, idx));
    updateModelInfo();
}

void SettingsPanel::addModelFile(const QString &path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    if (!m_extraFiles.contains(abs))
        m_extraFiles.prepend(abs);
    while (m_extraFiles.size() > 8)
        m_extraFiles.removeLast();
    refreshModels();
    int idx = m_modelCombo->findData(kFilePrefix + abs);
    if (idx < 0)
        idx = m_modelCombo->findData(m_registry->find(QFileInfo(abs).fileName())
                                         ? m_registry->find(QFileInfo(abs).fileName())->id
                                         : QString());
    if (idx >= 0)
        m_modelCombo->setCurrentIndex(idx);
}

void SettingsPanel::updateModelInfo()
{
    const ModelInfo *m = selectedModel();
    const QString path = selectedModelPath();
    const bool have = !path.isEmpty();
    const bool canDownload = m && !m->isCustom && m->downloadUrl().isValid() && !have;

    QStringList parts;
    if (m) {
        if (!m->description.isEmpty())
            parts << m->description;
        parts << sizeText(m->sizeMB);
    } else if (have) {
        parts << sizeText(QFileInfo(path).size() / (1024 * 1024));
    }
    if (have && m_state == ModelState::Loaded && path == m_loadedPath)
        parts << tr("Loaded");
    else if (!have)
        parts << tr("Not downloaded");
    m_modelInfo->setText(parts.join(QStringLiteral(" · ")));

    const bool loadedThis = m_state == ModelState::Loaded && path == m_loadedPath;
    m_loadButton->setText(loadedThis ? tr("Unload") : (m_state == ModelState::Loaded ? tr("Switch") : tr("Load")));
    m_loadButton->setObjectName(loadedThis ? QString() : QStringLiteral("Primary"));
    m_loadButton->style()->unpolish(m_loadButton);
    m_loadButton->style()->polish(m_loadButton);
    m_loadButton->setEnabled(have && m_state != ModelState::Loading && !m_downloading);

    m_downloadButton->setVisible(canDownload || m_downloading);
    m_downloadButton->setText(m_downloading ? tr("Cancel") : tr("Download"));

    if (m && !m->supportsThinking) {
        m_thinking->setEnabled(false);
    } else {
        m_thinking->setEnabled(true);
    }
}

void SettingsPanel::applyRecommendedSampling()
{
    const ModelInfo *m = selectedModel();
    const SamplingParams p = m ? m->recommendedSampling(thinking()) : SamplingParams{};
    m_temperature->setValue(p.temperature);
    m_topP->setValue(p.topP);
    m_topK->setValue(p.topK);
    m_minP->setValue(p.minP);
    m_presence->setValue(p.presencePenalty);
    m_repeat->setValue(p.repeatPenalty);
}

// --- State from the main window ---------------------------------------------------

void SettingsPanel::setModelState(ModelState state, const QString &loadedPath)
{
    m_state = state;
    m_loadedPath = loadedPath;
    if (state != ModelState::Loading) {
        m_progress->setVisible(m_downloading);
        m_progressLabel->setVisible(m_downloading);
    }
    m_modelCombo->setEnabled(state != ModelState::Loading);
    updateModelInfo();
}

void SettingsPanel::setLoadProgress(int percent)
{
    m_progress->setVisible(true);
    m_progress->setValue(percent);
    m_progressLabel->setVisible(true);
    m_progressLabel->setText(tr("Loading… %1%").arg(percent));
}

void SettingsPanel::setDownloadProgress(qint64 received, qint64 total)
{
    m_progress->setVisible(true);
    m_progressLabel->setVisible(true);
    const double gotMB = received / (1024.0 * 1024.0);
    if (total > 0) {
        m_progress->setRange(0, 1000);
        m_progress->setValue(int(received * 1000 / total));
        m_progressLabel->setText(tr("Downloading… %1 / %2 MB")
                                     .arg(gotMB, 0, 'f', 0)
                                     .arg(total / (1024.0 * 1024.0), 0, 'f', 0));
    } else {
        m_progress->setRange(0, 0);
        m_progressLabel->setText(tr("Downloading… %1 MB").arg(gotMB, 0, 'f', 0));
    }
}

void SettingsPanel::setDownloadRunning(bool running)
{
    m_downloading = running;
    if (!running) {
        m_progress->setRange(0, 100);
        m_progress->setVisible(false);
        m_progressLabel->setVisible(false);
    }
    updateModelInfo();
}

// --- Persistence ------------------------------------------------------------------

void SettingsPanel::loadSettings()
{
    QSettings s;
    m_extraFiles = s.value("model/extraFiles").toStringList();
    m_systemPrompt->setPlainText(s.value("chat/systemPrompt").toString());
    m_thinking->setChecked(s.value("chat/thinking", true).toBool());

    const SamplingParams def;
    m_temperature->setValue(s.value("sampling/temperature", 1.0).toDouble());
    m_topP->setValue(s.value("sampling/topP", 0.95).toDouble());
    m_topK->setValue(s.value("sampling/topK", 20).toInt());
    m_minP->setValue(s.value("sampling/minP", 0.0).toDouble());
    m_presence->setValue(s.value("sampling/presencePenalty", 1.5).toDouble());
    m_repeat->setValue(s.value("sampling/repeatPenalty", def.repeatPenalty).toDouble());
    m_maxTokens->setValue(s.value("sampling/maxTokens", -1).toInt());

    const int ctxIdx = m_contextSize->findData(s.value("perf/context", 8192).toInt());
    m_contextSize->setCurrentIndex(ctxIdx >= 0 ? ctxIdx : 2);
    m_threads->setValue(s.value("perf/threads", 0).toInt());
    m_gpuLayers->setValue(s.value("perf/gpuLayers", -1).toInt());
    m_autoLoad->setChecked(s.value("perf/autoLoad", true).toBool());

    const int themeIdx = m_theme->findData(s.value("ui/theme", "system").toString());
    m_theme->setCurrentIndex(qMax(0, themeIdx));
    m_fontSize->setValue(s.value("ui/fontSize", QApplication::font().pointSize()).toInt());
}

void SettingsPanel::saveSettings() const
{
    QSettings s;
    s.setValue("model/selected", m_modelCombo->currentData());
    s.setValue("model/extraFiles", m_extraFiles);
    s.setValue("chat/systemPrompt", m_systemPrompt->toPlainText());
    s.setValue("chat/thinking", m_thinking->isChecked());
    s.setValue("sampling/temperature", m_temperature->value());
    s.setValue("sampling/topP", m_topP->value());
    s.setValue("sampling/topK", m_topK->value());
    s.setValue("sampling/minP", m_minP->value());
    s.setValue("sampling/presencePenalty", m_presence->value());
    s.setValue("sampling/repeatPenalty", m_repeat->value());
    s.setValue("sampling/maxTokens", m_maxTokens->value());
    s.setValue("perf/context", m_contextSize->currentData());
    s.setValue("perf/threads", m_threads->value());
    s.setValue("perf/gpuLayers", m_gpuLayers->value());
    s.setValue("perf/autoLoad", m_autoLoad->isChecked());
    s.setValue("ui/theme", m_theme->currentData());
    s.setValue("ui/fontSize", m_fontSize->value());
}
