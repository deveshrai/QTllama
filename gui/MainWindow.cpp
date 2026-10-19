#include "MainWindow.h"

#include "ChatView.h"
#include "LlamaEngine.h"
#include "MessageWidget.h"
#include "ModelDownloader.h"
#include "SettingsPanel.h"
#include "Theme.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QStatusBar>
#include <QTextStream>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

using namespace qtllama;

// --- Composer ---------------------------------------------------------------------

Composer::Composer(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setPlaceholderText(tr("Message…  (Enter to send, Shift+Enter for a new line)"));
    setTabChangesFocus(true);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(document(), &QTextDocument::contentsChanged, this, &Composer::updateHeight);
    updateHeight();
}

void Composer::keyPressEvent(QKeyEvent *event)
{
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    if (enter && !(event->modifiers() & Qt::ShiftModifier)) {
        Q_EMIT submitted();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void Composer::updateHeight()
{
    const QFontMetrics fm(font());
    const int lines = qBound(1, int(document()->size().height()), 8);
    const int margins = int(document()->documentMargin() * 2) + contentsMargins().top()
                        + contentsMargins().bottom();
    setFixedHeight(lines * fm.lineSpacing() + margins);
}

// --- MainWindow -------------------------------------------------------------------

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("QTllama"));

    m_engine = new LlamaEngine;
    m_engine->moveToThread(&m_engineThread);
    m_engineThread.setObjectName(QStringLiteral("llama-engine"));
    m_engineThread.start();

    m_downloader = new ModelDownloader(this);

    buildUi();
    connectEngine();

    QSettings s;
    if (!restoreGeometry(s.value("ui/geometry").toByteArray()))
        resize(1180, 780);
    if (!m_splitter->restoreState(s.value("ui/splitter").toByteArray()))
        m_splitter->setSizes({300, 880});

    updateWelcome();
    updateComposerState();

    if (m_panel->autoLoad() && !m_panel->selectedModelPath().isEmpty())
        QMetaObject::invokeMethod(this, [this] { loadSelectedModel(); }, Qt::QueuedConnection);
}

MainWindow::~MainWindow()
{
    m_engine->requestStop();
    m_engineThread.quit();
    m_engineThread.wait();
    delete m_engine;
}

