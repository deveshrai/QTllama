#pragma once

#include <QColor>
#include <QString>

class QApplication;

// Fusion-based light/dark theme with a shared stylesheet.
namespace Theme {

enum class Mode { System, Light, Dark };

struct Colors
{
    QColor window, surface, surfaceAlt, border, text, muted, accent, accentText,
        userBubble, userText, danger;
};

void apply(QApplication &app, Mode mode);
bool isDark();
const Colors &colors();

Mode modeFromString(const QString &s);
QString modeToString(Mode mode);

} // namespace Theme
