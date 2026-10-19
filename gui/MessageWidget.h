#pragma once

#include "Types.h"

#include <QFrame>
#include <QTimer>

class QLabel;
class QToolButton;

// One chat message. Assistant messages stream in and show their reasoning
// in a collapsible section above the answer.
class MessageWidget : public QWidget
{
    Q_OBJECT

public:
    MessageWidget(qtllama::ChatMessage::Role role, QWidget *parent = nullptr);

    qtllama::ChatMessage::Role role() const { return m_role; }
    QString content() const { return m_content; }
    QString reasoning() const { return m_reasoning; }
    qtllama::ChatMessage message() const { return {m_role, m_content, m_reasoning}; }

    void setMessage(const QString &content, const QString &reasoning = {});
    void appendReasoning(const QString &text);
    void appendContent(const QString &text);
    void setStatus(const QString &text); // e.g. "Reading prompt… 40%"
    void setError(const QString &text);
    void finish(const qtllama::GenerationStats &stats);

    void setMaxBubbleWidth(int width);
    void setRegenerateVisible(bool visible);

Q_SIGNALS:
    void regenerateRequested();
    void contentChanged();

private:
    void scheduleRender();
    void render();
    void setThinkingExpanded(bool expanded);

    qtllama::ChatMessage::Role m_role;
    QString m_content;
    QString m_reasoning;
    bool m_finished = false;
    bool m_userToggledThinking = false;

    QFrame *m_bubble = nullptr;
    QToolButton *m_thinkToggle = nullptr;
    QLabel *m_thinkLabel = nullptr;
    QLabel *m_contentLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_footer = nullptr;
    QToolButton *m_copyButton = nullptr;
    QToolButton *m_regenButton = nullptr;
    QTimer m_renderTimer;
};
