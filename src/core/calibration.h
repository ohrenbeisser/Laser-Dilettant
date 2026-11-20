// Kalibrierung (siehe docs/planung.md 3.25): Eine Messgrafik wird gebrannt, der Bediener misst sie nach und trägt die
// Messwerte ein; daraus entstehen Korrekturfaktoren für die Maschinenachsen (Maschinenprofil).
//
// Die Grafik ist ein "Winkel" (L): eine waagerechte Achse der Länge X und eine senkrechte Achse der Länge Y mit dem
// Eckpunkt unten links, dazu Teilstriche alle 10 mm mit Zahlen. Im Winkel liegt ein Kreis, dessen Mittelpunkt auf
// der Hälfte beider Achsen sitzt (bei 100 mm also bei 50 / 50, der Kreis berührt beide Achsen). Ist ein Maßstab falsch,
// werden Achsen und Kreis (Ellipse) in dieser Richtung zu lang oder zu kurz.
// Die Grafik besteht nur aus Linien (Cutting) und wird als Vektordaten (SvgModel) in den Auftrag gesetzt.
#pragma once

#include <QImage>
#include <QJsonObject>
#include <QPainterPath>
#include <QSizeF>
#include <memory>
#include <optional>
#include <vector>

#include "core/svgmodel.h"

namespace ld::core {

struct CalibrationSpec {
    double lengthXMm = 100.0;         // Länge der waagerechten Achse
    double lengthYMm = 100.0;         // Länge der senkrechten Achse
    double circleDiameterMm = 100.0;  // Durchmesser des Kreises

    friend bool operator==(const CalibrationSpec&, const CalibrationSpec&) = default;
};

constexpr double kCalibrationMinMm = 20.0;
constexpr double kCalibrationMaxMm = 450.0;
constexpr double kCalibrationTickMm = 10.0;      // Abstand der Teilstriche
constexpr double kCalibrationLineWidthMm = 0.2;  // Breite der Linien in der Anzeige

CalibrationSpec sanitized(const CalibrationSpec& spec);

struct CalibrationShape {
    QPainterPath path;                 // alle Linien in mm, Ursprung oben links, Y nach unten
    std::vector<SvgSubpath> strokes;   // dieselben Linien als Bézierstücke (mm)
    QSizeF sizeMm;
    // Eckpunkt des Winkels (Schnittpunkt der Achsen) in der Form, in mm (Ursprung oben links).
    QPointF cornerMm;
    bool isEmpty() const { return strokes.empty() || sizeMm.isEmpty(); }
};

CalibrationShape layoutCalibration(const CalibrationSpec& spec);
std::shared_ptr<const SvgModel> calibrationToSvgModel(const CalibrationShape& shape);
// Schwarz auf weiß (Format_RGB32), ein Pixel = pixelSizeMm.
QImage renderCalibrationRaster(const CalibrationShape& shape, double pixelSizeMm);

struct CalibrationContent {
    CalibrationShape shape;
    QImage image;
    std::shared_ptr<const SvgModel> svg;
    bool ok() const { return !image.isNull() && svg; }
};
CalibrationContent makeCalibrationContent(const CalibrationSpec& spec, double pixelSizeMm);

QJsonObject calibrationSpecToJson(const CalibrationSpec& spec);
CalibrationSpec calibrationSpecFromJson(const QJsonObject& json);

// Korrekturfaktor einer Achse aus den Messwerten (Soll / gemessen). Ein Messwert 0 gilt als "nicht gemessen".
// Sind Achse und Kreis gemessen, zählt der Mittelwert beider Verhältnisse. Ohne Messwert: kein Ergebnis.
// Das Ergebnis liegt immer zwischen 0,5 und 2.
std::optional<double> calibrationFactor(double nominalAxisMm, double measuredAxisMm, double nominalCircleMm,
                                        double measuredCircleMm);

}  // namespace ld::core
