# Laser-Dilettant – Spezifikation für die Umsetzung

Stand: 13.10.2026
Autor und Auftraggeber: Chris Seifert („Ohrenbeisser“)
Zielgruppe dieses Dokuments: Claude Code als ausführender Entwickler

---

## 0. Arbeitsanweisung für Claude Code

Lies dieses Dokument vollständig, bevor du Code schreibst. Es enthält alle Entscheidungen, die in der Planung getroffen wurden. Wenn etwas unklar oder widersprüchlich ist, frage nach, statt zu raten.

Regeln für die Zusammenarbeit:

1. **Etappenweise arbeiten.** Setze die Etappen aus Abschnitt 14 der Reihe nach um. Nach jeder Etappe hältst du an, fasst zusammen, was fertig ist, und wartest, bis der Auftraggeber am echten Laser getestet hat.
2. **Niemals ungefragt Befehle an echte Hardware senden.** Alle automatischen Tests laufen gegen den eingebauten Simulator (Abschnitt 6.9). Der Auftraggeber testet am Gerät.
3. **Code und Kommentare auf Englisch**, Oberflächentexte über das Qt-Übersetzungssystem (Deutsch und Englisch), README zweisprachig.
4. **Sicherheit vor Bequemlichkeit.** Der Laser kann Brände und Augenschäden verursachen. Jede Funktion, die den Laser einschaltet, muss bewusst ausgelöst werden.
5. Halte die Abhängigkeiten klein. Alles, was Qt mitbringt, wird mit Qt gemacht.
6. Nach jeder Etappe: Projekt baut ohne Warnungen auf Linux, Unit-Tests laufen grün.

---

## 1. Hintergrund und Ziel

Der Auftraggeber besitzt einen günstigen China-Lasergravierer. Die mitgelieferte Software „Engraver Master“ (V1.60.0.285, Build 2022) läuft nur unter Windows. Ziel ist ein eigenes, quelloffenes Programm, das

- unter **Linux und Windows** läuft (Hauptsystem des Auftraggebers: Linux Mint),
- den Funktionsumfang von Engraver Master, vor allem des Tabs „NC Sender“, abdeckt und erweitert,
- **G-Code selbst erzeugt** aus Pixelbildern (JPG, PNG usw.), Vektorgrafiken (SVG, DXF) und Text,
- **nicht nur für diesen einen Laser** taugt, sondern über Geräteprofile auch für andere GRBL-Laser (z. B. K40 mit GRBL-Board) nutzbar ist,
- später auf GitHub veröffentlicht wird, zweisprachig (Deutsch/Englisch).

### 1.1 Name und Veröffentlichung

- Programmname: **Laser-Dilettant**
- GitHub-Repository: `ohrenbeisser/Laser-Dilettant`
- Name der ausführbaren Datei: `laser-dilettant` (Linux), `Laser-Dilettant.exe` (Windows)
- Lizenz: **GPL-3.0-or-later** (zwingend wegen Potrace, siehe Abschnitt 3.3)

---

## 2. Zielhardware

### 2.1 Gerät des Auftraggebers

| Eigenschaft | Wert |
|---|---|
| Bezeichnung in Engraver Master | „DIY8000 1720/4050 Desktop Laser“ |
| Variante | 4050, Arbeitsfläche ca. 400 × 500 mm |
| Steuerung | Arduino Nano mit ATmega328P, USB-Seriell-Wandler CH340 |
| Firmware | meldet sich als „Grbl 1.1z“ (angepasste Hersteller-Fassung, offiziell gibt es nur bis 1.1h) |
| Baudrate | aktuell 9600, soll später auf 115200 umgestellt werden (Neu-Flashen, Abschnitt 9.4) |
| Laser | Diodenlaser, „10 W“ (vermutlich elektrische Leistung) |
| Endschalter | unbekannt, Homing im Profil standardmäßig **aus** |
| Air Assist | unbekannt, im Profil standardmäßig **aus** |

Wichtig:
- Das Programm muss mit **9600 und 115200 Baud** zuverlässig funktionieren.
- Ob die Hersteller-Firmware den GRBL-Lasermodus (`$32`) kennt, ist unbekannt. Beim Verbinden prüfen (Abschnitt 6.3).
- Der Arduino Nano macht beim Öffnen des Ports einen Reset (DTR). Das Programm muss das Begrüßungsbanner abwarten.
- Linux: Zugriff auf `/dev/ttyUSB*` erfordert die Gruppe `dialout`. Bei „Permission denied“ einen verständlichen Hinweis anzeigen (`sudo usermod -aG dialout $USER`, danach ab- und anmelden).
- Windows: Der CH340-Treiber muss installiert sein. Wird kein Port gefunden, Hinweis darauf anzeigen.

### 2.2 Andere Laser

- Version 1 unterstützt **GRBL 1.1** (inkl. Hersteller-Abwandlungen und grblHAL im GRBL-kompatiblen Modus). Das deckt die meisten Diodenlaser ab (Ortur, Sculpfun, Atomstack, Two Trees, DIY-Geräte).
- **K40:** Im Originalzustand hat der K40 meist ein M2-Nano-Board mit eigenem USB-Protokoll, kein GRBL. Version 1 unterstützt den K40 nur mit GRBL-Umrüstung. Ein Bekannter des Auftraggebers kann einen K40 testen.
- Die Architektur bekommt dafür eine **Treiberschnittstelle** (Abschnitt 5.2), damit später weitere Steuerungen ergänzt werden können (Marlin, Smoothieware, M2 Nano).

---

## 3. Technik

### 3.1 Sprache und Framework

- **C++20**
- **Qt 6**, Mindestversion 6.5, empfohlen 6.8 LTS
  - Module: Core, Gui, Widgets, SerialPort, Svg, Concurrent, LinguistTools, Test
  - Hinweis: Linux Mint 22 liefert über apt nur Qt 6.4. Zum Entwickeln Qt über `aqtinstall` oder den Qt Online Installer installieren. In der README beschreiben.
- Build-System: **CMake** (≥ 3.21)
- Widgets-basierte Oberfläche (kein QML), Stil „Fusion“ für einheitliches Aussehen auf beiden Systemen

### 3.2 Zielsysteme

- Linux: Linux Mint 21 und 22 sowie andere aktuelle Distributionen (Ubuntu ab 22.04, Debian 12, Fedora). Erreicht über ein **AppImage**, gebaut auf Ubuntu-22.04-Basis (glibc-Kompatibilität).
- Windows: Windows 10 und 11, 64 Bit.

### 3.3 Fremdbibliotheken

| Bibliothek | Zweck | Lizenz | Einbindung |
|---|---|---|---|
| Qt 6 | alles Grundlegende | LGPL-3.0 | System/aqt |
| Potrace | Bitmap vektorisieren (Modi Kontur, Füllung) | GPL-2.0-or-later | als Quellcode in `third_party/` |
| nanosvg | SVG-Pfade auslesen (QtSvg liefert keine Geometrie) | zlib | Header in `third_party/` |
| dxflib | DXF einlesen | GPL-2.0-or-later | als Quellcode in `third_party/` |
| Hershey-Schriften | Einzellinien-Schriften | freie Nutzung mit Herkunftsvermerk | Daten in `resources/fonts/hershey/` |
| avrdude | Firmware sichern und flashen | GPL-2.0 | externes Programm, unter Windows mitgeliefert, unter Linux Systempaket |
| GRBL 1.1h (Hex) | Firmware für 115200 Baud | GPL-3.0 | Hex-Datei in `resources/firmware/` |

Alle Lizenztexte in `LICENSES/` ablegen und im Dialog „Über“ nennen.

### 3.4 Verteilung

- Linux: AppImage (linuxdeploy mit Qt-Plugin)
- Windows: Installer (Inno Setup) **und** portable ZIP (windeployqt)
- GitHub Actions baut beide bei jedem Tag `v*` und hängt sie an das Release.

---

## 4. Bedienoberfläche

