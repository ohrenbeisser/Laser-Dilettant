#include "app/AppSettings.h"
#include "app/MainWindow.h"
#include "core/Resources.h"
#include "device/GrblCodes.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Laser-Dilettant"));
    QApplication::setApplicationName(QStringLiteral("Laser-Dilettant"));
    QApplication::setApplicationVersion(QStringLiteral(LD_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("laser-dilettant"));
    ldInitResources();

    AppSettings settings;

    // Language: setting or system language; German or English.
    QString lang = settings.language();
    if (lang.isEmpty())
        lang = QLocale::system().language() == QLocale::German ? QStringLiteral("de") : QStringLiteral("en");
    const QLocale locale(lang == QLatin1String("de") ? QLocale::German : QLocale::English,
                         lang == QLatin1String("de") ? QLocale::Germany : QLocale::UnitedStates);
    QLocale::setDefault(locale);

    QTranslator qtTranslator;
    if (qtTranslator.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);
    QTranslator appTranslator;
    if (appTranslator.load(QStringLiteral(":/i18n/laser-dilettant_%1.qm").arg(lang)))
        QApplication::installTranslator(&appTranslator);
    GrblCodes::setLanguage(lang);

    MainWindow window(&settings);
    window.show();
    return QApplication::exec();
}
