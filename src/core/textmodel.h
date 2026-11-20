// Text als Vektorgrafik (siehe docs/planung.md 3.22).
//
// Aus einem Text, einer Schriftart und Einstellungen entstehen die Umrisse der Buchstaben als QPainterPath in
// Millimetern (Ursprung oben links an der Ecke des umschließenden Rechtecks, Y nach unten). Daraus werden
//   * ein SvgModel gebaut (damit laufen Full und Cutting wie bei einer SVG-Datei),
//   * ein Rasterbild gezeichnet (für "Linie" und die Anzeige auf der Arbeitsfläche).
// Die Spezifikation (TextSpec) wird im Projekt gespeichert, so lässt sich der Text später wieder bearbeiten
// und das Ergebnis bleibt auch auf einem Rechner ohne die Schriftart gleich, weil es neu aus der Spezifikation
// gebaut wird (fehlt die Schrift, ersetzt Qt sie; die gespeicherten Werte bleiben erhalten).
#pragma once

#include <QImage>
#include <QJsonObject>
#include <QPainterPath>
#include <QSizeF>
#include <QString>
#include <memory>

#include "core/svgmodel.h"

namespace ld::core {

enum class TextAlign { Left = 0, Center = 1, Right = 2 };

struct TextSpec {
    QString text;                  // mehrere Zeilen sind erlaubt (Zeilenumbruch)
    QString fontFamily;            // leer = Standardschrift des Systems
    bool bold = false;
    bool italic = false;
    double heightMm = 10.0;        // Zeichenhöhe: Höhe der Großbuchstaben (Versalhöhe) in mm
    double letterSpacingPercent = 0.0;   // zusätzlicher Abstand zwischen den Buchstaben, in Prozent der Zeichenhöhe
    double lineSpacing = 1.0;      // Faktor auf den Zeilenabstand der Schrift
    TextAlign align = TextAlign::Left;

    friend bool operator==(const TextSpec&, const TextSpec&) = default;
};

// Zulässige Bereiche (die Oberfläche und das Lesen aus dem Projekt benutzen dieselben Grenzen).
constexpr double kTextMinHeightMm = 0.5;
constexpr double kTextMaxHeightMm = 400.0;
constexpr double kTextMinSpacingPercent = -30.0;
constexpr double kTextMaxSpacingPercent = 200.0;
constexpr double kTextMinLineSpacing = 0.5;
constexpr double kTextMaxLineSpacing = 3.0;

// Bringt alle Werte in die zulässigen Bereiche.
TextSpec sanitized(const TextSpec& spec);

struct TextShape {
    QPainterPath path;       // Umrisse in mm, Ursprung oben links (leer: nichts zu zeichnen)
    QSizeF sizeMm;           // Breite und Höhe des umschließenden Rechtecks der Umrisse
    bool isEmpty() const { return path.isEmpty() || sizeMm.width() <= 0.0 || sizeMm.height() <= 0.0; }
};

// Setzt den Text. Braucht eine QGuiApplication (Schriften).
TextShape layoutText(const TextSpec& spec);

// SVG-Modell der Umrisse (eine gefüllte schwarze Form, Pfade geschlossen).
std::shared_ptr<const SvgModel> textToSvgModel(const TextShape& shape);

// Rasterbild: schwarz auf weiß (Format_RGB32), ein Pixel = pixelSizeMm. Bei mehr als maxPixels wird gröber gezeichnet.
QImage renderTextRaster(const TextShape& shape, double pixelSizeMm, double maxPixels = 64.0e6);

// Alles, was ein Text-Auftrag dem Dokument liefert: Rasterbild, Vektordaten und Größe.
struct TextContent {
    TextShape shape;
    QImage image;                          // leer, wenn der Text keine Umrisse hat
    std::shared_ptr<const SvgModel> svg;
    bool ok() const { return !image.isNull() && svg; }
};
TextContent makeTextContent(const TextSpec& spec, double pixelSizeMm);

// Speichern im Projekt (JSON); fromJson korrigiert ungültige Werte.
QJsonObject textSpecToJson(const TextSpec& spec);
TextSpec textSpecFromJson(const QJsonObject& json);

}  // namespace ld::core
