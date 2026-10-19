// QTllama — desktop/tablet chat client for local LLMs.

#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QSettings>
#include <QTimer>

#include "LlamaEngine.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("QTllama"));
    QApplication::setApplicationName(QStringLiteral("QTllama"));
    QApplication::setApplicationDisplayName(QStringLiteral("QTllama"));
    QApplication::setApplicationVersion(QStringLiteral(QTLLAMA_VERSION));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption verbose("verbose", "Print llama.cpp log output.");
    parser.addOption(verbose);
    parser.addPositionalArgument("prompt", "Optional message to send on startup.", "[prompt]");
    // Renders the window to an image after a delay and quits (docs/CI).
    const QCommandLineOption screenshot("screenshot", "Save a screenshot of the window after --screenshot-delay ms and exit.", "file");
    const QCommandLineOption screenshotDelay("screenshot-delay", "Delay before --screenshot in ms.", "ms", "3000");
    parser.addOption(screenshot);
    parser.addOption(screenshotDelay);
    parser.process(app);
    qtllama::LlamaEngine::setVerbose(parser.isSet(verbose));

    QSettings settings;
    Theme::apply(app, Theme::modeFromString(settings.value("ui/theme", "system").toString()));
    if (settings.contains("ui/fontSize")) {
        QFont f = QApplication::font();
        f.setPointSize(settings.value("ui/fontSize").toInt());
        QApplication::setFont(f);
    }

    MainWindow window;
    window.show();
    if (!parser.positionalArguments().isEmpty()) {
        const QString prompt = parser.positionalArguments().join(QLatin1Char(' '));
        QTimer::singleShot(0, &window, [&window, prompt] { window.sendText(prompt); });
    }
    if (parser.isSet(screenshot)) {
        const QString file = parser.value(screenshot);
        QTimer::singleShot(parser.value(screenshotDelay).toInt(), &window, [&window, file] {
            window.grab().save(file);
            window.close();
        });
    }
    return app.exec();
}
