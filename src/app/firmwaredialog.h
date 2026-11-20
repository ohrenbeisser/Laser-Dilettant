// Firmware-Dialog (Menü Maschine): liest die Firmware des Lasers aus ($I, $$ und $#) und zeigt sie an.
// Er ist modal und liest beim Öffnen sowie bei "Neu lesen". Nichts wird geschrieben; es sind nur lesende Abfragen.
// "Kopieren" legt den ganzen Bericht als Text in die Zwischenablage (zum Einfügen in Mails oder Dokumentation).
#pragma once

#include <QDialog>
#include <QMap>
#include <QString>
#include <QStringList>

#include "firmwareupdatedialog.h"

class QLabel;
class QPlainTextEdit;
class QTableWidget;

namespace ld::grbl {
class GrblConnection;
}

namespace ld::app {

// Zerlegt die Optionen aus "$I" (zum Beispiel "VMHL,15,128") in lesbare Zeilen: je Buchstabe eine Bedeutung,
// dazu "Planer-Puffer: 15 Blöcke" und "Empfangspuffer: 128 Bytes". Unbekannte Buchstaben werden als solche genannt.
QStringList describeGrblOptions(const QString& options);

// Der Bericht als reiner Text (für die Zwischenablage): Version, Optionen, Einstellungen, Koordinatensysteme.
QString firmwareReportText(const QString& version, const QString& options, const QMap<int, QString>& settings,
                           const QStringList& parameters);

class FirmwareDialog : public QDialog {
    Q_OBJECT

public:
    FirmwareDialog(ld::grbl::GrblConnection* grbl, const FirmwareUpdateContext& updateContext, QWidget* parent = nullptr);

private:
    void reload();
    void refresh();   // zeigt, was bisher angekommen ist

    ld::grbl::GrblConnection* grbl_;
    FirmwareUpdateContext updateContext_;   // für den Dialog "Firmware-Update"
    QLabel* status_;
    QLabel* versionLabel_;
    QPlainTextEdit* optionsText_;
    QTableWidget* settingsTable_;
    QPlainTextEdit* parametersText_;

    QString version_, options_;
    QMap<int, QString> settings_;
    QStringList parameters_;
    bool gotInfo_ = false, gotSettings_ = false, gotParameters_ = false;
};

}  // namespace ld::app