### 4.1 Grundaufbau des Hauptfensters

Keine Menüleiste. Das Fenster besteht aus fünf festen Bereichen:

```
┌──────────────────────────────────────────────────────────────────────┐
│ Verbindungsleiste: Port ▾  Baud ▾  [Verbinden]  ● Status   Profil ▾  [NOT-AUS] │
├───────────────────────────────────────────────┬──────────────────────┤
│                                               │ [Steuerung][Bild]    │
│                                               │ [Text][Einstellungen]│
│      Arbeitsfläche (Vorschau, mm-Raster)      │                      │
│      mit Motiv, Anfassern, Lupe               │   Inhalt des Tabs    │
│      Ansicht: Original | Bearbeitet | Pfade   │                      │
│                                               │                      │
├───────────────────────────────────────────────┤                      │
│ Auftragsleiste: Material ▾  Leistung %  Geschw. mm/s  Durchgänge      │
│ [Rahmen] [Start] [Pause] [Stopp]  ▓▓▓▓░░░░ 35 %  12:40 / 36:00        │
├───────────────────────────────────────────────┴──────────────────────┤
│ Statuszeile: Zustand  X 123,40  Y 87,15  F 1200  S 35 %  Ov 100/100  Grbl 1.1z │
└──────────────────────────────────────────────────────────────────────┘
```

- Arbeitsfläche links, Tab-Spalte rechts, getrennt durch einen `QSplitter`. Die Tab-Spalte ist **einklappbar**.
- Verbindungsleiste, Arbeitsfläche, Auftragsleiste und Statuszeile sind **immer sichtbar**, egal welcher Tab offen ist.
- **Not-Aus** ist immer sichtbar, rot, groß, rechts oben.
- Mindestgröße: **1280 × 720** (Laptop). Die Oberfläche folgt der Bildschirmskalierung (High-DPI).
- Design: hell, dunkel oder Systemeinstellung (Abschnitt 10).
- Fenstergröße, Splitter-Position und zuletzt aktiver Tab werden gespeichert.

### 4.2 Verbindungsleiste

- Auswahlliste der seriellen Ports (`QSerialPortInfo`), mit Beschreibung (z. B. „USB-SERIAL CH340“). Knopf zum Neu-Einlesen.
- Auswahlliste Baudrate: 9600, 19200, 38400, 57600, 115200, 230400. Vorbelegung aus dem Geräteprofil.
- Knopf Verbinden/Trennen.
- Statusanzeige als farbiges Feld: Getrennt (grau), Idle (grün), Run (blau), Hold (gelb), Jog (blau), Alarm (rot), Door (rot), Home (blau), Check (grau), Sleep (grau).
- Auswahlliste Geräteprofil.
- Knopf Not-Aus (Abschnitt 6.7).
- Ein Eintrag „Simulator“ in der Port-Liste verbindet mit dem eingebauten virtuellen Gerät (Abschnitt 6.9).

### 4.3 Arbeitsfläche

- `QGraphicsView`/`QGraphicsScene`, Einheit der Szene: Millimeter.
- Zeigt die Arbeitsfläche des aktiven Profils mit mm-Raster (Linien alle 10 mm, kräftiger alle 50 mm), Achsbeschriftung, Nullpunkt-Markierung.
- **Koordinatensystem:** Maschinen-Nullpunkt laut Profil (Standard: unten links, Y nach oben). Bildkoordinaten (Y nach unten) werden bei der Erzeugung umgerechnet.
- Zoom mit Mausrad (um den Mauszeiger), Verschieben mit mittlerer Maustaste oder Leertaste+Ziehen, Knopf „Alles zeigen“.
- **Ein Motiv pro Auftrag.** Das Motiv ist auswählbar, hat Anfasser zum Skalieren (Seitenverhältnis gesperrt, Umschalttaste hebt die Sperre auf) und lässt sich mit der Maus verschieben.
- Ansichtsumschalter oben rechts: **Original** (geladenes Bild), **Bearbeitet** (Ergebnis der Bildpipeline in Laser-Pixeln), **Pfade** (erzeugter G-Code: Laserbahnen rot, Leerfahrten dünn grau gestrichelt).
- **Lupe:** schaltbares Fenster mit 1:1-Ansicht (ein Bildpixel = ein Bildschirmpixel oder größer), damit sich Dithering-Muster beurteilen lassen.
- Während eines Auftrags: aktuelle Laserposition als Fadenkreuz, bereits gelaserte Bahnen farblich anders.
- Warnung (rote Umrandung und Hinweis), wenn das Motiv über die Arbeitsfläche hinausragt. Start ist dann gesperrt, bis der Nutzer bestätigt.
- Drag and Drop von Dateien auf die Arbeitsfläche lädt sie (Bild- und Vektorformate → Bild-Tab; `.nc`, `.gcode`, `.gc`, `.ngc`, `.tap` → als G-Code).

### 4.4 Auftragsleiste (zentral)

Die Laser-Werte stehen **zentral** hier, nicht in den einzelnen Tabs.

- **Material** (Auswahlliste der Materialvorlagen, Abschnitt 8.2). Auswahl setzt Leistung, Geschwindigkeit, Durchgänge.
- **Leistung** in **Prozent** (0–100, eine Nachkommastelle). Intern umgerechnet auf S-Werte: `S = Prozent / 100 × $30` (Maximalwert aus dem Profil bzw. Gerät).
- **Geschwindigkeit** in **mm/s**. Intern in G-Code als `F = mm/s × 60` (mm/min).
- **Durchgänge** (1–99): der gesamte Auftrag wird so oft wiederholt.
- Knöpfe: **Rahmen**, **Start**, **Pause/Fortsetzen**, **Stopp** (Abschnitt 6.6).
- Fortschrittsbalken (Prozent der bestätigten Zeilen), verstrichene Zeit / geschätzte Gesamtzeit.
- Bei geladenem fremdem G-Code sind Leistung, Geschwindigkeit und Material ausgegraut mit Hinweis „aus Datei“. Overrides (Tab Steuerung) bleiben nutzbar.
- Jede Änderung der Laser-Werte erzeugt den G-Code neu (verzögert um ca. 300 ms).

### 4.5 Statuszeile

Zustand, Position X/Y (Arbeitskoordinaten, zwei Nachkommastellen, Dezimalkomma je nach Sprache), aktueller Vorschub, aktuelle Leistung in %, Overrides (Vorschub/Leistung), Firmware-Version. Bei Alarm oder Fehler: Klartextmeldung.

### 4.6 Tastenkürzel

| Taste | Funktion |
|---|---|
| Esc | **Not-Aus** (immer aktiv, auch wenn ein Eingabefeld den Fokus hat) |
| Leertaste | Pause/Fortsetzen während eines Auftrags (nur wenn kein Eingabefeld den Fokus hat) |
| Pfeiltasten | Achsen verfahren (nur wenn Arbeitsfläche oder Tab Steuerung den Fokus hat) |
| Bild auf/ab | Schrittweite ändern |
| Strg+O | Datei laden (Bild, Vektor oder G-Code) |
| Strg+V | Bild aus Zwischenablage einfügen |
| Strg+S / Strg+Umschalt+S | Projekt speichern / speichern unter |
| Strg+E | G-Code exportieren |
| Strg+0 | Arbeitsfläche ganz zeigen |

---

## 5. Architektur

### 5.1 Verzeichnisstruktur

