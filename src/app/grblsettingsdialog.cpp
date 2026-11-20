#include "grblsettingsdialog.h"
#include "helpwindow.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "grbl/grblconnection.h"

namespace ld::app {

QString grblSettingDescription(int number)
{
    switch (number) {
    case 0: return QObject::tr("Schrittimpuls (µs)");
    case 1: return QObject::tr("Leerlaufverzögerung der Motoren (ms)");
    case 2: return QObject::tr("Schrittimpuls invertieren (Maske)");
    case 3: return QObject::tr("Fahrtrichtung invertieren (Maske, X=1 Y=2 Z=4)");
    case 4: return QObject::tr("Motor-Freigabe invertieren");
    case 5: return QObject::tr("Endschalter invertieren");
    case 6: return QObject::tr("Taster invertieren");
    case 10: return QObject::tr("Statusbericht (Maske)");
    case 11: return QObject::tr("Eckenabweichung (mm)");
    case 12: return QObject::tr("Bogentoleranz (mm)");
    case 13: return QObject::tr("Berichte in Zoll");
    case 20: return QObject::tr("Soft-Limits (braucht Referenzfahrt)");
    case 21: return QObject::tr("Hard-Limits (Endschalter)");
    case 22: return QObject::tr("Referenzfahrt");
    case 23: return QObject::tr("Richtung der Referenzfahrt invertieren");
    case 24: return QObject::tr("Referenzfahrt langsam (mm/min)");
    case 25: return QObject::tr("Referenzfahrt suchen (mm/min)");
    case 26: return QObject::tr("Entprellung der Referenzfahrt (ms)");
    case 27: return QObject::tr("Abstand nach der Referenzfahrt (mm)");
    case 30: return QObject::tr("Maximale Leistung S (Obergrenze für S)");
    case 31: return QObject::tr("Minimale Leistung S");
    case 32: return QObject::tr("Laser-Modus (1 = an, nötig für M4)");
    case 100: return QObject::tr("Schritte pro mm, X");
    case 101: return QObject::tr("Schritte pro mm, Y");
    case 102: return QObject::tr("Schritte pro mm, Z");
    case 110: return QObject::tr("Höchste Geschwindigkeit X (mm/min)");
    case 111: return QObject::tr("Höchste Geschwindigkeit Y (mm/min)");
    case 112: return QObject::tr("Höchste Geschwindigkeit Z (mm/min)");
    case 120: return QObject::tr("Beschleunigung X (mm/s²)");
    case 121: return QObject::tr("Beschleunigung Y (mm/s²)");
    case 122: return QObject::tr("Beschleunigung Z (mm/s²)");
    case 130: return QObject::tr("Höchster Verfahrweg X (mm)");
    case 131: return QObject::tr("Höchster Verfahrweg Y (mm)");
    case 132: return QObject::tr("Höchster Verfahrweg Z (mm)");
    default: break;
    }
    return QObject::tr("(unbekannt)");
}

GrblSettingsDialog::GrblSettingsDialog(ld::grbl::GrblConnection* grbl, QWidget* parent) : QDialog(parent), grbl_(grbl)
{
    setWindowTitle(tr("GRBL-Parameter"));
    attachHelp(this, ld::core::helptopic::kMachineProfile);   // F1 und Knopf "Hilfe" (Handbuch)
    resize(640, 560);
    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(tr("Diese Werte liegen im Speicher von GRBL. Änderungen wirken sofort und bleiben nach dem Ausschalten erhalten. "
                               "Bitte nur ändern, was du verstehst: Falsche Werte können den Laser falsch fahren lassen."));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    table_ = new QTableWidget(0, 3);
    table_->setHorizontalHeaderLabels({tr("$"), tr("Bedeutung"), tr("Wert")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->verticalHeader()->hide();
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(table_, 1);

    status_ = new QLabel;
    layout->addWidget(status_);

    auto* buttons = new QDialogButtonBox;
    auto* reloadButton = buttons->addButton(tr("Neu lesen"), QDialogButtonBox::ActionRole);
    auto* writeButton = buttons->addButton(tr("Änderungen schreiben"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(reloadButton, &QPushButton::clicked, this, &GrblSettingsDialog::reload);
    connect(writeButton, &QPushButton::clicked, this, &GrblSettingsDialog::writeChanges);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

    connect(grbl_, &ld::grbl::GrblConnection::settingsRead, this, &GrblSettingsDialog::fill);
    connect(grbl_, &ld::grbl::GrblConnection::commandError, this, [this](int code, const QString& text, const QString& line) {
        status_->setText(tr("Fehler %1 bei \"%2\": %3").arg(code).arg(line, text));
    });
    reload();
}

void GrblSettingsDialog::reload()
{
    if (!grbl_->readSettings()) {
        status_->setText(tr("Die Werte lassen sich jetzt nicht lesen (keine Verbindung oder ein Auftrag läuft)."));
        return;
    }
    status_->setText(tr("Werte werden gelesen …"));
}

void GrblSettingsDialog::fill(const QMap<int, QString>& values)
{
    original_ = values;
    table_->setRowCount(0);
    for (auto it = values.begin(); it != values.end(); ++it) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto* number = new QTableWidgetItem(QString::number(it.key()));
        number->setFlags(number->flags() & ~Qt::ItemIsEditable);
        auto* description = new QTableWidgetItem(grblSettingDescription(it.key()));
        description->setFlags(description->flags() & ~Qt::ItemIsEditable);
        auto* value = new QTableWidgetItem(it.value());
        table_->setItem(row, 0, number);
        table_->setItem(row, 1, description);
        table_->setItem(row, 2, value);
    }
    status_->setText(tr("%1 Werte gelesen.").arg(values.size()));
}

void GrblSettingsDialog::writeChanges()
{
    QList<QPair<int, QString>> changes;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const int number = table_->item(row, 0)->text().toInt();
        const QString value = table_->item(row, 2)->text().trimmed();
        bool ok = false;
        value.toDouble(&ok);
        if (!ok) {
            status_->setText(tr("$%1: \"%2\" ist keine Zahl.").arg(number).arg(value));
            return;
        }
        if (original_.value(number) != value) changes.append({number, value});
    }
    if (changes.isEmpty()) {
        status_->setText(tr("Nichts geändert."));
        return;
    }
    QString list;
    for (const auto& [number, value] : changes) {
        list += tr("$%1 (%2): %3 → %4\n").arg(number).arg(grblSettingDescription(number), original_.value(number), value);
    }
    if (QMessageBox::question(this, tr("GRBL-Parameter schreiben"), tr("Diese Werte in GRBL überschreiben?\n\n%1").arg(list),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    for (const auto& [number, value] : changes) {
        if (!grbl_->sendLine(QStringLiteral("$%1=%2").arg(number).arg(value))) {
            status_->setText(tr("Schreiben nicht möglich (keine Verbindung oder ein Auftrag läuft)."));
            return;
        }
    }
    reload();   // Zur Kontrolle neu lesen
}

}  // namespace ld::app
