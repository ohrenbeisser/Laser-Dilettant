#include "aboutdialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QSvgRenderer>
#include <QSysInfo>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace ld::app {

namespace {

// Adresse des Quellcodes (Pflicht bei GPL) und der Webseite des Autors.
const char* const kSourceUrl = "https://github.com/ohrenbeisser/Laser-Dilettant";
const char* const kWebsiteUrl = "https://ohrenbeisser.de";

// Zeichnet das Logo (SVG aus den Programmressourcen) in der gewünschten Größe.
// Die Zeichnung ist doppelt so groß wie angezeigt, damit sie auf hochauflösenden Bildschirmen scharf bleibt.
QPixmap logoPixmap(int size)
{
    QSvgRenderer renderer(QStringLiteral(":/about/logo.svg"));
    QPixmap pixmap(size * 2, size * 2);
    pixmap.fill(Qt::transparent);
    if (renderer.isValid()) {
        QPainter painter(&pixmap);
        renderer.render(&painter);
    }
    pixmap.setDevicePixelRatio(2.0);
    return pixmap;
}

QString readResourceText(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    return QString::fromUtf8(file.readAll());
}

}  // namespace

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Über Laser-Dilettant"));
    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget;
    tabs->addTab(createAboutTab(), tr("Über"));
    tabs->addTab(createLiabilityTab(), tr("Haftung"));
    tabs->addTab(createLicenseTab(), tr("Lizenzen"));
    layout->addWidget(tabs);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
    resize(620, 520);
}

QWidget* AboutDialog::createAboutTab()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 22, 24, 18);
    layout->setSpacing(22);   // Platz zwischen den Textblöcken

    auto* head = new QHBoxLayout;
    head->setSpacing(22);
    auto* logo = new QLabel;
    logo->setPixmap(logoPixmap(112));
    logo->setFixedSize(112, 112);
    head->addWidget(logo, 0, Qt::AlignTop);
    auto* title = new QLabel(tr("<h2 style=\"margin-top:0; margin-bottom:10px;\">Laser-Dilettant</h2>"
                                "<p style=\"margin:0 0 8px 0;\">Version %1 (beta)</p>"
                                "<p style=\"margin:0;\">Bilder für DIY-Diodenlaser (GRBL) aufbereiten und gravieren.</p>")
                                 .arg(QCoreApplication::applicationVersion()));
    title->setTextFormat(Qt::RichText);
    title->setWordWrap(true);
    head->addWidget(title, 1);
    layout->addLayout(head);

    auto* info = new QLabel(tr("<p style=\"margin:0 0 10px 0;\"><b>Autor:</b> Chris Seifert</p>"
                               "<p style=\"margin:0 0 10px 0;\"><b>Webseite:</b> <a href=\"%1\">ohrenbeisser.de</a></p>"
                               "<p style=\"margin:0;\"><b>Quellcode:</b> <a href=\"%2\">github.com/ohrenbeisser/Laser-Dilettant</a></p>")
                                .arg(QString::fromLatin1(kWebsiteUrl), QString::fromLatin1(kSourceUrl)));
    info->setTextFormat(Qt::RichText);
    info->setOpenExternalLinks(true);
    info->setWordWrap(true);
    layout->addWidget(info);

    auto* copyright = new QLabel(tr("© 2026 Chris Seifert. Freie Software unter der "
                                    "GNU General Public License, Version 3."));
    copyright->setWordWrap(true);
    layout->addWidget(copyright);

    // Technische Angaben: helfen bei Fehlermeldungen.
    auto* tech = new QLabel(tr("Qt %1 · %2").arg(QString::fromLatin1(qVersion()), QSysInfo::prettyProductName()));
    tech->setEnabled(false);   // gedämpfte Schrift
    tech->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(tech);
    layout->addStretch(1);
    return page;
}

