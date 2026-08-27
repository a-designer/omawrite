#include <QFont>
#include <QFontDatabase>
#include <QApplication>
#include <QEvent>
#include <QFileOpenEvent>
#include <QProcess>
#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QUrl>
#include <QWindow>
#include <QFile>

#include "backend.h"
#include "systemtheme.h"

namespace {
// macOS delivers Finder opens (double-click, drag to the Dock, "Open With")
// as events rather than arguments. Take the file into this window if it is
// still untouched, otherwise give it a window of its own.
class FileOpenFilter : public QObject {
public:
    explicit FileOpenFilter(Backend &backend, QObject *parent)
        : QObject(parent), m_backend(backend) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() != QEvent::FileOpen)
            return QObject::eventFilter(watched, event);

        const QUrl url = static_cast<QFileOpenEvent *>(event)->url();
        if (!url.isLocalFile())
            return true;

        const bool windowInUse = m_backend.modified()
            || (m_backend.fileUrl().isValid() && !m_backend.fileUrl().isEmpty());
        if (windowInUse)
            QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                    {url.toLocalFile()});
        else
            m_backend.open(url);
        return true;
    }

private:
    Backend &m_backend;
};
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omawrite"));
    app.setDesktopFileName(QStringLiteral("omawrite"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("omawrite")));

    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-Regular.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-Italic.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-Bold.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-BoldItalic.ttf"));
    // Qt's Markdown renderer asks for the generic "monospace" family for code
    // spans and blocks; keep those in the writing font in the preview.
    QFont::insertSubstitution(QStringLiteral("monospace"), QStringLiteral("iA Writer Mono S"));
    app.setOrganizationName(QStringLiteral("Omacom"));
    app.setOrganizationDomain(QStringLiteral("omacom.io"));

    QQuickStyle::setStyle(QStringLiteral("Material"));

    Backend backend(&app);
    SystemTheme systemTheme(&app);
    backend.setDarkMode(systemTheme.darkMode());
    QObject::connect(&systemTheme, &SystemTheme::darkModeChanged, &backend,
                     &Backend::setDarkMode);

    // Rendered Markdown links take their colour from the palette's Link role,
    // which defaults to a pure blue that is unreadable on the dark page.
    const auto applyLinkColor = [&app, &backend]() {
        QPalette palette = app.palette();
        palette.setColor(QPalette::Link, QColor(backend.themeAccent()));
        app.setPalette(palette);
    };
    applyLinkColor();
    QObject::connect(&backend, &Backend::themeColorsChanged, &backend, applyLinkColor);

    // Carry the desktop's text scale into the default font, so the chrome that
    // inherits it (dialog titles, buttons) grows along with the writing area.
    const QFont interfaceFont(QStringLiteral("iA Writer Mono S"));
    const qreal basePointSize = interfaceFont.pointSizeF() > 0
        ? interfaceFont.pointSizeF()
        : app.font().pointSizeF();
    const auto applyInterfaceFont = [&app, interfaceFont, basePointSize](qreal textScale) {
        QFont scaled = interfaceFont;
        scaled.setPointSizeF(basePointSize * textScale);
        app.setFont(scaled);
    };
    backend.setTextScale(systemTheme.textScale());
    applyInterfaceFont(backend.textScale());
    QObject::connect(&systemTheme, &SystemTheme::textScaleChanged, &backend,
                     &Backend::setTextScale);
    // Backend folds the user's zoom (Ctrl+=/-/0) into the effective scale.
    QObject::connect(&backend, &Backend::textScaleChanged, &backend,
                     [&backend, applyInterfaceFont]() {
        applyInterfaceFont(backend.textScale());
    });

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings)
            qWarning().noquote() << warning.toString();
    });
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);

    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Could not load the Omawrite interface; resource available:"
                    << QFile::exists(QStringLiteral(":/Main.qml"));
        return -1;
    }

    backend.setParentWindow(qobject_cast<QWindow *>(engine.rootObjects().constFirst()));
    app.installEventFilter(new FileOpenFilter(backend, &app));

    const QStringList args = app.arguments();
    if (args.size() > 1 && !backend.modified())
        backend.open(QUrl::fromLocalFile(args.at(1)));

    return app.exec();
}