```
Laser-Dilettant/
├── CMakeLists.txt
├── LICENSE                       (GPL-3.0)
├── LICENSES/                     (Lizenzen der Fremdbibliotheken)
├── README.md / README.de.md
├── docs/                         (Spezifikation, Protokollnotizen)
├── resources/
│   ├── icons/
│   ├── fonts/hershey/
│   ├── firmware/grbl_v1.1h.hex
│   ├── profiles/                 (vordefinierte Geräteprofile, JSON)
│   └── grbl/                     (Fehler-, Alarm-, Einstellungstexte, CSV)
├── i18n/ (laser-dilettant_de.ts, laser-dilettant_en.ts)
├── third_party/ (potrace, nanosvg, dxflib)
├── src/
│   ├── app/          main.cpp, MainWindow, Application-Einstellungen
│   ├── ui/           ConnectionBar, WorkArea, JobBar, StatusLine, Loupe,
│   │                 tabs/ControlTab, ImageTab, TextTab, SettingsTab,
│   │                 dialogs/ (GrblSettingsDialog, FirmwareDialog, AboutDialog)
│   ├── core/         Motif, Job, Project (Laden/Speichern), Units,
│   │                 DeviceProfile, MaterialPreset, ImagePreset, PresetStore
│   ├── device/       IDeviceDriver, ITransport, SerialTransport,
│   │                 GrblDriver, GrblStatusParser, GrblStreamer,
│   │                 GrblSettings, GrblCodes, SimulatedGrbl
│   ├── imaging/      ImagePipeline, GrayscaleConversion, Levels,
│   │                 BrightnessContrast, Sharpen, Invert, Resampler,
│   │                 dither/ (Threshold, ErrorDiffusion, Bayer, Grayscale)
│   ├── vector/       PathSet (Polylinien in mm), SvgImporter, DxfImporter,
│   │                 Vectorizer (Potrace), HatchFill, PathOptimizer
│   ├── text/         TextRenderer (Systemschriften), HersheyFont
│   ├── gcode/        RasterGenerator, VectorGenerator, GcodeWriter,
│   │                 GcodeParser (für Vorschau), TimeEstimator, TestGridGenerator
│   └── firmware/     AvrdudeRunner
└── tests/            Unit-Tests (Qt Test)
```

### 5.2 Treiberschnittstelle

```cpp
class IDeviceDriver : public QObject {
    // Connection
    virtual void connectDevice(const ConnectionParams&) = 0;
    virtual void disconnectDevice() = 0;
    // Job
    virtual void startJob(const GcodeProgram&, int passes) = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;          // controlled stop, keeps position
    virtual void emergencyStop() = 0; // immediate
    // Manual control
    virtual void jog(double dx, double dy, double feedMmMin) = 0;
    virtual void sendCommand(const QString&) = 0;
    // signals: stateChanged, positionChanged, progressChanged,
    //          messageReceived, errorOccurred, alarmRaised, connected, disconnected
};
```

`GrblDriver` implementiert sie. `ITransport` abstrahiert die Byte-Verbindung, damit `SerialTransport` und `SimulatedGrbl` austauschbar sind.

### 5.3 Datenfluss

```
Quelle (Bild/SVG/DXF/Text/Testraster/G-Code-Datei)
  → Motiv (RasterMotif mit QImage oder VectorMotif mit PathSet oder GcodeMotif)
  → Bearbeitung (Bildpipeline bzw. Vektor-Operationen)
  → Generator (Raster oder Vektor) mit Laser-Werten aus der Auftragsleiste
  → GcodeProgram (Zeilenliste + Metadaten: Begrenzungsrahmen, Zeitschätzung)
  → Vorschau (Pfade) und Streamer (an das Gerät) oder Export (Datei)
```

Rechenintensive Schritte (Pipeline, Dithering, Vektorisierung, G-Code-Erzeugung) laufen in Hintergrund-Threads (`QtConcurrent`), die Oberfläche bleibt bedienbar. Neue Eingaben brechen laufende Berechnungen ab (Generationszähler).

---

## 6. Gerätesteuerung (GRBL)

### 6.1 Allgemeines

- Grundlage ist das offizielle GRBL-1.1-Protokoll (Repository `gnea/grbl`, Wiki „Grbl v1.1 Interface“, „Grbl v1.1 Commands“, „Grbl v1.1 Laser Mode“).
- Fehler-, Alarm- und Einstellungstexte aus den CSV-Dateien in `gnea/grbl/doc/csv/` übernehmen und ins Deutsche übersetzen (`resources/grbl/`).
- Zeilenende beim Senden: `\n`.
- Antworten: `ok`, `error:N`, `ALARM:N`, `<...>` (Status), `[MSG:...]`, `[GC:...]`, `[VER:...]`, `[OPT:...]`, `$N=Wert`, Banner `Grbl X.Y...`.

### 6.2 Verbindungsaufbau

1. Port öffnen (8N1, keine Flusssteuerung).
2. Auf Banner warten (Regex `^Grbl\s+(\S+)`), Zeitlimit 5 s. Kommt nichts, einmal Soft-Reset `0x18` senden und erneut 3 s warten. Danach Fehlermeldung „Kein GRBL-Gerät gefunden. Baudrate prüfen.“
3. Firmware-Version aus dem Banner merken (auch Abwandlungen wie „1.1z“ akzeptieren).
4. `$I` (Build-Info), `$$` (Einstellungen), `$G` (Parser-Zustand) abfragen.
5. Sicherheit: sofort `M5` senden (Laser aus).
6. Statusabfrage starten (Abschnitt 6.4).
7. Ist der Zustand `Alarm` (typisch nach Start mit aktiviertem Homing), Hinweis anzeigen mit Knöpfen „Entsperren ($X)“ und „Referenzfahrt ($H)“.

### 6.3 Prüfungen nach dem Verbinden

- `$32` (Lasermodus): Fehlt der Wert oder ist er 0, gelbe Warnung: „Lasermodus ist aus. Der Laser hält an Ecken kurz an und brennt dort stärker. In den Einstellungen aktivieren?“ (setzt `$32=1`).
- `$30` (maximaler S-Wert): mit Profil vergleichen. Bei Abweichung anbieten, den Gerätewert ins Profil zu übernehmen.
- `$130/$131` (Verfahrweg X/Y): mit Profil vergleichen, ebenso Übernahme anbieten.
- `$10` (Statusbericht): Positionsdarstellung entsprechend auswerten (MPos oder WPos; bei MPos mit WCO umrechnen).

### 6.4 Statusabfrage

- Echtzeitbefehl `?` (ohne Zeilenende) alle **200 ms** (einstellbar 100–1000 ms), auch während eines Auftrags.
- Statusbericht parsen: `<Zustand|MPos:x,y,z|FS:f,s|Ov:feed,rapid,spindle|WCO:x,y,z|Bf:...|Pn:...>`. Unbekannte Felder ignorieren.
- Statusberichte **nicht** in der Konsole anzeigen (Filter, abschaltbar).

### 6.5 Streaming (Senden eines Auftrags)

- Verfahren **Zeichenzählung** (character counting) nach GRBL-Wiki: Der Empfangspuffer der 328P-Firmware hat 128 Byte. Es dürfen höchstens 127 Byte unbestätigt unterwegs sein. Für jede gesendete Zeile die Länge (inkl. `\n`) merken; bei jedem `ok` oder `error` die älteste entfernen.
- Vor dem Senden jede Zeile normalisieren: Kommentare `( … )` und `; …` entfernen, Leerzeichen entfernen, Großbuchstaben, leere Zeilen überspringen. Zeilen über 80 Zeichen ablehnen (Fehler beim Laden).
- Bei `error:N` während eines Auftrags: Auftrag pausieren (Feed Hold), Meldung mit Zeilennummer, Zeileninhalt und Klartext. Nutzer wählt „Fortsetzen“ oder „Abbrechen“.
- Bei `ALARM:N`: Auftrag sofort als abgebrochen werten, Laser gilt als aus, Klartext anzeigen.
- Fortschritt = bestätigte Zeilen / Gesamtzeilen. Restzeit aus Zeitschätzung (Abschnitt 7.8) minus verstrichener Zeit, korrigiert über den Fortschritt.
- **Durchgänge:** Programm wird n-mal hintereinander gesendet, zwischen den Durchgängen kein Halt.
- Vor dem Start Bestätigungsdialog (abschaltbar in den Einstellungen): Motivgröße, Position, Leistung, Geschwindigkeit, Durchgänge, geschätzte Dauer, Hinweis „Schutzbrille tragen, Laser nicht unbeaufsichtigt lassen“.
- Während eines Auftrags sind Verfahren, Konsole (außer Echtzeitbefehle), Profilwechsel und Motivänderungen gesperrt.

