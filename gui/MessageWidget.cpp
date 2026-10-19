#include "MessageWidget.h"

#include "Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

using qtllama::ChatMessage;

namespace {

QLabel *makeTextLabel(QWidget *parent, Qt::TextFormat format)
{
    auto *label = new QLabel(parent);
    label->setTextFormat(format);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    label->setOpenExternalLinks(true);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    return label;
}

// Markdown -> HTML with tinted code blocks and toned-down headings; QLabel's
// built-in Markdown rendering leaves code blocks unstyled.
QString renderMarkdown(const QString &markdown, const QFont &baseFont)
{
    QTextDocument doc;
    doc.setDefaultFont(baseFont);
    doc.setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);

    const Theme::Colors &c = Theme::colors();
    const qreal base = baseFont.pointSizeF() > 0 ? baseFont.pointSizeF() : 12.0;
    QTextCursor cursor(&doc);
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat bf = block.blockFormat();
        cursor.setPosition(block.position());
        cursor.setPosition(block.position() + qMax(0, block.length() - 1), QTextCursor::KeepAnchor);

        if (bf.hasProperty(QTextFormat::BlockCodeFence) || block.charFormat().fontFixedPitch()) {
            bf.setBackground(c.surfaceAlt);
            bf.setLeftMargin(8);
            bf.setRightMargin(8);
            cursor.setBlockFormat(bf);
        } else if (bf.headingLevel() > 0) {
            QTextCharFormat cf;
            cf.setFontPointSize(base * (bf.headingLevel() <= 2 ? 1.3 : 1.12));
            cf.setFontWeight(QFont::DemiBold);
            cursor.mergeCharFormat(cf);
            bf.setTopMargin(10);
            bf.setBottomMargin(4);
            cursor.setBlockFormat(bf);
        }
    }
    return doc.toHtml();
}

} // namespace

MessageWidget::MessageWidget(ChatMessage::Role role, QWidget *parent)
    : QWidget(parent)
    , m_role(role)
{
    const bool isUser = role == ChatMessage::Role::User;

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 6, 0, 6);

    m_bubble = new QFrame(this);
    m_bubble->setObjectName(isUser ? QStringLiteral("UserBubble") : QStringLiteral("AssistantBubble"));
    auto *col = new QVBoxLayout(m_bubble);
    col->setContentsMargins(isUser ? 14 : 4, isUser ? 10 : 4, isUser ? 14 : 4, isUser ? 10 : 4);
    col->setSpacing(6);

    if (!isUser) {
        auto *header = new QHBoxLayout;
        header->setSpacing(4);
        auto *roleLabel = new QLabel(tr("Assistant"), m_bubble);
        roleLabel->setObjectName(QStringLiteral("RoleLabel"));
        header->addWidget(roleLabel);
        header->addStretch(1);

        m_copyButton = new QToolButton(m_bubble);
        m_copyButton->setObjectName(QStringLiteral("BubbleAction"));
        m_copyButton->setText(tr("Copy"));
        m_copyButton->setToolTip(tr("Copy the answer as Markdown"));
        m_copyButton->setVisible(false);
        connect(m_copyButton, &QToolButton::clicked, this, [this] {
            QApplication::clipboard()->setText(m_content);
            m_copyButton->setText(tr("Copied"));
            QTimer::singleShot(1200, m_copyButton, [this] { m_copyButton->setText(tr("Copy")); });
        });
        header->addWidget(m_copyButton);

        m_regenButton = new QToolButton(m_bubble);
        m_regenButton->setObjectName(QStringLiteral("BubbleAction"));
        m_regenButton->setText(tr("Regenerate"));
        m_regenButton->setVisible(false);
        connect(m_regenButton, &QToolButton::clicked, this, &MessageWidget::regenerateRequested);
        header->addWidget(m_regenButton);
        col->addLayout(header);

        m_thinkToggle = new QToolButton(m_bubble);
        m_thinkToggle->setObjectName(QStringLiteral("ThinkToggle"));
        m_thinkToggle->setCheckable(true);
        m_thinkToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_thinkToggle->setVisible(false);
        connect(m_thinkToggle, &QToolButton::clicked, this, [this](bool on) {
            m_userToggledThinking = true;
            setThinkingExpanded(on);
        });
        col->addWidget(m_thinkToggle, 0, Qt::AlignLeft);

        m_thinkLabel = makeTextLabel(m_bubble, Qt::PlainText);
        m_thinkLabel->setObjectName(QStringLiteral("ThinkingText"));
        m_thinkLabel->setVisible(false);
        col->addWidget(m_thinkLabel);

        m_statusLabel = new QLabel(m_bubble);
        m_statusLabel->setObjectName(QStringLiteral("Muted"));
        m_statusLabel->setVisible(false);
        col->addWidget(m_statusLabel);
    }

    m_contentLabel = makeTextLabel(m_bubble, isUser ? Qt::PlainText : Qt::RichText);
    col->addWidget(m_contentLabel);

    if (!isUser) {
        m_footer = new QLabel(m_bubble);
        m_footer->setObjectName(QStringLiteral("Footer"));
        QFont small = m_footer->font();
        small.setPointSizeF(small.pointSizeF() * 0.85);
        m_footer->setFont(small);
        m_footer->setVisible(false);
        col->addWidget(m_footer);
    }

    if (isUser) {
        row->addStretch(1);
        row->addWidget(m_bubble);
    } else {
        row->addWidget(m_bubble, 1);
    }

    // Markdown re-rendering is the expensive part of streaming; batch it.
    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(60);
    connect(&m_renderTimer, &QTimer::timeout, this, &MessageWidget::render);
}

