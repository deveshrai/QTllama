#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>

namespace Theme {

namespace {

const Colors kDark{
    QColor("#1b1c1f"), QColor("#232428"), QColor("#2c2e33"), QColor("#3a3c42"),
    QColor("#e7e8ea"), QColor("#9a9ca3"), QColor("#5b8cff"), QColor("#ffffff"),
    QColor("#2e4370"), QColor("#eef2ff"), QColor("#ef5b5b"),
};

const Colors kLight{
    QColor("#f6f7f9"), QColor("#ffffff"), QColor("#eef0f3"), QColor("#d9dce1"),
    QColor("#1f2328"), QColor("#6b7280"), QColor("#2563eb"), QColor("#ffffff"),
    QColor("#dbe7ff"), QColor("#10224a"), QColor("#d93636"),
};

bool g_dark = true;

bool systemPrefersDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    const QPalette p = QGuiApplication::palette();
    return p.color(QPalette::Window).lightness() < 128;
#endif
}

QString styleSheet(const Colors &c)
{
    QString css = QStringLiteral(R"(
QMainWindow, QWidget#Central { background: @window; }
QWidget#Sidebar { background: @surface; border-right: 1px solid @border; }
QScrollArea#SidebarScroll, QScrollArea#SidebarScroll > QWidget > QWidget { background: @surface; border: none; }
QLabel#SectionTitle { color: @muted; font-weight: 600; text-transform: uppercase; letter-spacing: 1px; padding-top: 10px; }
QLabel#Muted, QLabel#Footer { color: @muted; }
QLabel#WelcomeTitle { font-size: 22pt; font-weight: 600; }
QLabel#WelcomeText { color: @muted; }

QToolBar#TopBar { background: @window; border: none; border-bottom: 1px solid @border; padding: 4px 8px; spacing: 6px; }
QToolBar#TopBar QLabel#ModelBadge { color: @muted; padding: 0 8px; }
QToolButton { border: none; border-radius: 6px; padding: 5px 8px; color: @text; }
QToolButton:hover { background: @surfaceAlt; }
QToolButton:checked { background: @surfaceAlt; }

QScrollArea#ChatScroll, QWidget#ChatContainer { background: @window; border: none; }

QFrame#UserBubble { background: @userBubble; border-radius: 14px; }
QFrame#UserBubble QLabel { color: @userText; }
QFrame#AssistantBubble { background: transparent; }
QLabel#RoleLabel { color: @muted; font-weight: 600; }
QToolButton#ThinkToggle { color: @muted; padding: 3px 6px; text-align: left; }
QLabel#ThinkingText { color: @muted; border-left: 2px solid @border; padding-left: 10px; }
QToolButton#BubbleAction { color: @muted; padding: 2px 6px; }
QToolButton#BubbleAction:hover { color: @text; }
QLabel#ErrorText { color: @danger; }

QFrame#Composer { background: @surface; border: 1px solid @border; border-radius: 16px; }
QFrame#Composer QPlainTextEdit { background: transparent; border: none; color: @text; }

QPushButton { background: @surfaceAlt; color: @text; border: 1px solid @border; border-radius: 8px; padding: 6px 12px; }
QPushButton:hover { border-color: @accent; }
QPushButton:disabled { color: @muted; }
QPushButton#Primary { background: @accent; color: @accentText; border: none; font-weight: 600; }
QPushButton#Primary:disabled { background: @surfaceAlt; color: @muted; }
QPushButton#Danger { background: @danger; color: #ffffff; border: none; font-weight: 600; }
QPushButton#SendButton { background: @accent; color: @accentText; border: none; border-radius: 16px; min-width: 32px; min-height: 32px; max-width: 32px; max-height: 32px; padding: 0; font-weight: 700; }
QPushButton#SendButton:disabled { background: @surfaceAlt; color: @muted; }
QPushButton#StopButton { background: @danger; color: #ffffff; border: none; border-radius: 16px; min-width: 32px; min-height: 32px; max-width: 32px; max-height: 32px; padding: 0; }

QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit, QPlainTextEdit#SystemPrompt {
    background: @surfaceAlt; color: @text; border: 1px solid @border; border-radius: 6px; padding: 4px 6px;
}
QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QLineEdit:focus, QPlainTextEdit#SystemPrompt:focus { border-color: @accent; }
QComboBox QAbstractItemView { background: @surface; color: @text; selection-background-color: @accent; border: 1px solid @border; }

QProgressBar { background: @surfaceAlt; border: none; border-radius: 4px; height: 8px; text-align: center; color: transparent; }
QProgressBar::chunk { background: @accent; border-radius: 4px; }

QSlider::groove:horizontal { height: 4px; background: @border; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal { background: @accent; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; }

QCheckBox { color: @text; spacing: 8px; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: @border; border-radius: 5px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: @muted; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }

QStatusBar { background: @window; color: @muted; border-top: 1px solid @border; }
QStatusBar QLabel { color: @muted; padding: 0 8px; }
QSplitter::handle { background: @border; }
QToolTip { background: @surface; color: @text; border: 1px solid @border; padding: 4px; }
)");
    const std::pair<const char *, QColor> vars[] = {
        {"@window", c.window},       {"@surfaceAlt", c.surfaceAlt}, {"@surface", c.surface},
        {"@border", c.border},       {"@text", c.text},             {"@muted", c.muted},
        {"@accentText", c.accentText}, {"@accent", c.accent},      {"@userBubble", c.userBubble},
        {"@userText", c.userText},   {"@danger", c.danger},
    };
    for (const auto &v : vars)
        css.replace(QLatin1String(v.first), v.second.name());
    return css;
}

QPalette palette(const Colors &c)
{
    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.surface);
    p.setColor(QPalette::AlternateBase, c.surfaceAlt);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.surfaceAlt);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::Highlight, c.accent);
    p.setColor(QPalette::HighlightedText, c.accentText);
    p.setColor(QPalette::ToolTipBase, c.surface);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::PlaceholderText, c.muted);
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::Disabled, QPalette::Text, c.muted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c.muted);
    p.setColor(QPalette::Disabled, QPalette::WindowText, c.muted);
    return p;
}

} // namespace

void apply(QApplication &app, Mode mode)
{
    g_dark = mode == Mode::Dark || (mode == Mode::System && systemPrefersDark());
    const Colors &c = colors();
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setPalette(palette(c));
    app.setStyleSheet(styleSheet(c));
}

bool isDark()
{
    return g_dark;
}

const Colors &colors()
{
    return g_dark ? kDark : kLight;
}

Mode modeFromString(const QString &s)
{
    if (s == QLatin1String("light")) return Mode::Light;
    if (s == QLatin1String("dark")) return Mode::Dark;
    return Mode::System;
}

QString modeToString(Mode mode)
{
    switch (mode) {
    case Mode::Light: return QStringLiteral("light");
    case Mode::Dark: return QStringLiteral("dark");
    case Mode::System: break;
    }
    return QStringLiteral("system");
}

} // namespace Theme