### 6.6 Auftragssteuerung

| Knopf | Ablauf |
|---|---|
| Rahmen | Fährt den Begrenzungsrahmen des Motivs mit `G0` ab (Laser aus). Option in den Einstellungen: „Rahmen mit schwachem Laser“ (`M3 S<Fokusleistung>`, `G1`-Fahrten, danach `M5`). Danach zurück zur Startposition. |
| Start | Erzeugt bzw. nimmt das aktuelle Programm und startet das Streaming. |
| Pause | Echtzeitbefehl `!` (Feed Hold). Knopf wird zu „Fortsetzen“ → `~` (Cycle Start). Im Lasermodus ist der Laser während der Pause aus. |
| Stopp | Kontrollierter Abbruch: `!` senden, auf Zustand `Hold:0` warten (Bewegung steht), dann `0x18` (Soft-Reset), Sendepuffer leeren, danach `M5`. Die Position bleibt dabei erhalten. |

### 6.7 Not-Aus

- Knopf in der Verbindungsleiste und Taste Esc.
- Sendet **sofort** `0x18` (Soft-Reset), ohne vorher anzuhalten, leert den Sendepuffer und bricht den Auftrag ab. GRBL schaltet beim Reset den Laser ab.
- Folge: Position kann verloren gehen, GRBL meldet ggf. Alarm. Hinweis anzeigen: „Not-Aus ausgelöst. Position prüfen, ggf. Referenzfahrt oder Nullpunkt neu setzen.“
- Funktioniert auch, wenn gerade kein Auftrag läuft (z. B. bei eingeschaltetem Fokus-Laser).
- **Hinweis in README und Hilfe:** Der Software-Not-Aus ersetzt keinen Hardware-Not-Aus. Hängt der PC oder reißt die USB-Verbindung ab, wirkt er nicht. Empfehlung: Schalter, der die Stromversorgung des Lasermoduls trennt.

### 6.8 Overrides

- Echtzeitbefehle Vorschub: `0x90` = 100 %, `0x91` = +10 %, `0x92` = −10 %, `0x93` = +1 %, `0x94` = −1 %.
- Echtzeitbefehle Leistung (Spindel): `0x99` = 100 %, `0x9A` = +10 %, `0x9B` = −10 %, `0x9C` = +1 %, `0x9D` = −1 %.
- Bereich 10–200 %. Die Schieberegler im Tab Steuerung berechnen die Differenz zum aktuellen Wert (aus `Ov:` im Statusbericht) und senden die nötige Folge aus ±10- und ±1-Schritten.
- Knopf „100 %“ setzt zurück.

### 6.9 Simulator (virtuelles GRBL-Gerät)

- Implementiert `ITransport` und verhält sich wie GRBL 1.1:
  - Banner beim Verbinden (`Grbl 1.1h ['$' for help]`), Antworten auf `$$`, `$I`, `$G`, `$X`, `$H`, `$J=…`.
  - Empfangspuffer 128 Byte, `ok` nach Abarbeitung jeder Zeile, `error:N` für ungültige Befehle (mindestens für unbekannte `$`-Befehle und G-Code-Wörter).
  - Bewegungssimulation mit Vorschub und vereinfachter Beschleunigung, Statusberichte mit Position, Zustand, FS und Ov.
  - Echtzeitbefehle `?`, `!`, `~`, `0x18`, Overrides.
  - Einstellbar: simulierte Baudrate (künstliche Verzögerung), Fehlerinjektion für Tests.
- Wird für Unit-Tests und zum gefahrlosen Ausprobieren genutzt. In der Port-Liste als „Simulator“.

### 6.10 Konsole

- Im Tab Steuerung. Ausgabe gesendeter Zeilen (`> …`) und empfangener Antworten, Statusberichte gefiltert.
- Eingabezeile mit Verlauf (Pfeil hoch/runter), Enter sendet.
- Knöpfe: Leeren, Protokoll speichern.
- Optional (Einstellungen): Kommunikationsprotokoll in Datei schreiben.

---

## 7. G-Code-Erzeugung

### 7.1 Allgemeine Regeln

- Einheit mm (`G21`), absolute Koordinaten (`G90`).
- Laser: **M4** (dynamischer Lasermodus) als Standard für Diodenlaser, wählbar M3/M4 im Profil.
- Zahlen: höchstens 3 Nachkommastellen, nachfolgende Nullen weglassen (`X12.5` statt `X12.500`), Dezimalpunkt immer `.`.
- Modale Wörter nicht wiederholen (`G1` nur bei Wechsel, `F` nur bei Änderung, `S` nur bei Änderung). Das spart Bandbreite, wichtig bei 9600 Baud.
- Kopf:
  ```
  ; Laser-Dilettant <Version>, <Datum>
  ; Motiv: <Name>, <Breite> x <Höhe> mm, Position <X>,<Y>
  ; Leistung <P> %, Geschwindigkeit <V> mm/s, Durchgänge <n>
  G21 G90
  M8            (nur wenn Air Assist im Profil aktiv)
  M4 S0
  ```
- Fuß:
  ```
  M5
  M9            (nur wenn Air Assist)
  G0 X<Startpunkt> Y<Startpunkt>   (Verhalten einstellbar: Nullpunkt, Startposition, stehen bleiben)
  ```
- Leerfahrten mit `G0`, Laserfahrten mit `G1 F<Geschwindigkeit>`.

### 7.2 Raster (Modus „Raster“, entspricht „Line“ in Engraver Master)

- Eingabe: Bild in Laser-Pixeln (nach Pipeline), Pixelgröße = Linienabstand × Linienabstand.
- Zeilenweise abfahren, Scanrichtung **horizontal** oder **vertikal** (vertikal = Bild intern transponieren).
- **Beidseitig scannen** (Standard an): gerade Zeilen hin, ungerade zurück.
- Pro Zeile Läufe gleichen Werts zusammenfassen und als ein `G1 X… S…` ausgeben.
- **Weiße Bereiche überspringen** (Standard an): Zeilen ohne schwarze Pixel komplett auslassen. Weiße Läufe innerhalb einer Zeile, die länger als 5 mm sind (einstellbar), mit `G0` überspringen, kürzere mit `G1 S0` durchfahren.
- **Overscan** (Standard 0 mm, einstellbar 0–20 mm): jede Zeile beidseitig um diese Strecke mit `S0` verlängern, damit die Achse auf Geschwindigkeit ist.
- Pixelposition: Mitte des Pixels in Zeilenrichtung beginnt bei Pixelkante (der Lauf von Pixel i bis j geht von `x0 + i·d` bis `x0 + (j+1)·d`), Zeile y liegt bei `y0 + (Zeilen − 1 − row)·d + d/2`.
- Bei Graustufen: S je Lauf aus Grauwert (Abschnitt 7.6.5).

### 7.3 Füllung (Modus „Füllung“, entspricht „Full Mode“)

- Pixelbild: Schwellwert → Potrace → geschlossene Pfade → Schraffur.
- Vektorquelle (SVG, DXF, Text): direkt die geschlossenen Pfade schraffieren.
- Schraffur: Scanlinien im eingestellten **Linienabstand** und **Winkel** (Standard 0°), Schnittpunkte mit den Polygonen nach **Even-Odd-Regel**, Abschnitte als `G1`.
- Option **Kreuzschraffur**: zweiter Durchgang um 90° gedreht.
- Option **Umriss zusätzlich**: nach der Füllung die Konturen einmal abfahren (saubere Kanten).
- Beidseitig fahren, Leerfahrten zwischen Abschnitten mit `G0` (kurze Abstände < 1 mm auf derselben Linie mit `G1 S0`).

### 7.4 Kontur (Modus „Kontur“, entspricht „Cutting Mode“)

