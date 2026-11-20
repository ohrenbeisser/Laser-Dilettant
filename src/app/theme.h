// Hell/Dunkel-Design der ganzen Anwendung.
// Qt kann das Farbschema nicht auf jedem System selbst umschalten (unter Linux oft wirkungslos).
// Deshalb setzt die App im Fusion-Stil eine eigene Palette: dunkel nach dem Mockup
// (docs/mockup/oberflaeche.html), hell mit der Standardpalette des Stils.
#pragma once

#include <QIcon>
#include <QPalette>

#include "appsettings.h"

namespace ld::app {

// Die dunkle Palette (Farben aus dem Mockup).
QPalette darkPalette();
// Die helle Palette (fest, damit das helle Design nicht vom Systemdesign abhängt).
QPalette lightPalette();

// Gezeichnete Symbole für den Umschalter: Sonne (zum hellen Design) bzw. Mond (zum dunklen Design).
QIcon themeIcon(bool sun, const QColor& color);

// Wendet die Wahl an (Hell, Dunkel, System) und meldet, ob jetzt das dunkle Design aktiv ist.
// Bei "System" wird das Farbschema des Systems übernommen.
bool applyTheme(DesignChoice choice);

}  // namespace ld::app
