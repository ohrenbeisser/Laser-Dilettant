// Einstiegspunkt von Laser-Dilettant.
#include <QApplication>
#include <QIcon>
#include <QLocale>
#include <QLibraryInfo>
#include <QTranslator>

#include "appsettings.h"
#include "mainwindow.h"
#include "theme.h"

int main(int argc, char* argv[])
{
    // Fusion-Stil auf allen Systemen: Nur so lässt sich das Hell/Dunkel-Design überall gleich
    // umsetzen (eigene Palette, siehe theme.cpp).
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication app(argc, argv);

    // Diese Namen bestimmen, wo QSettings die Einstellungen ablegt
    // (Linux: ~/.config/Laser-Dilettant/, Windows: Registry).
    QCoreApplication::setOrganizationName(QStringLiteral("Laser-Dilettant"));
    QCoreApplication::setApplicationName(QStringLiteral("Laser-Dilettant"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LD_VERSION));

    // Programm-Icon für alle Fenster: mehrere Größen, Qt wählt die passende (Ressourcen :/icons/<Größe>.png).
    QIcon appIcon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256, 512}) {
        appIcon.addFile(QStringLiteral(":/icons/%1.png").arg(size), QSize(size, size));
    }
    QApplication::setWindowIcon(appIcon);

    // Das Design gilt schon vor dem Bau des Fensters, damit alle Widgets (auch mit Stylesheet) es erhalten.
    ld::app::applyTheme(ld::app::AppSettings::instance().design());

    // Übersetzung: Die Quelltexte sind Deutsch. Für andere Sprachen (zuerst Englisch) wird
    // eine .qm-Datei aus den eingebetteten Ressourcen geladen, falls sie existiert.
    // Bei deutschem System gibt es keine laser-dilettant_de.qm -> es bleibt beim Quelltext.
    // Qt-eigene Texte (Knöpfe wie "OK", "Abbrechen", "Ja" in Standarddialogen) in der Systemsprache.
    // Die Sprache kommt aus den Einstellungen ("system" = Sprache des Systems). Zahlen und Datum folgen ihr.
    const QString language = ld::app::AppSettings::instance().language();
    const QLocale uiLocale = language == QLatin1String("system") ? QLocale() : QLocale(language);
    if (language != QLatin1String("system")) QLocale::setDefault(uiLocale);
    QTranslator qtTranslator;
    if (qtTranslator.load(uiLocale, QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(&qtTranslator);
    }

    QTranslator translator;
    if (translator.load(uiLocale, QStringLiteral("laser-dilettant"), QStringLiteral("_"),
                        QStringLiteral(":/i18n"))) {
        app.installTranslator(&translator);
    }

    ld::app::MainWindow window;
    window.show();
    window.handleStartup(app.arguments().mid(1));   // Sicherung anbieten, Projekt aus dem Aufruf öffnen

    return app.exec();
}