- Pixelbild: Schwellwert → Potrace → Pfade.
- Vektorquelle: Pfade direkt.
- Kurven (Bézier, Bögen) in Polylinien zerlegen, Toleranz 0,02 mm.
- Pfade sortieren, um Leerfahrten zu minimieren (Nächster-Nachbar-Heuristik, Startpunkt geschlossener Pfade frei wählbar).
- Ausgabe als `G1`-Polylinien (keine `G2/G3` in Version 1).

### 7.5 Bildpipeline

Reihenfolge fest, jeder Schritt auf dem Ergebnis des vorigen:

1. **Laden:** `QImageReader` mit `setAutoTransform(true)` (EXIF-Ausrichtung). Alpha-Kanal: transparent wird **weiß** (auf weißem Hintergrund zusammensetzen).
2. **Zuschneiden** (rechteckig, optional, Abschnitt 4.7 Bild-Tab).
3. **Drehen/Spiegeln:** 90°-Schritte, horizontal/vertikal spiegeln.
4. **Umrechnen auf Laser-Auflösung:** Zielgröße in Pixeln = Motivgröße in mm ÷ Linienabstand (gerundet). Verkleinern mit Flächenmittelung (hochwertig), Vergrößern bilinear. Warnung, wenn um mehr als Faktor 2 vergrößert wird („Bild hat zu wenig Auflösung für diese Größe“).
5. **Graustufen-Umwandlung:** Luminanz (Rec. 709: 0,2126 R + 0,7152 G + 0,0722 B) als Standard; alternativ nur Rot-, Grün- oder Blaukanal; alternativ Durchschnitt.
6. **Tonwerte:** Schwarzpunkt (0–254), Weißpunkt (1–255, > Schwarzpunkt), Gamma (0,1–5,0, Standard 1,0).
7. **Helligkeit** (−100 bis +100) und **Kontrast** (−100 bis +100).
8. **Schärfen:** Unscharf maskieren, Stärke 0–300 %, Radius 0,5–5 px, Standard aus.
9. **Invertieren** (an/aus).
10. **Umsetzung in Laser-Pixel** (Abschnitt 7.6) bzw. Schwellwert für Füllung/Kontur.

Intern mit 8-Bit-Graustufen (`QImage::Format_Grayscale8`) arbeiten, bei Fehlerstreuung mit Fließkomma-Puffer.

**Live-Vorschau:** Während ein Regler gezogen wird, rechnet die Pipeline auf einer verkleinerten Version (längste Kante max. 800 px). Beim Loslassen in voller Auflösung.

### 7.6 Umsetzung in Laser-Pixel (Modus Raster)

#### 7.6.1 Schwellwert
- Schwellwert 0–255 (Standard 128): Pixel < Schwellwert = Laser an.
- Knopf **Automatisch** (Otsu-Verfahren) setzt den Schwellwert.

#### 7.6.2 Fehlerstreuung (Dithering)
Algorithmen (Fehler = alter Wert − neuer Wert, verteilt auf Nachbarn; `*` = aktuelles Pixel, Gewichte geteilt durch den Divisor):

- **Floyd-Steinberg** (÷16):
  ```
        *   7
    3   5   1
  ```
- **Jarvis-Judice-Ninke** (÷48):
  ```
            *   7   5
    3   5   7   5   3
    1   3   5   3   1
  ```
- **Stucki** (÷42):
  ```
            *   8   4
    2   4   8   4   2
    1   2   4   2   1
  ```
- **Atkinson** (÷8, verteilt nur 6/8 des Fehlers):
  ```
        *   1   1
    1   1   1
        1
  ```
- **Sierra (Sierra-3)** (÷32):
  ```
            *   5   3
    2   4   5   4   2
        2   3   2
  ```

Einstellungen:
- **Stärke** 0–100 % (Standard 100 %): weitergegebener Fehler wird mit diesem Faktor multipliziert.
- **Schlangenlinien-Abtastung** (Standard an): Zeilenrichtung abwechselnd, Kern dabei gespiegelt.
- **Schwellwert** für die Entscheidung (Standard 128).

#### 7.6.3 Geordnetes Raster (Bayer)
- Matrix 2×2, 4×4 oder 8×8 (Standard 4×4), Schwelle je Position `(M(i,j) + 0,5) / n² × 255`.

#### 7.6.4 Halbton
- **Nicht in Version 1** (Backlog).

#### 7.6.5 Graustufen
- Kein Dithering, sondern Leistung je Pixel: `S = Smin + (1 − Grau/255) × (Smax − Smin)`.
- **Leistung min** in % (im Bild-Tab, nur bei diesem Verfahren sichtbar), **Leistung max** = Leistung aus der Auftragsleiste.
- **Stufen** 2–256 (Standard 32): Grauwerte vorher quantisieren, damit gleiche Werte zu längeren Läufen zusammenfallen.
- Reines Weiß (255) = Laser aus.

### 7.7 Vektor-Import

- **SVG** über nanosvg: alle Pfade, Formen, Transformationen; Einheiten (px, mm, cm, in, pt) aus `width/height/viewBox` korrekt umrechnen (96 px = 1 Zoll, wie Inkscape). Gefüllte und nur umrandete Formen werden gleich behandelt (Geometrie zählt, Farben werden ignoriert). Text in SVG wird ignoriert mit Hinweis „Text in Pfade umwandeln (Inkscape: Pfad → Objekt in Pfad)“.
- **DXF** über dxflib: LINE, LWPOLYLINE, POLYLINE, CIRCLE, ARC, ELLIPSE, SPLINE (als Polylinie), Einheit aus `$INSUNITS`, sonst mm. Blöcke (INSERT) auflösen.
- Nach dem Import: Begrenzungsrahmen bestimmen, auf Ursprung verschieben, Größe wie gelesen (Maßstab 1:1).

### 7.8 Zeitschätzung

- Aus dem fertigen Programm: jede Bewegung mit Trapezprofil aus `$110/$111` (max. Geschwindigkeit) und `$120/$121` (Beschleunigung), vereinfacht ohne Vorausschau über Segmentgrenzen. Bei fehlenden Werten Profilwerte.
- Anzeige im Format h:mm:ss, mal Durchgänge.

### 7.9 Export

- G-Code-Export (Strg+E) schreibt das aktuelle Programm mit Kopfkommentar als `.nc` (Dateidialog bietet `.nc`, `.gcode`).

---

## 8. Vorlagen

### 8.1 Geräteprofile

Gespeichert als JSON im Benutzerverzeichnis (`QStandardPaths::AppDataLocation/profiles/`). Vordefinierte Profile aus `resources/profiles/` werden beim ersten Start kopiert.

Felder:

| Feld | Bedeutung | Beispiel |
|---|---|---|
| name | Anzeigename | „DIY8000 4050 (Diode)“ |
| driver | Treiber | `grbl` |
| baudRate | Baudrate | 9600 |
| workAreaX / workAreaY | Arbeitsfläche in mm | 400 / 500 |
| origin | Nullpunkt-Ecke | `bottomLeft` (alternativ `topLeft`, `topRight`, `bottomRight`) |
| laserType | `diode` oder `co2` | `diode` |
| laserMode | `M4` oder `M3` | `M4` |
| maxS | entspricht `$30` | 1000 |
| homing | Referenzfahrt vorhanden | false |
| airAssist | M8/M9 ausgeben | false |
| focusPower | Leistung Fokus-Laser in % | 1,0 |
| maxSpeed | mm/s, Obergrenze für die Eingabe | 100 |
| jogSpeed | mm/s Standard für Verfahren | 50 |

Vordefinierte Profile:
1. **DIY8000 4050 (Diode)**: 400 × 500 mm, 9600 Baud, M4, maxS 1000 (beim Verbinden mit `$30` abgleichen)
2. **DIY8000 1720 (Diode)**: 170 × 200 mm, sonst wie oben
3. **GRBL-Diodenlaser (allgemein)**: 300 × 300 mm, 115200 Baud, M4, maxS 1000
4. **K40 mit GRBL-Board (CO₂)**: 300 × 200 mm, 115200 Baud, M4, maxS 1000, laserType `co2`