QWidget* AboutDialog::createLiabilityTab()
{
    auto* browser = new QTextBrowser;
    browser->setOpenExternalLinks(true);
    browser->setHtml(
        tr("<h3>Sicherheit</h3>"
           "<p>Ein Diodenlaser kann <b>Augen und Haut verletzen</b> und <b>Brände auslösen</b>. "
           "Bitte beachten:</p>"
           "<ul>"
           "<li>Tragen Sie eine Schutzbrille, die zur Wellenlänge Ihres Lasers passt.</li>"
           "<li>Lassen Sie den Laser nie unbeaufsichtigt und halten Sie eine Löschmöglichkeit bereit.</li>"
           "<li>Brennen Sie nur Materialien, die sich dafür eignen. Manche Stoffe (zum Beispiel PVC) "
           "setzen giftige Dämpfe frei.</li>"
           "<li>Sorgen Sie für Abluft und freie Sicht auf den Arbeitsbereich.</li>"
           "</ul>"
           "<h3>Not-Aus</h3>"
           "<p>Der Not-Aus dieser Software ersetzt <b>keinen Hardware-Not-Aus</b>. "
           "Bei einem Abbruch der USB-Verbindung, einem Absturz des Rechners oder einem Programmfehler "
           "kann er ausfallen. Halten Sie die Stromversorgung des Lasers jederzeit erreichbar.</p>"
           "<h3>Beta-Version</h3>"
           "<p>Dieses Programm ist eine Beta-Version und kann Fehler enthalten. "
           "Prüfen Sie neue Einstellungen zuerst mit niedriger Leistung und an Probematerial.</p>"
           "<h3>Haftungsausschluss</h3>"
           "<p>Dieses Programm wird ohne jede Gewährleistung bereitgestellt, soweit das Gesetz es erlaubt. "
           "Die Nutzung geschieht auf eigene Gefahr. Der Autor haftet nicht für Schäden an Personen, "
           "Material, Laser oder Rechner, die durch die Nutzung entstehen.</p>"));
    return browser;
}

QWidget* AboutDialog::createLicenseTab()
{
    auto* splitter = new QSplitter(Qt::Vertical);

    auto* overview = new QTextBrowser;
    overview->setOpenExternalLinks(true);
    overview->setHtml(
        tr("<p><b>Laser-Dilettant</b> steht unter der <b>GNU General Public License, Version 3</b>. "
           "Dieses Programm kommt <b>ohne jede Garantie</b>. Sie dürfen es unter den Bedingungen der GPL "
           "weitergeben und ändern. Den Quellcode finden Sie unter "
           "<a href=\"%1\">github.com/ohrenbeisser/Laser-Dilettant</a>.</p>"
           "<p><b>Verwendete Bibliotheken:</b></p>"
           "<ul>"
           "<li><b>Qt %2</b> (Widgets, SerialPort, Svg, Network), LGPL Version 3, <a href=\"https://www.qt.io\">qt.io</a>. "
           "Qt wird dynamisch gelinkt.</li>"
           "<li><b>potrace 1.16</b> von Peter Selinger, GPL Version 2 oder später, "
           "<a href=\"https://potrace.sourceforge.net\">potrace.sourceforge.net</a></li>"
           "<li><b>nanosvg</b> von Mikko Mononen, zlib-Lizenz, "
           "<a href=\"https://github.com/memononen/nanosvg\">github.com/memononen/nanosvg</a></li>"
           "<li><b>Roboto, Roboto Mono, Montserrat Alt1</b> (Schriften der Fernansicht), SIL Open Font License</li>"
           "<li><b>Material Symbols</b> von Google (Symbole der Fernansicht), Apache-Lizenz 2.0</li>"
           "</ul>"
           "<p>Volltext der GPL unten.</p>")
            .arg(QString::fromLatin1(kSourceUrl), QString::fromLatin1(qVersion())));
    splitter->addWidget(overview);

    auto* licenseText = new QPlainTextEdit;
    licenseText->setReadOnly(true);
    licenseText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    licenseText->setLineWrapMode(QPlainTextEdit::NoWrap);
    const QString text = readResourceText(QStringLiteral(":/about/LICENSE"));
    licenseText->setPlainText(text.isEmpty() ? tr("Der Lizenztext konnte nicht geladen werden. Er steht in der Datei LICENSE "
                                                  "und unter https://www.gnu.org/licenses/gpl-3.0.html.")
                                             : text);
    splitter->addWidget(licenseText);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    return splitter;
}

}  // namespace ld::app
