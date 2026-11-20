// Gemeinsamer Stil für Umschalt-Knöpfe, von denen genau einer aktiv ist (Full / Cutting / Linie,
// Bild / Laserwege / Beides). Der aktive Knopf ist deutlich hervorgehoben: Hervorhebungsfarbe
// des Designs, weiße Schrift, fett. Die Farben kommen aus der Palette und folgen deshalb dem
// hellen und dem dunklen Design.
#pragma once

#include <QPushButton>
#include <QString>

namespace ld::app {

inline void applySegmentStyle(QPushButton* button)
{
    button->setStyleSheet(QStringLiteral(
        "QPushButton { padding: 4px 8px; border: 1px solid palette(mid); background: palette(button); color: palette(button-text); }"
        "QPushButton:hover:!checked { background: palette(midlight); }"
        "QPushButton:checked { background: palette(highlight); color: palette(highlighted-text);"
        " border: 1px solid palette(highlight); font-weight: 700; }"
        "QPushButton:disabled { color: palette(placeholder-text); }"));
}

}  // namespace ld::app