Im Tab Einstellungen: Profil auswählen, neu, kopieren, umbenennen, löschen (vordefinierte nur kopierbar), **„Werte aus Gerät übernehmen“** (setzt Arbeitsfläche aus `$130/$131`, maxS aus `$30`, homing aus `$22`).

### 8.2 Materialvorlagen

- Felder: Name, Leistung %, Geschwindigkeit mm/s, Durchgänge, Notiz, optional zugehöriges Profil.
- Auswahl in der Auftragsleiste. Knopf daneben „Aktuelle Werte als Vorlage speichern“.
- Verwaltung (umbenennen, löschen) im Tab Einstellungen.
- Gespeichert in `materials.json`.

### 8.3 Bildvorlagen

- Speichern alle Einstellungen des Bild-Tabs außer Datei, Größe und Position (Graustufen-Kanal, Tonwerte, Helligkeit, Kontrast, Schärfen, Invertieren, Modus, Verfahren und dessen Parameter, Linienabstand, Scanrichtung, Beidseitig, Overscan, Weiß überspringen, Füllungs- und Kontur-Parameter).
- Auswahl, Speichern, Löschen im Bild-Tab oben.
- Gespeichert in `image_presets.json`. Getrennt von den Materialvorlagen.

---

## 9. Die vier Tabs im Detail

### 9.1 Tab Steuerung

**Achsen verfahren**
- Acht Richtungsknöpfe (Raute mit Diagonalen), Mitte = „Zum Nullpunkt“ (`G0 X0 Y0` in Arbeitskoordinaten).
- Schrittweite: 0,1 / 1 / 10 / 50 mm (Umschaltknöpfe).
- Verfahrgeschwindigkeit in mm/s (Vorbelegung aus Profil).
- Umsetzung mit Jog-Befehlen: `$J=G91 G21 X<dx> Y<dy> F<mm/min>`. Abbruch mit `0x85` (Jog Cancel).
- Pfeiltasten wie Knöpfe (Abschnitt 4.6).

**Position**
- **Nullpunkt hier setzen:** `G10 L20 P1 X0 Y0` (aktuelle Position wird Arbeitskoordinaten-Nullpunkt).
- **Nullpunkt zurücksetzen:** `G10 L2 P1 X0 Y0` (Arbeitskoordinaten = Maschinenkoordinaten).
- **Referenzfahrt** `$H` (nur sichtbar, wenn Profil `homing = true`).
- **Entsperren** `$X`.
- **Reset** (Soft-Reset `0x18`).

**Fokus-Laser**
- Umschaltknopf „Fokus-Laser an/aus“ mit Leistungsregler in % (Vorbelegung aus Profil, max. 5 %, darüber Bestätigungsabfrage).
- Umsetzung: `M3 S<Wert>` gefolgt von `G1 F100` ohne Weg (bzw. Weg null). **Achtung:** Im GRBL-Lasermodus (`$32=1`) feuert der Laser ggf. nur während einer `G1`-Bewegung. Am Gerät prüfen lassen. Falls der Laser im Stand nicht leuchtet: Fokus-Laser schaltet vorübergehend `$32=0`, merkt sich den alten Wert und stellt ihn beim Ausschalten (und beim Trennen, Not-Aus, Programmende) zuverlässig wieder her.
- Automatisches Abschalten nach 60 s (einstellbar), Laser aus beim Trennen, bei Not-Aus und beim Schließen des Programms.

**Overrides** (nur während eines Auftrags aktiv)
- Schieberegler Vorschub 10–200 %, Schieberegler Leistung 10–200 %, je mit Knopf „100 %“. Umsetzung siehe Abschnitt 6.8.

**G-Code-Datei**
- Knöpfe „G-Code laden“ und „G-Code exportieren“.
- Geladener G-Code wird zum Motiv (Typ G-Code), in der Vorschau dargestellt (Parser für G0, G1, G2, G3, G90/G91, G20/G21, M3/M4/M5, S, F), verschiebbar (Versatz wird beim Senden auf alle X/Y addiert, nur bei absoluten Koordinaten; bei G91-Abschnitten Warnung), nicht skalierbar.

**Konsole** (Abschnitt 6.10)

### 9.2 Tab Bild

Gilt für Pixelbilder (JPG, JPEG, PNG, BMP, GIF, TIFF, WebP) und Vektoren (SVG, DXF). Bei Vektoren werden nicht passende Bereiche ausgeblendet.

**Kopf**
- Knopf „Laden…“, Hinweis „oder Datei auf die Arbeitsfläche ziehen / Strg+V“.
- Bildvorlage: Auswahlliste, Speichern, Löschen.
- Dateiname und Originalgröße in Pixeln, effektive Auflösung (z. B. „1000 × 750 Laser-Pixel, entspricht 254 dpi“).

**Größe und Position**
- Breite, Höhe in mm; Schloss-Symbol für Seitenverhältnis (Standard gesperrt); Feld Prozent.
- **Startgröße beim Import:** Auflösung aus der Datei (DPI-Angabe, bei SVG/DXF die Dokumentgröße). Fehlt sie, 100 mm Breite. Ist das Motiv größer als die Arbeitsfläche, auf die Arbeitsfläche einpassen.
- Position X, Y in mm mit Bezugspunkt-Auswahl (3 × 3 Raster: Ecken, Kantenmitten, Mitte).
- Knopf „Auf Arbeitsfläche zentrieren“.
- Drehen 90° links / rechts, horizontal spiegeln, vertikal spiegeln.
- **Zuschneiden** (nur Pixelbilder): Knopf startet Zuschneidemodus auf der Arbeitsfläche (Rechteck aufziehen, Anfasser, Bestätigen/Abbrechen). Knopf „Zuschnitt zurücksetzen“.

**Modus**
- Raster (nur Pixelbilder), Füllung, Kontur. Standard bei Pixelbildern: Raster; bei Vektoren: Kontur.

**Bildkorrektur** (nur Pixelbilder, aufklappbar)
- Graustufen-Kanal: Luminanz, Rot, Grün, Blau, Durchschnitt
- Schwarzpunkt, Weißpunkt, Gamma
- Helligkeit, Kontrast
- Schärfen: an/aus, Stärke, Radius
- Invertieren
- Jeder Regler mit Zurücksetzen-Knopf, dazu „Alle Korrekturen zurücksetzen“.
- Kleines Histogramm über den Tonwertreglern.

**Raster-Einstellungen** (nur Modus Raster)
- Verfahren: Schwellwert, Fehlerstreuung, Bayer, Graustufen
- Parameter je Verfahren (Abschnitt 7.6), nur die zum Verfahren passenden sichtbar
- Linienabstand in mm (Standard **0,1 mm**, Bereich 0,02–1,0), Anzeige in Linien/mm und dpi
- Scanrichtung horizontal/vertikal
- Beidseitig scannen
- Weiße Bereiche überspringen
- Overscan in mm

**Füllungs-Einstellungen** (nur Modus Füllung)
- Schwellwert (nur Pixelbilder) mit Automatik
- Linienabstand, Winkel, Kreuzschraffur, Umriss zusätzlich
- Glättung (Potrace `alphamax`, 0–1,33, Standard 1,0), Kleine Flecken entfernen (Potrace `turdsize`, 0–100 px, Standard 2)

**Kontur-Einstellungen** (nur Modus Kontur)
- Schwellwert (nur Pixelbilder) mit Automatik
- Glättung, Kleine Flecken entfernen (wie oben)

Alle Änderungen aktualisieren die Vorschau live (Abschnitt 7.5).

### 9.3 Tab Text

Version 1, ohne Text auf Bogen, Rahmen oder QR-Code.