void MessageWidget::setMessage(const QString &content, const QString &reasoning)
{
    m_content = content;
    m_reasoning = reasoning;
    m_finished = true;
    render();
    if (m_copyButton)
        m_copyButton->setVisible(!m_content.isEmpty());
}

void MessageWidget::appendReasoning(const QString &text)
{
    m_reasoning += text;
    if (m_statusLabel)
        m_statusLabel->setVisible(false);
    if (!m_userToggledThinking && m_content.isEmpty())
        setThinkingExpanded(true); // show live reasoning while it streams
    scheduleRender();
}

void MessageWidget::appendContent(const QString &text)
{
    if (m_content.isEmpty() && !m_userToggledThinking)
        setThinkingExpanded(false); // collapse once the answer starts
    m_content += text;
    if (m_statusLabel)
        m_statusLabel->setVisible(false);
    scheduleRender();
}

void MessageWidget::setStatus(const QString &text)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setVisible(!text.isEmpty());
}

void MessageWidget::setError(const QString &text)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setObjectName(QStringLiteral("ErrorText"));
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
    setStatus(text);
}

void MessageWidget::finish(const qtllama::GenerationStats &stats)
{
    m_finished = true;
    m_renderTimer.stop();
    render();
    if (m_statusLabel && m_statusLabel->objectName() != QLatin1String("ErrorText"))
        m_statusLabel->setVisible(false);
    if (m_copyButton)
        m_copyButton->setVisible(!m_content.isEmpty());
    if (m_footer) {
        QString text = tr("%1 tokens · %2 tok/s")
                           .arg(stats.generatedTokens)
                           .arg(stats.tokensPerSecond(), 0, 'f', 1);
        if (stats.stopped)
            text += tr(" · stopped");
        m_footer->setText(text);
        m_footer->setVisible(stats.generatedTokens > 0 || stats.stopped);
    }
}

void MessageWidget::setMaxBubbleWidth(int width)
{
    if (m_role == ChatMessage::Role::User)
        m_bubble->setMaximumWidth(width);
}

void MessageWidget::setRegenerateVisible(bool visible)
{
    if (m_regenButton)
        m_regenButton->setVisible(visible);
}

void MessageWidget::scheduleRender()
{
    if (!m_renderTimer.isActive())
        m_renderTimer.start();
}

void MessageWidget::setThinkingExpanded(bool expanded)
{
    if (!m_thinkToggle)
        return;
    m_thinkToggle->setChecked(expanded);
    m_thinkLabel->setVisible(expanded && !m_reasoning.isEmpty());
    render();
}

void MessageWidget::render()
{
    if (m_thinkToggle) {
        const bool hasReasoning = !m_reasoning.isEmpty();
        m_thinkToggle->setVisible(hasReasoning);
        if (hasReasoning) {
            const bool thinkingNow = !m_finished && m_content.isEmpty();
            const QString arrow = m_thinkToggle->isChecked() ? QStringLiteral("▾ ") : QStringLiteral("▸ ");
            m_thinkToggle->setText(arrow + (thinkingNow ? tr("Thinking…") : tr("Thought process")));
            if (m_thinkLabel->isVisible())
                m_thinkLabel->setText(m_reasoning);
        }
    }
    if (m_role == ChatMessage::Role::User)
        m_contentLabel->setText(m_content);
    else
        m_contentLabel->setText(renderMarkdown(m_content, m_contentLabel->font()));
    m_contentLabel->setVisible(!m_content.isEmpty());
    Q_EMIT contentChanged();
}
