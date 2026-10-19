#pragma once

#include "ModelRegistry.h"
#include "Types.h"

#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPointer>
#include <QThread>

class ChatView;
class MessageWidget;
class SettingsPanel;
class QLabel;
class QPushButton;
class QSplitter;

namespace qtllama {
class LlamaEngine;
class ModelDownloader;
}

// Multi-line input: Enter sends, Shift+Enter inserts a newline. Grows with
// its content up to a few lines.
class Composer : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit Composer(QWidget *parent = nullptr);

Q_SIGNALS:
    void submitted();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void updateHeight();
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Sends a message as if typed (loads the model first if needed).
    void sendText(const QString &text);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void connectEngine();

    void loadSelectedModel();
    void unloadModel();
    void downloadSelectedModel();
    void openModelFile();
    void openModelsFolder();

    void send();
    void stop();
    void regenerate();
    void newChat();
    void exportChat();
    void toggleSidebar();

    void setGenerating(bool generating);
    void updateComposerState();
    void updateWelcome();

    qtllama::ModelRegistry m_registry;
    qtllama::LlamaEngine *m_engine = nullptr;
    QThread m_engineThread;
    qtllama::ModelDownloader *m_downloader = nullptr;

    QSplitter *m_splitter = nullptr;
    SettingsPanel *m_panel = nullptr;
    ChatView *m_chat = nullptr;
    Composer *m_input = nullptr;
    QPushButton *m_sendButton = nullptr;
    QLabel *m_modelBadge = nullptr;
    QLabel *m_statusLeft = nullptr;
    QLabel *m_statusRight = nullptr;

    QPointer<MessageWidget> m_current; // assistant message being generated
    bool m_generating = false;
    bool m_loading = false;
    bool m_loaded = false;
    QString m_loadedPath;
    QString m_pendingMessage; // sent once the model finishes loading
};
