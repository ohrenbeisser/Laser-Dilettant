// NC-Dateien laden (siehe docs/planung.md 3.21): Eine fremde G-Code-Datei wird geprüft und in eine bereinigte
// Kopie geschrieben, die an den Laser gesendet wird. Die Originaldatei bleibt unverändert.
//
//  * Zeilen, die nicht gesendet werden dürfen, bleiben draußen (Einstellungen "$…", Befehle, die Koordinaten oder
//    den Nullpunkt verschieben, unbekannte Befehle, zu lange Zeilen). Sie werden gemeldet.
//  * Ein S über dem Maximum der Maschine wird auf das Maximum begrenzt (mit Meldung).
//  * Kommentare und überflüssige Leerzeichen entfallen, damit der Empfangspuffer von GRBL reicht.
#pragma once

#include <QString>
#include <atomic>
#include <functional>
#include <vector>

#include "core/machinesetup.h"

namespace ld::core {

struct NcIssue {
    long long line = 0;      // Zeilennummer in der Originaldatei (ab 1)
    QString text;            // was los ist
    QString content;         // die betroffene Zeile (gekürzt)
    bool blocked = false;    // true: Die Zeile wird nicht gesendet; false: Sie wurde geändert (S begrenzt)
};

struct NcAnalysis {
    bool ok = false;
    bool cancelled = false;
    QString error;
    long long lines = 0;           // Zeilen der Originaldatei
    long long sentLines = 0;       // Zeilen mit Inhalt in der bereinigten Kopie
    long long blockedLines = 0;
    long long clampedLines = 0;
    int maxS = 0;                  // größter S-Wert in der Originaldatei
    std::vector<NcIssue> issues;   // die ersten Meldungen (höchstens maxIssues)
    long long moreIssues = 0;      // weitere, nicht aufgeführte Meldungen
    bool hasProblems() const { return blockedLines > 0 || clampedLines > 0; }
};

constexpr size_t kMaxNcIssues = 200;

// Prüft src und schreibt die bereinigte Kopie nach dst. ok = false bei Lese- oder Schreibfehler (error sagt warum)
// oder Abbruch (cancelled).
NcAnalysis sanitizeNcFile(const QString& src, const QString& dst, const MachineSetup& machine,
                          const std::atomic_bool* cancel = nullptr,
                          const std::function<void(double)>& progress = {});

}  // namespace ld::core
