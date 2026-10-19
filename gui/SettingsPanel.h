#pragma once

#include "ModelRegistry.h"
#include "Theme.h"
#include "Types.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;

// Sidebar: model selection/download, conversation and sampling settings,
// performance and appearance. Persists everything with QSettings.
class SettingsPanel : public QWidget
{
    Q_OBJECT

public:
    enum class ModelState { Unloaded, Loading, Loaded };

    explicit SettingsPanel(qtllama::ModelRegistry *registry, QWidget *parent = nullptr);

    // Selected model: a registry entry, or a .gguf path opened from disk.
    const qtllama::ModelInfo *selectedModel() const;
    QString selectedModelPath() const; // empty when not downloaded
    QString selectedModelName() const;

    qtllama::LoadParams loadParams() const;
    qtllama::SamplingParams samplingParams() const;
    QString systemPrompt() const;
    bool thinking() const;
    Theme::Mode themeMode() const;
    int fontSize() const;
    bool autoLoad() const;

    void refreshModels();
    void addModelFile(const QString &path); // also selects it
    void setModelState(ModelState state, const QString &loadedPath = {});
    void setLoadProgress(int percent);
    void setDownloadProgress(qint64 received, qint64 total);
    void setDownloadRunning(bool running);
    void saveSettings() const;

Q_SIGNALS:
    void loadRequested();
    void unloadRequested();
    void downloadRequested();
    void cancelDownloadRequested();
    void openFileRequested();
    void openModelsFolderRequested();
    void themeChanged(Theme::Mode mode);
    void fontSizeChanged(int pointSize);
    void selectionChanged();

private:
    void loadSettings();
    void updateModelInfo();
    void applyRecommendedSampling();

    qtllama::ModelRegistry *m_registry;
    ModelState m_state = ModelState::Unloaded;
    QString m_loadedPath;
    bool m_downloading = false;
    QStringList m_extraFiles;

    QComboBox *m_modelCombo = nullptr;
    QLabel *m_modelInfo = nullptr;
    QPushButton *m_loadButton = nullptr;
    QPushButton *m_downloadButton = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_progressLabel = nullptr;

    QPlainTextEdit *m_systemPrompt = nullptr;
    QCheckBox *m_thinking = nullptr;

    QDoubleSpinBox *m_temperature = nullptr;
    QDoubleSpinBox *m_topP = nullptr;
    QSpinBox *m_topK = nullptr;
    QDoubleSpinBox *m_minP = nullptr;
    QDoubleSpinBox *m_presence = nullptr;
    QDoubleSpinBox *m_repeat = nullptr;
    QSpinBox *m_maxTokens = nullptr;

    QComboBox *m_contextSize = nullptr;
    QSpinBox *m_threads = nullptr;
    QSpinBox *m_gpuLayers = nullptr;
    QCheckBox *m_autoLoad = nullptr;

    QComboBox *m_theme = nullptr;
    QSpinBox *m_fontSize = nullptr;
};