- Mehrzeiliges Textfeld.
- **Schriftart:** Systemschriften (`QFontDatabase`) und Einzellinien-Schriften (Hershey, in der Liste unter eigener Überschrift).
- Stil fett/kursiv (nur Systemschriften, soweit verfügbar).
- **Zeichenhöhe in mm** = Versalhöhe (Höhe des „H“), damit die Angabe dem entspricht, was man auf dem Werkstück misst.
- Zeilenabstand als Faktor (Standard 1,2) und Zeichenabstand (Standard 0).
- Ausrichtung links, zentriert, rechts.
- **Modus:** Kontur oder Füllung (Füllung nur bei Systemschriften; Einzellinien-Schriften immer als Kontur). Füllungs-Parameter wie im Bild-Tab (Linienabstand, Winkel, Kreuzschraffur, Umriss zusätzlich).
- Knopf „Erzeugen“ (zusätzlich automatische Aktualisierung bei Änderungen, verzögert).
- Umsetzung Systemschriften: `QPainterPath::addText` → in Polylinien zerlegen (Toleranz 0,02 mm) → Vektor-Pipeline.
- Größe und Position wie im Bild-Tab (Breite/Höhe ergeben sich aus dem Text, Skalieren über Anfasser ändert die Zeichenhöhe).
- Ein erzeugter Text **ersetzt** das aktuelle Motiv (ein Motiv pro Auftrag). Hat das aktuelle Motiv ungespeicherte Änderungen, vorher fragen.

### 9.4 Tab Einstellungen

Gegliedert in aufklappbare Abschnitte:

**Geräteprofil** (Abschnitt 8.1)

**GRBL-Parameter**
- Tabelle aller `$`-Werte des verbundenen Geräts: Nummer, Wert, Einheit, deutsche Erklärung (aus `resources/grbl/`). Mindestens erklärt: $0, $1, $2, $3, $4, $5, $6, $10, $11, $12, $13, $20, $21, $22, $23, $24, $25, $26, $27, $30, $31, $32, $100, $101, $102, $110, $111, $112, $120, $121, $122, $130, $131, $132. Unbekannte Nummern (Hersteller-Erweiterungen) mit Hinweis „unbekannt“ anzeigen.
- Werte bearbeiten, „Geänderte Werte schreiben“ sendet `$N=Wert` einzeln mit Prüfung auf `ok`.
- „In Datei sichern“ (Textdatei mit allen `$N=Wert`), „Aus Datei wiederherstellen“ (nur Nummern, die das Gerät kennt; vorher Vergleichsansicht alt/neu).
- Nur bei Zustand Idle bearbeitbar.

**Firmware**
- Anzeige: Version aus Banner, `$I`.
- **Firmware sichern:** avrdude liest den Flash aus: `avrdude -c arduino -p m328p -P <port> -b <boot-baud> -U flash:r:<datei>.hex:i`. Boot-Baudrate: 57600 (alter Nano-Bootloader) oder 115200 (neuer Bootloader), Auswahl mit Hinweis „Wenn es nicht klappt, die andere probieren“. Port vorher im Programm schließen.
- **GRBL flashen:** mitgelieferte `grbl_v1.1h.hex` (115200 Baud) oder eigene Hex-Datei: `avrdude -c arduino -p m328p -P <port> -b <boot-baud> -D -U flash:w:<datei>.hex:i`.
- Ablauf geführt (Assistent):
  1. Hinweis und Bestätigung („Die Hersteller-Firmware wird überschrieben.“)
  2. `$$` sichern (Pflicht)
  3. Firmware sichern (dringend empfohlen, überspringbar nur mit zweiter Bestätigung)
  4. Flashen, Ausgabe von avrdude live anzeigen
  5. Neu verbinden mit 115200 Baud, Profil-Baudrate anpassen
  6. Gesicherte `$$`-Werte wiederherstellen (Vergleichsansicht), danach `$32=1` prüfen
- Wiederherstellen der gesicherten Hersteller-Firmware über denselben Weg mit der Sicherungsdatei.
- avrdude-Pfad einstellbar, automatische Suche (Windows: mitgeliefert neben der .exe; Linux: `PATH`, Hinweis `sudo apt install avrdude`).

**Material testen** (Testraster)
- Einstellungen: Leistung von–bis in %, Geschwindigkeit von–bis in mm/s, Anzahl Stufen je Achse (2–10, Standard 5 × 5), Feldgröße in mm (Standard 5 mm), Abstand zwischen Feldern (Standard 2 mm), Füllart der Felder (Schraffur mit Linienabstand aus dem Bild-Tab oder 0,1 mm), Beschriftung an/aus (Einzellinien-Schrift, Leistung als Zeilen-, Geschwindigkeit als Spaltenbeschriftung, feste niedrige Leistung/hohe Geschwindigkeit, einstellbar).
- Knopf „Testraster erzeugen“: legt das Raster als Motiv auf die Arbeitsfläche (ersetzt aktuelles Motiv). Jedes Feld hat eigene Leistung und Geschwindigkeit; die Werte der Auftragsleiste gelten hier nicht (Hinweis anzeigen).
- Nach dem Lasern: Nutzer wählt das beste Feld per Klick in der Vorschau (oder Zeile/Spalte) → „Als Materialvorlage speichern“ mit Namensfeld.

**Materialvorlagen verwalten** (Liste, umbenennen, löschen, Notiz bearbeiten)

**Programm**
- Sprache: Deutsch, Englisch, Systemsprache (Neustart nötig, Hinweis)
- Design: hell, dunkel, wie System
- Bestätigung vor Start an/aus (Standard an)
- Nach Auftragsende: zum Nullpunkt, zur Startposition, stehen bleiben
- Rahmen mit schwachem Laser an/aus
- Statusabfrage-Intervall in ms
- Fokus-Laser Abschaltzeit in s
- Kommunikationsprotokoll in Datei schreiben an/aus
- Leerfahrt-Schwelle für weiße Bereiche in mm

---

## 10. Projektdateien und Einstellungen

- **Projektdatei** `.ldproj` (JSON, UTF-8):
  - Version des Formats
  - Motiv: Typ (`raster`, `vector`, `text`, `gcode`, `testgrid`), Quelldaten eingebettet (Pixelbild als PNG in Base64, Vektor als SVG-Text bzw. Pfadliste, Text mit allen Text-Einstellungen, G-Code als Text, Testraster-Parameter), Originaldateiname
  - Größe, Position, Drehung, Spiegelung, Zuschnitt
  - alle Bild-Tab-Einstellungen
  - Laser-Werte der Auftragsleiste, Name der Materialvorlage
  - Name des Geräteprofils
- Speichern Strg+S, Speichern unter, Laden über Strg+O (erkennt `.ldproj`) oder Drag and Drop.
- Ungespeicherte Änderungen: Sternchen im Fenstertitel, Abfrage beim Schließen.
- Zuletzt geöffnete Projekte (5) als Liste im Laden-Dialog bzw. Kopf des Bild-Tabs.
- Programmeinstellungen: `QSettings`. Profile, Material- und Bildvorlagen: JSON-Dateien in `QStandardPaths::AppDataLocation`.

---

## 11. Sicherheit (Zusammenfassung)

- Nach dem Verbinden immer `M5`.
- Laser nur durch bewusste Aktion: Start (mit Bestätigung), Fokus-Laser (mit Zeitlimit), Rahmen mit Laser (nur wenn in Einstellungen aktiviert).
- Not-Aus immer sichtbar, Esc immer aktiv.
- Fokus-Leistung über 5 % nur nach Bestätigung.
- Beim Schließen des Programms, Trennen oder Verbindungsabbruch: Laser aus (soweit möglich), laufenden Auftrag als abgebrochen markieren.
- Verbindungsabbruch während eines Auftrags erkennen (Port-Fehler oder 3 s keine Antwort auf `?`) und deutlich melden.
- Hinweistexte in README und im Bestätigungsdialog: Schutzbrille passend zur Wellenlänge, Laser nie unbeaufsichtigt, Brandschutz, Absaugung (Rauch), Hardware-Not-Aus empfohlen.
- `$32`-Änderungen durch den Fokus-Laser werden zuverlässig zurückgesetzt (Abschnitt 9.1).

