#include "firmwaredialog.h"
#include "helpwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "grblsettingsdialog.h"
#include "grbl/grblconnection.h"

namespace ld::app {

QStringList describeGrblOptions(const QString& options)
{
    QStringList lines;
    if (options.trimmed().isEmpty()) return lines;
    // Aufbau: "Buchstaben,Planer-Blöcke,Empfangspuffer-Bytes". Die Buchstaben sind Merkmale, mit denen GRBL gebaut wurde.
    const QStringList parts = options.split(QLatin1Char(','));
    for (const QChar letter : parts.value(0)) {
        QString meaning;
        switch (letter.toLatin1()) {
        case 'V': meaning = QObject::tr("Variable Spindel (Laserleistung per S-Wert)"); break;
        case 'N': meaning = QObject::tr("Zeilennummern (N) werden angenommen"); break;
        case 'M': meaning = QObject::tr("Mist-Kühlung (M7) vorhanden"); break;
        case 'C': meaning = QObject::tr("CoreXY-Kinematik"); break;
        case 'P': meaning = QObject::tr("Parken (Parking-Bewegung) vorhanden"); break;
        case 'Z': meaning = QObject::tr("Referenzfahrt setzt den Ursprung erzwungen"); break;
        case 'H': meaning = QObject::tr("Referenzfahrt einzelner Achsen möglich"); break;
        case 'L': meaning = QObject::tr("Zwei Endschalter-Pins je Achse"); break;
        case 'A': meaning = QObject::tr("Vorschub-Regler auch beim Antasten erlaubt"); break;
        case '*': meaning = QObject::tr("Zurücksetzen des ganzen EEPROM ($RST=*) abgeschaltet"); break;
        case '$': meaning = QObject::tr("Zurücksetzen der Einstellungen ($RST=$) abgeschaltet"); break;
        case '#': meaning = QObject::tr("Zurücksetzen der Parameter ($RST=#) abgeschaltet"); break;
        case 'I': meaning = QObject::tr("Schreiben der Bauinfo-Zeichenkette abgeschaltet"); break;
        case 'E': meaning = QObject::tr("Erzwungenes Synchronisieren beim EEPROM-Schreiben abgeschaltet"); break;
        case 'W': meaning = QObject::tr("Erzwungenes Synchronisieren bei Änderung der Werkstück-Versätze abgeschaltet"); break;
        default: meaning = QObject::tr("(unbekanntes Merkmal)"); break;
        }
        lines << QStringLiteral("%1: %2").arg(letter, meaning);
    }
    if (parts.size() > 1) lines << QObject::tr("Planer-Puffer: %1 Blöcke").arg(parts.value(1));
    if (parts.size() > 2) lines << QObject::tr("Empfangspuffer: %1 Bytes").arg(parts.value(2));
    return lines;
}

QString firmwareReportText(const QString& version, const QString& options, const QMap<int, QString>& settings,
                           const QStringList& parameters)
{
    QStringList out;
    out << QObject::tr("Version ($I): %1").arg(version);
    out << QObject::tr("Optionen: %1").arg(options);
    for (const QString& line : describeGrblOptions(options)) out << QStringLiteral("  ") + line;
    out << QString() << QObject::tr("Einstellungen ($$):");
    for (auto it = settings.begin(); it != settings.end(); ++it)
        out << QStringLiteral("$%1=%2   (%3)").arg(it.key()).arg(it.value(), grblSettingDescription(it.key()));
    out << QString() << QObject::tr("Koordinatensysteme ($#):");
    for (const QString& line : parameters) out << QStringLiteral("[%1]").arg(line);
    return out.join(QLatin1Char('\n'));
}

FirmwareDialog::FirmwareDialog(ld::grbl::GrblConnection* grbl, const FirmwareUpdateContext& updateContext, QWidget* parent)
    : QDialog(parent), grbl_(grbl), updateContext_(updateContext)
{
    setWindowTitle(tr("Firmware des Lasers"));
    attachHelp(this, ld::core::helptopic::kFirmware);   // F1 und Knopf "Hilfe" (Handbuch)
    resize(680, 640);
    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(tr("Die App fragt den Laser nach Version ($I), Einstellungen ($$) und Koordinatensystemen ($#). "
                               "Es wird nichts verändert."));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    versionLabel_ = new QLabel;
    versionLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    versionLabel_->setWordWrap(true);
    layout->addWidget(versionLabel_);

    layout->addWidget(new QLabel(tr("Optionen der Firmware:")));
    optionsText_ = new QPlainTextEdit;
    optionsText_->setReadOnly(true);
    optionsText_->setMaximumHeight(120);
    layout->addWidget(optionsText_);

    layout->addWidget(new QLabel(tr("Einstellungen ($$):")));
    settingsTable_ = new QTableWidget(0, 3);
    settingsTable_->setHorizontalHeaderLabels({tr("$"), tr("Bedeutung"), tr("Wert")});
    settingsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    settingsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    settingsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    settingsTable_->verticalHeader()->hide();
    settingsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(settingsTable_, 1);

    layout->addWidget(new QLabel(tr("Koordinatensysteme ($#):")));
    parametersText_ = new QPlainTextEdit;
    parametersText_->setReadOnly(true);
    parametersText_->setMaximumHeight(150);
    layout->addWidget(parametersText_);

    status_ = new QLabel;
    layout->addWidget(status_);

    auto* buttons = new QDialogButtonBox;
    auto* reloadButton = buttons->addButton(tr("Neu lesen"), QDialogButtonBox::ActionRole);
    auto* copyButton = buttons->addButton(tr("Kopieren"), QDialogButtonBox::ActionRole);
    auto* updateButton = buttons->addButton(tr("Firmware-Update …"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(updateButton, &QPushButton::clicked, this, [this] {
        // Aktuellen Bericht und Version mitgeben (zum Sichern vor dem Update); danach muss neu gelesen werden.
        FirmwareUpdateContext context = updateContext_;
        context.currentVersion = version_;
        context.reportText = firmwareReportText(version_, options_, settings_, parameters_);
        FirmwareUpdateDialog dialog(grbl_, context, this);
        dialog.exec();
        status_->setText(tr("Nach einem Update: warten, bis der Laser wieder verbunden ist, dann \"Neu lesen\"."));
    });
    connect(reloadButton, &QPushButton::clicked, this, &FirmwareDialog::reload);
    connect(copyButton, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(firmwareReportText(version_, options_, settings_, parameters_));
        status_->setText(tr("Bericht in die Zwischenablage kopiert."));
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

    connect(grbl_, &ld::grbl::GrblConnection::infoRead, this, [this](const QString& version, const QString& options) {
        version_ = version;
        options_ = options;
        gotInfo_ = true;
        refresh();
    });
    connect(grbl_, &ld::grbl::GrblConnection::settingsRead, this, [this](const QMap<int, QString>& settings) {
        settings_ = settings;
        gotSettings_ = true;
        refresh();
    });
    connect(grbl_, &ld::grbl::GrblConnection::parametersRead, this, [this](const QStringList& parameters) {
        parameters_ = parameters;
        gotParameters_ = true;
        refresh();
    });
    connect(grbl_, &ld::grbl::GrblConnection::commandError, this, [this](int code, const QString& text, const QString& line) {
        if (line == QLatin1String("$#")) gotParameters_ = true;   // die Firmware kennt $# nicht: ohne Koordinatensysteme weiter
        status_->setText(tr("Fehler %1 bei \"%2\": %3").arg(code).arg(line, text));
        refresh();
    });
    reload();
}

void FirmwareDialog::reload()
{
    gotInfo_ = gotSettings_ = gotParameters_ = false;
    version_.clear();
    options_.clear();
    settings_.clear();
    parameters_.clear();
    refresh();
    // Die drei Abfragen laufen hintereinander; jede Antwort kommt als Signal (siehe oben).
    if (!grbl_->readInfo() || !grbl_->readSettings() || !grbl_->readParameters()) {
        status_->setText(tr("Die Firmware lässt sich jetzt nicht lesen (keine Verbindung oder ein Auftrag läuft)."));
        return;
    }
    status_->setText(tr("Firmware wird gelesen …"));
}

void FirmwareDialog::refresh()
{
    versionLabel_->setText(gotInfo_ ? tr("<b>Version:</b> %1").arg(version_.toHtmlEscaped()) : tr("<b>Version:</b> –"));
    optionsText_->setPlainText(gotInfo_ ? options_ + QLatin1Char('\n') + describeGrblOptions(options_).join(QLatin1Char('\n'))
                                        : QString());
    settingsTable_->setRowCount(0);
    for (auto it = settings_.begin(); it != settings_.end(); ++it) {
        const int row = settingsTable_->rowCount();
        settingsTable_->insertRow(row);
        settingsTable_->setItem(row, 0, new QTableWidgetItem(QString::number(it.key())));
        settingsTable_->setItem(row, 1, new QTableWidgetItem(grblSettingDescription(it.key())));
        settingsTable_->setItem(row, 2, new QTableWidgetItem(it.value()));
    }
    parametersText_->setPlainText(parameters_.join(QLatin1Char('\n')));
    if (gotInfo_ && gotSettings_ && gotParameters_) status_->setText(tr("Gelesen: %1 Einstellungen.").arg(settings_.size()));
}

}  // namespace ld::app
