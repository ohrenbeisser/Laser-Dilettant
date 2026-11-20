// Einstellungen der Bildbearbeitung (Importdialog, siehe docs/planung.md 3.9).
// Sie liegen getrennt vom Originalbild: Das Original bleibt unverändert (nicht-destruktiv).
#pragma once

#include <QRect>
#include <QSize>

namespace ld::core {

// Wie das vorbereitete Graubild in Laserpunkte umgewandelt wird.
enum class ConversionMethod {
    Threshold,        // Schwellenwert: schwarz oder weiß
    FloydSteinberg,   // Dithering mit Fehlerverteilung (klassisch)
    Atkinson,         // Dithering mit Fehlerverteilung (kontrastreicher, verliert etwas Fehler)
    Bayer,            // geordnetes Dithering mit 8 x 8 Matrix
    Grayscale,        // Graustufen-Leistung: das Graubild bleibt, wie es ist (Abbildung auf Leistung: M8)
};

// Einstellungen der Vektorisierung (potrace) für Full und Cutting, siehe planung.md 3.14.
struct VectorizeSettings {
    int turdSize = 2;          // Flecken bis zu so vielen Pixeln werden weggelassen (0..100)
    double alphaMax = 1.0;     // Eckenschärfe: 0 = alles Ecken, 1,33 = alles rund
    double optTolerance = 0.2; // Kurven vereinfachen: größer = weniger Kurvenstücke

    friend bool operator==(const VectorizeSettings&, const VectorizeSettings&) = default;
};

// Abbildung der Graustufen auf Laserleistung (nur Verfahren "Graustufen-Leistung", siehe planung.md 3.16).
struct GrayPowerSettings {
    int minPercent = 10;     // Leistung für fast weiße Punkte, in % der Gravur-Leistung (reines Weiß bleibt aus)
    int maxPercent = 100;    // Leistung für Schwarz, in % der Gravur-Leistung
    int levels = 32;         // die Leistung wird auf so viele Stufen gerundet (hält den G-Code klein), 8..64

    friend bool operator==(const GrayPowerSettings&, const GrayPowerSettings&) = default;
};

// Was aus einer SVG-Datei verwendet wird (siehe planung.md 3.14).
struct SvgSettings {
    bool fullStrokes = false;   // Full: Striche zusätzlich als Fläche in Strichbreite
    bool cutStrokes = true;     // Cutting: Striche als Mittellinie
    bool cutFills = false;      // Cutting: Umrisse der Füllungen

    friend bool operator==(const SvgSettings&, const SvgSettings&) = default;
};

struct ImageSettings {
    ConversionMethod method = ConversionMethod::Threshold;

    // Schwelle (0..255): Pixel mit Grauwert < Schwelle werden schwarz (= Laser brennt).
    // Bei autoThreshold wird stattdessen der Otsu-Wert benutzt.
    bool autoThreshold = true;
    int threshold = 128;

    int brightness = 0;   // -100..100
    int contrast = 0;     // -100..100
    double gamma = 1.0;   // 0.2..3.0; > 1 macht heller
    int sharpen = 0;      // 0..100 (Prozent, Unscharf maskieren)

    // Punktgröße beim Dithering in Laserpixeln (1..8): Das Bild wird in Blöcke dieser Kantenlänge
    // eingeteilt und pro Block gedithert. 1 = feinste Stufe. Gilt nicht beim Schwellenwert.
    int ditherCell = 1;
    bool invert = false;

    // Vektorisierung für Full und Cutting (der Schwellenwert oben bestimmt, was "schwarz" ist).
    VectorizeSettings vectorize;
    SvgSettings svg;   // nur bei SVG-Dateien
    GrayPowerSettings grayPower;   // nur bei Graustufen-Leistung

    // Ausschnitt in Pixeln des Originalbildes. Ein leeres Rechteck bedeutet: ganzes Bild.
    QRect crop;

    // Abstand der Laserpunkte in mm. Das Arbeitsbild hat Breite / pixelSizeMm Pixel.
    double pixelSizeMm = 0.0635;

    friend bool operator==(const ImageSettings&, const ImageSettings&) = default;
};

// Der tatsächlich benutzte Ausschnitt: bei leerem oder ungültigem crop das ganze Bild,
// sonst crop, auf das Bild begrenzt.
QRect effectiveCrop(const ImageSettings& settings, const QSize& imageSize);

}  // namespace ld::core