---

## 12. Mehrsprachigkeit

- Alle Oberflächentexte über `tr()`. Übersetzungsdateien `i18n/laser-dilettant_de.ts` und `_en.ts`, mit `qt_add_translations` eingebunden.
- Ausgangssprache im Code: Englisch. Deutsche Übersetzung vollständig pflegen.
- Zahlen mit `QLocale` formatieren (Dezimalkomma im Deutschen). Eingabefelder akzeptieren Komma und Punkt.
- GRBL-Fehler-, Alarm- und Einstellungstexte in beiden Sprachen.
- README.md (Englisch) und README.de.md (Deutsch).

---

## 13. Tests

- **Unit-Tests (Qt Test)**:
  - `GrblStatusParser`: alle Zustände, MPos/WPos/WCO, fehlende Felder, Abwandlungen
  - `GrblStreamer`: Zeichenzählung gegen Simulator, korrekte Pufferauslastung, Fehler mitten im Auftrag, Pause/Fortsetzen, Stopp, Not-Aus, 9600 und 115200 Baud
  - Normalisierung von G-Code-Zeilen
  - Dithering-Algorithmen: Referenzbilder (kleine Testbilder, erwartetes Ergebnis als Datei), Stärke 0 % = reiner Schwellwert
  - Bildpipeline: Tonwerte, Gamma, Invertieren, Umrechnung auf Laser-Auflösung
  - RasterGenerator: Lauf-Zusammenfassung, beidseitig, Weiß überspringen, Overscan, Koordinaten exakt
  - HatchFill: Rechteck, Ring (Even-Odd), Winkel
  - SVG-/DXF-Import: Einheitenumrechnung, Maßstab
  - GcodeWriter: modale Wörter, Zahlenformat
  - TimeEstimator: einfache Bewegungen gegen Handrechnung
  - Projektdatei: Speichern und Laden ergibt gleiches Motiv
- Tests laufen in GitHub Actions auf Linux und Windows.

---

## 14. Etappen der Umsetzung

Nach jeder Etappe: anhalten, Ergebnis zusammenfassen, auf Test des Auftraggebers am Laser warten.

### Etappe 1: Grundgerüst, Verbindung, Steuerung, Einstellungen
- CMake-Projekt, Verzeichnisstruktur, Lizenzdateien, README-Gerüst, GitHub-Actions-Build (nur Kompilieren und Tests)
- Hauptfenster mit allen festen Bereichen (Tabs Bild und Text zunächst leer mit Hinweis)
- Simulator
- Verbindungsaufbau, Statusabfrage, Statuszeile
- Tab Steuerung: Verfahren, Nullpunkt, Entsperren, Reset, Fokus-Laser, Konsole
- Not-Aus
- Tab Einstellungen: Geräteprofile, GRBL-Parameter (lesen, schreiben, sichern, wiederherstellen), Firmware (sichern, flashen, Assistent), Programm-Einstellungen
- Mehrsprachigkeit eingerichtet
- **Abnahme:** Verbindung zum echten Laser mit 9600 Baud, `$$` wird angezeigt und gesichert, Verfahren funktioniert, Fokus-Laser leuchtet, Not-Aus wirkt. Danach Firmware-Sicherung und Umstellung auf 115200 durch den Auftraggeber.

### Etappe 2: NC-Sender
- G-Code laden, Parser, Vorschau auf der Arbeitsfläche, Verschieben des Motivs
- Streaming mit Zeichenzählung, Fortschritt, Zeitschätzung
- Auftragsleiste: Rahmen, Start (mit Bestätigung), Pause, Stopp, Durchgänge
- Overrides
- G-Code-Export
- **Abnahme:** Eine mit Engraver Master erzeugte NC-Datei wird vollständig und korrekt gelasert.

### Etappe 3: Bild (Raster)
- Laden (Knopf, Drag and Drop, Zwischenablage), EXIF, Transparenz
- Größe, Position, Bezugspunkt, Zentrieren, Drehen 90°, Spiegeln, Zuschneiden
- Bildpipeline mit Live-Vorschau, Lupe, Histogramm
- Verfahren Schwellwert, Fehlerstreuung (alle fünf), Bayer, Graustufen
- RasterGenerator
- Bildvorlagen, Materialvorlagen in der Auftragsleiste
- Projektdatei speichern/laden
- **Abnahme:** Foto auf Holz in brauchbarer Qualität, Vergleich mit Engraver Master „Line“.

### Etappe 4: Füllung und Kontur aus Pixelbildern
- Potrace-Anbindung, HatchFill, PathOptimizer, VectorGenerator
- **Abnahme:** Logo als Füllung und als Kontur, Vergleich mit Engraver Master „Full Mode“ und „Cutting Mode“.

### Etappe 5: Text
- Systemschriften und Hershey-Schriften, alle Text-Einstellungen
- **Abnahme:** Mehrzeiliger Text als Kontur und Füllung, Einzellinien-Schrift.

### Etappe 6: SVG und DXF
- Import, Einheiten, Kontur und Füllung
- **Abnahme:** In Inkscape erstellte SVG und eine DXF aus einem CAD-Programm werden maßhaltig gelasert.

### Etappe 7: Testraster und Feinschliff
- Testraster, Auswahl des besten Feldes, Speichern als Materialvorlage
- Design hell/dunkel, Tastenkürzel vollständig, Übersetzungen vollständig
- Pakete: AppImage, Windows-Installer, portable ZIP, Release-Workflow
- README zweisprachig mit Screenshots, Sicherheitshinweisen, Installationsanleitung (dialout, CH340-Treiber)
- **Abnahme:** Installation aus den Paketen auf Linux Mint und Windows, kompletter Ablauf ohne Entwicklungsumgebung.

---

## 15. Nicht in Version 1 (Backlog)

- Text auf Bogen, Rahmen um Text, QR-Code-Erzeugung
- Freies Drehen (beliebiger Winkel)
- Gradationskurve (wie in GIMP)
- Halbton (Punktraster) mit Rasterweite, Winkel, Punktform
- Diagonale Scanrichtung
- Bögen (`G2/G3`) bei der Ausgabe von Konturen
- Weitere Treiber: K40 M2 Nano (Lihuiyu), Marlin, Smoothieware, Ruida
- Mehrere Motive pro Auftrag
- Kamera-Unterstützung
- Motivbibliothek (wie „My NC Files“), ausdrücklich nicht gewünscht

---

## 16. Referenz: Engraver Master

Zur Orientierung, was ersetzt wird (Version V1.60.0.285, Build 2022-06-07):

- Tabs: Print Picture, **NC Sender**, Quick Tagging, Device Settings. Genutzt werden Bild, Text, Steuerung, Einstellungen. Quick Tagging und die Motivbibliothek entfallen.
- Tab NC Sender:
  - Knöpfe: Load NC File, Load CAD DXF File, Export NC File
  - Repeats (Wiederholungen), Speed als Schieberegler (Beispielwert 820)
  - Start, Pause, Test Size (= Rahmen abfahren), Fortschrittsbalken, Laufzeit
  - „Create image NC file“: Load Picture, Modi **Full Mode** (zusammenhängende Flächen gefüllt), **Cutting Mode** (Umriss), **Line** (zeilenweise, für Fotos), Pixel Size (Beispiel 0,06 mm), New Size in mm, OK, Reset
  - „Create text NC file“: Font Name, Char Height in mm, Outline, Line Height, Textfeld, Create
  - „My NC Files“: Motivbibliothek
  - Vorschau mit mm-Achsen, Nullpunkt unten links, Motivgröße als Überschrift
- Statuszeile zeigt „Grbl 1.1z“.

Zuordnung: Line → Raster, Full Mode → Füllung, Cutting Mode → Kontur, Pixel Size → Linienabstand, Test Size → Rahmen, Repeats → Durchgänge.