void MainWindow::buildUi()
{
    // --- Top bar
    auto *bar = new QToolBar(this);
    bar->setObjectName(QStringLiteral("TopBar"));
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->toggleViewAction()->setVisible(false);
    addToolBar(Qt::TopToolBarArea, bar);

    QAction *sidebarAct = bar->addAction(QStringLiteral("☰"), this, &MainWindow::toggleSidebar);
    sidebarAct->setToolTip(tr("Show/hide settings (Ctrl+B)"));
    sidebarAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    QAction *newAct = bar->addAction(tr("New chat"), this, &MainWindow::newChat);
    newAct->setShortcut(QKeySequence::New);
    newAct->setToolTip(tr("Start a new conversation (Ctrl+N)"));
    QAction *exportAct = bar->addAction(tr("Export"), this, &MainWindow::exportChat);
    exportAct->setToolTip(tr("Save the conversation as Markdown"));

    auto *spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);
    m_modelBadge = new QLabel(bar);
    m_modelBadge->setObjectName(QStringLiteral("ModelBadge"));
    bar->addWidget(m_modelBadge);

    // --- Body
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(true);
    m_splitter->setHandleWidth(1);

    m_panel = new SettingsPanel(&m_registry, m_splitter);
    m_panel->setMinimumWidth(260);
    m_panel->setMaximumWidth(420);

    auto *central = new QWidget(m_splitter);
    central->setObjectName(QStringLiteral("Central"));
    auto *cv = new QVBoxLayout(central);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);

    m_chat = new ChatView(central);
    cv->addWidget(m_chat, 1);

    auto *composerRow = new QHBoxLayout;
    composerRow->setContentsMargins(16, 8, 16, 14);
    auto *composer = new QFrame(central);
    composer->setObjectName(QStringLiteral("Composer"));
    composer->setMaximumWidth(880);
    auto *ch = new QHBoxLayout(composer);
    ch->setContentsMargins(12, 6, 8, 6);
    m_input = new Composer(composer);
    ch->addWidget(m_input, 1);
    m_sendButton = new QPushButton(QStringLiteral("↑"), composer);
    m_sendButton->setObjectName(QStringLiteral("SendButton"));
    m_sendButton->setCursor(Qt::PointingHandCursor);
    ch->addWidget(m_sendButton, 0, Qt::AlignBottom);
    composerRow->addWidget(composer);
    cv->addLayout(composerRow);

    m_splitter->addWidget(m_panel);
    m_splitter->addWidget(central);
    m_splitter->setStretchFactor(1, 1);
    setCentralWidget(m_splitter);

    // --- Status bar
    m_statusLeft = new QLabel(this);
    m_statusRight = new QLabel(this);
    statusBar()->addWidget(m_statusLeft, 1);
    statusBar()->addPermanentWidget(m_statusRight);
    statusBar()->setSizeGripEnabled(false);

    // --- Wiring
    connect(m_input, &Composer::submitted, this, &MainWindow::send);
    connect(m_input, &QPlainTextEdit::textChanged, this, &MainWindow::updateComposerState);
    connect(m_sendButton, &QPushButton::clicked, this, [this] { m_generating ? stop() : send(); });
    connect(m_chat, &ChatView::regenerateRequested, this, &MainWindow::regenerate);
    auto *stopShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(stopShortcut, &QShortcut::activated, this, &MainWindow::stop);

    connect(m_panel, &SettingsPanel::loadRequested, this, &MainWindow::loadSelectedModel);
    connect(m_panel, &SettingsPanel::unloadRequested, this, &MainWindow::unloadModel);
    connect(m_panel, &SettingsPanel::downloadRequested, this, &MainWindow::downloadSelectedModel);
    connect(m_panel, &SettingsPanel::cancelDownloadRequested, m_downloader, &ModelDownloader::cancel);
    connect(m_panel, &SettingsPanel::openFileRequested, this, &MainWindow::openModelFile);
    connect(m_panel, &SettingsPanel::openModelsFolderRequested, this, &MainWindow::openModelsFolder);
    connect(m_panel, &SettingsPanel::selectionChanged, this, &MainWindow::updateWelcome);
    connect(m_panel, &SettingsPanel::themeChanged, this, [](Theme::Mode mode) {
        Theme::apply(*qApp, mode);
    });
    connect(m_panel, &SettingsPanel::fontSizeChanged, this, [](int pt) {
        QFont f = QApplication::font();
        f.setPointSize(pt);
        QApplication::setFont(f);
    });

    connect(m_downloader, &ModelDownloader::progress, m_panel, &SettingsPanel::setDownloadProgress);
    connect(m_downloader, &ModelDownloader::finished, this, [this](const QString &path) {
        m_panel->setDownloadRunning(false);
        m_panel->refreshModels();
        m_statusLeft->setText(tr("Downloaded %1").arg(QFileInfo(path).fileName()));
        updateWelcome();
        if (!m_loaded && !m_loading)
            loadSelectedModel();
    });
    connect(m_downloader, &ModelDownloader::failed, this, [this](const QString &error) {
        m_panel->setDownloadRunning(false);
        m_statusLeft->setText(error);
        if (error != tr("Download cancelled"))
            QMessageBox::warning(this, tr("Download failed"), error);
    });
}

