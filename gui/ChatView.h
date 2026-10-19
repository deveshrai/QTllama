#pragma once

#include "Types.h"

#include <QScrollArea>

class MessageWidget;
class QVBoxLayout;
class QLabel;

// Scrollable list of messages that keeps following the bottom while the
// user hasn't scrolled up.
class ChatView : public QScrollArea
{
    Q_OBJECT

public:
    explicit ChatView(QWidget *parent = nullptr);

    MessageWidget *addMessage(qtllama::ChatMessage::Role role);
    void removeLast();
    void clear();

    QList<MessageWidget *> messages() const { return m_messages; }
    MessageWidget *last() const { return m_messages.isEmpty() ? nullptr : m_messages.last(); }
    qtllama::ChatHistory history() const;

    void setWelcome(const QString &title, const QString &text);
    void scrollToBottom();

Q_SIGNALS:
    void regenerateRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateWidths();
    void updateWelcome();

    QWidget *m_container = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QWidget *m_welcome = nullptr;
    QLabel *m_welcomeTitle = nullptr;
    QLabel *m_welcomeText = nullptr;
    QList<MessageWidget *> m_messages;
    bool m_follow = true;
};
