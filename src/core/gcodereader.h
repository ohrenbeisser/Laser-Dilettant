// Liest G-Code zeilenweise und liefert die Fahrten mit Start, Ziel, Leistung und Vorschub.
// Es versteht G0, G1, G2/G3 (Bögen mit I/J oder R, als kurze Strecken), G90/G91, G20/G21, X, Y, S, F,
// M3/M4/M5, G4 (Pause, ohne Fahrt) und Kommentare in Klammern oder nach Semikolon. Alles andere wird ignoriert.
// Eine Ebene außer G17 und Achsen außer X/Y werden nicht ausgewertet. Siehe docs/planung.md 3.21.
#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QPointF>
#include <deque>

namespace ld::core {

struct GcodeMove {
    QPointF from;            // Maschinenkoordinaten in mm
    QPointF to;
    bool rapid = false;      // G0
    int power = 0;           // wirksamer S-Wert (0, wenn der Laser mit M5 aus ist)
    double feedMmPerMin = 0; // F
};

class GcodeReader {
public:
    explicit GcodeReader(QIODevice* device) : device_(device) {}

    // Nächste Fahrt lesen. false am Dateiende. Fahrten ohne Ortsänderung werden übersprungen.
    // Ein Bogen liefert mehrere kurze Fahrten nacheinander.
    bool next(GcodeMove& move);

    // Aktuelle Position vorgeben (Vorgabe: 0 / 0).
    void setPosition(double x, double y) { x_ = x; y_ = y; }

    // Größte Abweichung eines Bogens von den Strecken in mm.
    static constexpr double kArcToleranceMm = 0.02;

private:
    void addArc(double endX, double endY, double i, double j, double r, bool hasR, bool clockwise);

    QIODevice* device_;
    QByteArray line_;
    std::deque<GcodeMove> pending_;   // noch nicht ausgelieferte Teile eines Bogens
    double x_ = 0.0;
    double y_ = 0.0;
    int s_ = 0;
    double feed_ = 0.0;
    int motion_ = 0;          // 0 = G0, 1 = G1, 2 = G2, 3 = G3 (G0 ist die Grundeinstellung)
    bool relative_ = false;   // G91
    double unit_ = 1.0;       // mm je Einheit der Datei (G20: 25,4)
    bool laserOn_ = false;    // nach M3/M4 an, nach M5 aus
};

}  // namespace ld::core