void MainWindow::connectEngine()
{
    // The engine lives in m_engineThread, so these are queued connections.
    connect(m_engine, &LlamaEngine::loadProgress, m_panel, &SettingsPanel::setLoadProgress);
    connect(m_engine, &LlamaEngine::modelLoaded, this, [this](const QString &desc, int ctx) {
        m_loading = false;
        m_loaded = true;
        m_panel->setModelState(SettingsPanel::ModelState::Loaded, m_loadedPath);
        m_modelBadge->setText(m_panel->selectedModelName());
        m_modelBadge->setToolTip(desc);
        m_statusLeft->setText(tr("Ready"));
        m_statusRight->setText(tr("ctx %1").arg(ctx));
        updateWelcome();
        updateComposerState();
        m_input->setFocus();
        if (!m_pendingMessage.isEmpty()) {
            m_input->setPlainText(m_pendingMessage);
            m_pendingMessage.clear();
            send();
        }
    });
    connect(m_engine, &LlamaEngine::modelLoadFailed, this, [this](const QString &error) {
        m_loading = false;
        m_loaded = false;
        m_loadedPath.clear();
        m_pendingMessage.clear();
        m_panel->setModelState(SettingsPanel::ModelState::Unloaded);
        m_modelBadge->clear();
        m_statusLeft->setText(error);
        updateComposerState();
        QMessageBox::warning(this, tr("Could not load model"), error);
    });
    connect(m_engine, &LlamaEngine::modelUnloaded, this, [this] {
        if (m_loading)
            return; // part of switching models
        m_loaded = false;
        m_loadedPath.clear();
        m_panel->setModelState(SettingsPanel::ModelState::Unloaded);
        m_modelBadge->clear();
        m_statusLeft->setText(tr("Model unloaded"));
        m_statusRight->clear();
        updateComposerState();
        updateWelcome();
    });
    connect(m_engine, &LlamaEngine::promptProgress, this, [this](int done, int total) {
        if (m_current && total > 64 && done < total)
            m_current->setStatus(tr("Reading prompt… %1%").arg(done * 100 / total));
    });
    connect(m_engine, &LlamaEngine::reasoningText, this, [this](const QString &t) {
        if (m_current)
            m_current->appendReasoning(t);
    });
    connect(m_engine, &LlamaEngine::contentText, this, [this](const QString &t) {
        if (m_current)
            m_current->appendContent(t);
    });
    connect(m_engine, &LlamaEngine::generationFinished, this,
            [this](const GenerationStats &stats, const ChatMessage &reply) {
                if (m_current) {
                    // Use the engine's final text (trimmed) as the canonical message.
                    m_current->setMessage(reply.content, reply.reasoning);
                    m_current->finish(stats);
                    m_current->setRegenerateVisible(true);
                }
                m_current = nullptr;
                m_statusLeft->setText(stats.stopped ? tr("Stopped") : tr("Ready"));
                m_statusRight->setText(tr("%1 tok/s · prompt %2 tok/s · ctx %3 / %4")
                                           .arg(stats.tokensPerSecond(), 0, 'f', 1)
                                           .arg(stats.promptTokensPerSecond(), 0, 'f', 0)
                                           .arg(stats.contextUsed)
                                           .arg(stats.contextSize));
                setGenerating(false);
            });
    connect(m_engine, &LlamaEngine::errorOccurred, this, [this](const QString &error) {
        if (m_current) {
            m_current->setError(error);
        } else {
            m_statusLeft->setText(error);
        }
        // Errors before generation starts don't produce generationFinished.
        if (m_generating && m_current && m_current->content().isEmpty()
            && m_current->reasoning().isEmpty()) {
            m_current = nullptr;
            setGenerating(false);
        }
    });
}

// --- Model management -------------------------------------------------------------

void MainWindow::loadSelectedModel()
{
    const LoadParams params = m_panel->loadParams();
    if (params.modelPath.isEmpty()) {
        m_statusLeft->setText(tr("Download the model first"));
        return;
    }
    if (m_generating)
        stop();
    m_loading = true;
    m_loadedPath = params.modelPath;
    m_panel->setModelState(SettingsPanel::ModelState::Loading);
    m_panel->setLoadProgress(0);
    m_statusLeft->setText(tr("Loading %1…").arg(QFileInfo(params.modelPath).fileName()));
    updateComposerState();
    QMetaObject::invokeMethod(m_engine, [engine = m_engine, params] { engine->loadModel(params); });
}

void MainWindow::unloadModel()
{
    if (m_generating)
        stop();
    QMetaObject::invokeMethod(m_engine, [engine = m_engine] { engine->unloadModel(); });
}

void MainWindow::downloadSelectedModel()
{
    const ModelInfo *m = m_panel->selectedModel();
    if (!m || m->isCustom)
        return;
    m_panel->setDownloadRunning(true);
    m_panel->setDownloadProgress(0, m->sizeMB * 1024 * 1024);
    m_downloader->start(m->downloadUrl(), m_registry.localPath(*m));
}

void MainWindow::openModelFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open GGUF model"), m_registry.modelsDir(),
                                                      tr("GGUF models (*.gguf);;All files (*)"));
    if (path.isEmpty())
        return;
    m_panel->addModelFile(path);
    loadSelectedModel();
}

void MainWindow::openModelsFolder()
{
    QDir().mkpath(m_registry.modelsDir());
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_registry.modelsDir()));
}

// --- Chat -----------------------------------------------------------------------------

void MainWindow::send()
{
    const QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty() || m_generating)
        return;

    if (!m_loaded) {
        if (m_panel->selectedModelPath().isEmpty()) {
            m_statusLeft->setText(tr("Download or open a model first"));
            return;
        }
        // Load on demand and send once ready.
        m_pendingMessage = text;
        m_input->clear();
        if (!m_loading)
            loadSelectedModel();
        return;
    }

    m_input->clear();
    m_chat->addMessage(ChatMessage::Role::User)->setMessage(text);
    m_current = m_chat->addMessage(ChatMessage::Role::Assistant);
    m_current->setStatus(tr("Thinking…"));
    m_chat->scrollToBottom();
    setGenerating(true);

    const QString system = m_panel->systemPrompt();
    const SamplingParams sampling = m_panel->samplingParams();
    QMetaObject::invokeMethod(m_engine, [engine = m_engine, system, text, sampling] {
        engine->setSystemPrompt(system);
        engine->sendMessage(text, sampling);
    });
}

