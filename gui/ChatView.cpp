#include "ChatView.h"

#include "MessageWidget.h"

#include <QLabel>
#include <QResizeEvent>
#include <QScrollBar>
#include <QVBoxLayout>

using qtllama::ChatMessage;

namespace {
constexpr int kMaxColumnWidth = 860; // readable line length on wide screens
}

ChatView::ChatView(QWidget *parent)
    : QScrollArea(parent)
{
    setObjectName(QStringLiteral("ChatScroll"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_container = new QWidget(this);
    m_container->setObjectName(QStringLiteral("ChatContainer"));
    m_layout = new QVBoxLayout(m_container);
    m_layout->setSpacing(4);

    m_welcome = new QWidget(m_container);
    auto *wl = new QVBoxLayout(m_welcome);
    wl->addStretch(1);
    m_welcomeTitle = new QLabel(m_welcome);
    m_welcomeTitle->setObjectName(QStringLiteral("WelcomeTitle"));
    m_welcomeTitle->setAlignment(Qt::AlignCenter);
    m_welcomeText = new QLabel(m_welcome);
    m_welcomeText->setObjectName(QStringLiteral("WelcomeText"));
    m_welcomeText->setAlignment(Qt::AlignCenter);
    m_welcomeText->setWordWrap(true);
    wl->addWidget(m_welcomeTitle);
    wl->addWidget(m_welcomeText);
    wl->addStretch(2);
    m_layout->addWidget(m_welcome, 1);
    m_layout->addStretch(0);

    setWidget(m_container);

    // Follow new output only while the view is scrolled to the bottom.
    QScrollBar *bar = verticalScrollBar();
    connect(bar, &QScrollBar::valueChanged, this, [this, bar](int v) {
        m_follow = v >= bar->maximum() - 40;
    });
    connect(bar, &QScrollBar::rangeChanged, this, [this, bar](int, int max) {
        if (m_follow)
            bar->setValue(max);
    });
}

MessageWidget *ChatView::addMessage(ChatMessage::Role role)
{
    auto *msg = new MessageWidget(role, m_container);
    connect(msg, &MessageWidget::regenerateRequested, this, &ChatView::regenerateRequested);
    // Insert before the trailing stretch.
    m_layout->insertWidget(m_layout->count() - 1, msg);
    for (MessageWidget *m : std::as_const(m_messages))
        m->setRegenerateVisible(false);
    m_messages.append(msg);
    updateWelcome();
    updateWidths();
    m_follow = true;
    return msg;
}

void ChatView::removeLast()
{
    if (m_messages.isEmpty())
        return;
    delete m_messages.takeLast();
    updateWelcome();
}

void ChatView::clear()
{
    qDeleteAll(m_messages);
    m_messages.clear();
    updateWelcome();
}

qtllama::ChatHistory ChatView::history() const
{
    qtllama::ChatHistory h;
    for (const MessageWidget *m : m_messages)
        h.append(m->message());
    return h;
}

void ChatView::setWelcome(const QString &title, const QString &text)
{
    m_welcomeTitle->setText(title);
    m_welcomeText->setText(text);
}

void ChatView::scrollToBottom()
{
    m_follow = true;
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void ChatView::resizeEvent(QResizeEvent *event)
{
    QScrollArea::resizeEvent(event);
    updateWidths();
}

void ChatView::updateWelcome()
{
    m_welcome->setVisible(m_messages.isEmpty());
    m_layout->setStretch(m_layout->count() - 1, m_messages.isEmpty() ? 0 : 1);
}

void ChatView::updateWidths()
{
    // Centre a fixed-width column on wide windows; fill on narrow ones (tablets).
    const int w = viewport()->width();
    const int side = qMax(12, (w - kMaxColumnWidth) / 2);
    m_layout->setContentsMargins(side, 12, side, 12);
    const int column = w - 2 * side;
    for (MessageWidget *m : std::as_const(m_messages))
        m->setMaxBubbleWidth(int(column * 0.85));
}
