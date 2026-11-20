// Das Handbuch (MANUAL.md) als Hilfetext: in Kapitel zerlegen, Themen und Sprungmarken finden (ohne Fenster).
//
// Die Hilfe im Programm zeigt dieselbe Datei wie das Handbuch im Repository. Sie wird als Ressource eingebettet. Hier
// steckt nur, was sich ohne Oberfläche prüfen lässt: Zerlegen nach Überschriften, "Themen" der Dialoge auf Kapitel
// abbilden (F1 im Dialog) und Links der Form "#3-den-laser-anschließen" auflösen.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace ld::core {

// Themen der Dialoge: Teil einer Überschrift im Handbuch (ohne Rücksicht auf Groß- und Kleinschreibung). Ein Test
// prüft, dass jedes Thema im Handbuch vorkommt, damit eine umbenannte Überschrift nicht still die Hilfe zerstört.
namespace helptopic {
inline constexpr const char* kOverview = "";                                  // das ganze Handbuch
inline constexpr const char* kConnect = "Den Laser anschließen";
inline constexpr const char* kMachineProfile = "Das Maschinenprofil";
inline constexpr const char* kImageEdit = "Bild bearbeiten";
inline constexpr const char* kPlacement = "Platzieren, Nullpunkt und Rahmen";
inline constexpr const char* kEngraving = "Gravurarten und Einstellungen";
inline constexpr const char* kBurning = "Brennen";
inline constexpr const char* kMaterialTest = "Materialtest (Maschine";
inline constexpr const char* kCalibration = "Kalibrierung (Maschine";
inline constexpr const char* kText = "Text (Strg+T)";
inline constexpr const char* kSettings = "Einstellungen";
inline constexpr const char* kProtocol = "Protokolle";
inline constexpr const char* kNtfy = "Meldungen aufs Handy";
inline constexpr const char* kRemote = "Die Fernansicht im Heimnetz";
inline constexpr const char* kFirmware = "Firmware des Lasers";
inline constexpr const char* kFirmwareUpdate = "Firmware-Update: Vorsicht";
}  // namespace helptopic

struct HelpHeading {
    int level = 2;      // 2 = Kapitel, 3 = Abschnitt
    QString title;
    QString slug;       // Sprungmarke wie auf GitHub: "1-sicherheit"
};

struct HelpChapter {
    QString title;                 // "1. Sicherheit"; die Einleitung heißt "Einleitung"
    QString markdown;              // der Text des Kapitels samt Überschrift
    QList<HelpHeading> headings;   // alle Überschriften des Kapitels (Ebene 2 und 3)
};

struct HelpLocation {
    int chapter = -1;
    QString heading;               // Überschrift des Abschnitts (leer: Anfang des Kapitels)
    bool valid() const { return chapter >= 0; }
};

// Sprungmarke einer Überschrift (GitHub-Art): klein, Leerzeichen zu "-", nur Buchstaben, Ziffern, "-" und "_".
QString helpSlug(const QString& heading);

// Bereitet das Handbuch für die Hilfe auf: Markierungen "(am Laser bestätigen)" werden zu "noch nicht am Laser bestätigt",
// damit der Satz lesbar bleibt und die Unsicherheit nicht verschwindet.
QString prepareHelpMarkdown(const QString& manual);

class HelpDocument {
public:
    // Zerlegt das (aufbereitete) Handbuch in Kapitel. Das Inhaltsverzeichnis des Handbuchs entfällt, die Hilfe hat ein eigenes.
    static HelpDocument parse(const QString& markdown);

    const QList<HelpChapter>& chapters() const { return chapters_; }
    // Erste Überschrift, die den Text enthält (leeres Thema: Anfang der Einleitung).
    HelpLocation findTopic(const QString& topic) const;
    // Überschrift zu einer Sprungmarke ("#...", mit oder ohne "#").
    HelpLocation findAnchor(const QString& anchor) const;
    // Kapitel, in denen der Text vorkommt (ohne Rücksicht auf Groß- und Kleinschreibung).
    QList<int> searchChapters(const QString& text) const;

private:
    QList<HelpChapter> chapters_;
};

}  // namespace ld::core