void MainWindow::sendText(const QString &text)
{
    if (m_loading) {
        m_pendingMessage = text.trimmed();
        return;
    }
    m_input->setPlainText(text);
    send();
}

void MainWindow::stop()
{
    if (m_generating || m_loading)
        m_engine->requestStop(); // thread-safe
}

void MainWindow::regenerate()
{
    if (m_generating || !m_loaded)
        return;
    MessageWidget *last = m_chat->last();
    if (!last || last->role() != ChatMessage::Role::Assistant)
        return;
    m_chat->removeLast();
    m_current = m_chat->addMessage(ChatMessage::Role::Assistant);
    m_current->setStatus(tr("Thinking…"));
    setGenerating(true);

    const QString system = m_panel->systemPrompt();
    const SamplingParams sampling = m_panel->samplingParams();
    QMetaObject::invokeMethod(m_engine, [engine = m_engine, system, sampling] {
        engine->setSystemPrompt(system);
        engine->regenerate(sampling);
    });
}

void MainWindow::newChat()
{
    stop();
    m_chat->clear();
    m_current = nullptr;
    QMetaObject::invokeMethod(m_engine, [engine = m_engine] { engine->resetConversation(); });
    m_statusLeft->setText(m_loaded ? tr("Ready") : QString());
    m_input->setFocus();
}

void MainWindow::exportChat()
{
    if (m_chat->messages().isEmpty())
        return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Export conversation"),
                                                      QDir::homePath() + QStringLiteral("/chat.md"),
                                                      tr("Markdown (*.md)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export failed"), f.errorString());
        return;
    }
    QTextStream ts(&f);
    const QString system = m_panel->systemPrompt();
    if (!system.isEmpty())
        ts << "**System:** " << system << "\n\n";
    for (const ChatMessage &m : m_chat->history()) {
        ts << "### " << (m.role == ChatMessage::Role::User ? "You" : "Assistant") << "\n\n";
        if (!m.reasoning.isEmpty())
            ts << "<details><summary>Thinking</summary>\n\n" << m.reasoning << "\n\n</details>\n\n";
        ts << m.content << "\n\n";
    }
    m_statusLeft->setText(tr("Exported to %1").arg(path));
}

void MainWindow::toggleSidebar()
{
    m_panel->setVisible(!m_panel->isVisible());
}

// --- State -------------------------------------------------------------------------------

void MainWindow::setGenerating(bool generating)
{
    m_generating = generating;
    m_sendButton->setText(generating ? QStringLiteral("■") : QStringLiteral("↑"));
    m_sendButton->setObjectName(generating ? QStringLiteral("StopButton") : QStringLiteral("SendButton"));
    m_sendButton->setToolTip(generating ? tr("Stop (Esc)") : tr("Send (Enter)"));
    m_sendButton->style()->unpolish(m_sendButton);
    m_sendButton->style()->polish(m_sendButton);
    if (generating)
        m_statusLeft->setText(tr("Generating…"));
    updateComposerState();
}

void MainWindow::updateComposerState()
{
    const bool canType = m_loaded || !m_panel->selectedModelPath().isEmpty();
    m_input->setEnabled(canType);
    m_sendButton->setEnabled(m_generating || (canType && !m_input->toPlainText().trimmed().isEmpty()));
}

void MainWindow::updateWelcome()
{
    const ModelInfo *m = m_panel->selectedModel();
    const QString name = m_panel->selectedModelName();
    if (m_loaded) {
        m_chat->setWelcome(tr("How can I help?"), tr("Chatting with %1, running locally.").arg(m_modelBadge->text()));
    } else if (m_loading) {
        m_chat->setWelcome(tr("Loading %1…").arg(name), QString());
    } else if (!m_panel->selectedModelPath().isEmpty()) {
        m_chat->setWelcome(tr("QTllama"), tr("%1 is ready to load. Type a message to start.").arg(name));
    } else if (m && !m->isCustom) {
        m_chat->setWelcome(tr("Welcome to QTllama"),
                           tr("Download %1 (%2 MB) from the sidebar, or open any .gguf model.")
                               .arg(m->name)
                               .arg(m->sizeMB));
    } else {
        m_chat->setWelcome(tr("Welcome to QTllama"), tr("Open a .gguf model from the sidebar to begin."));
    }
    updateComposerState();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_panel->saveSettings();
    QSettings s;
    s.setValue("ui/geometry", saveGeometry());
    s.setValue("ui/splitter", m_splitter->saveState());
    if (m_downloader->isRunning())
        m_downloader->cancel();
    QMainWindow::closeEvent(event);
}
