# Laser-Dilettant

Ein Programm für den DIY-Diodenlaser: Bild laden, aufbereiten, als G-Code erzeugen und über USB an einen Laser mit GRBL schicken. Es läuft unter Linux (Windows ist vorgesehen, siehe unten) und ist zugleich ein Lernprojekt für C++ mit Qt.

> **Stand: Beta 0.1.0.** Das Programm ist am echten Laser getestet, kann aber Fehler enthalten. Neue Einstellungen zuerst mit niedriger Leistung an Probematerial prüfen.

> **Sicherheit.** Ein Laser kann Augen schädigen und Feuer auslösen. Schutzbrille für die Wellenlänge des Lasers tragen, nichts Brennbares im Arbeitsbereich lassen, für Abluft sorgen, den Laser nie unbeaufsichtigt laufen lassen. Der Not-Aus dieser Software ist **kein** Hardware-Not-Aus: Bei einem Abbruch der USB-Verbindung, einem Rechnerabsturz oder einem Programmfehler kann er ausfallen. Die Stromversorgung des Lasers muss jederzeit erreichbar sein. Die Nutzung geschieht auf eigene Gefahr, es gibt keine Gewährleistung (siehe Lizenz).

## Was es kann

**Bild vorbereiten**
- Bilder laden (JPG, PNG, BMP und weitere Formate, SVG als Vektorgrafik), drehen, spiegeln, skalieren, zuschneiden, in Schwarz/Weiß, mit Dithering oder in Graustufen umwandeln.
- Text als Vektorpfade, Materialtest (Tabelle aus Leistung und Speed) und eine Messgrafik zum Kalibrieren der Maße.
- Vorschau der Laserwege und eine Simulation, die den gebrannten Teil mitzeichnet.

**G-Code erzeugen**
- Drei Arten: **Full** (zusammenhängende Flächen im Zickzack füllen), **Cutting** (Konturen) und **Linie** (Rasterzeilen).
- Wegoptimierung, Überscan, mehrere Durchgänge, Graustufen mit eigener Leistung je Stufe.
- Maschinen- und Materialprofile, Projektdateien (`.ldproj`). Auch fertige NC-Dateien lassen sich laden, prüfen, simulieren und brennen.

**Laser steuern**
- Verbindung zu GRBL 1.1 über USB, Streaming mit Rückmeldung, Statusanzeige, Konsole, Jog, Nullpunkt setzen, Rahmen abfahren, Schwachlicht zum Positionieren.
- Start, Pause, Abbrechen und ein Software-Not-Aus. Live-Regler für Speed und Leistung während der Gravur.
- Alarme, GRBL-Parameter, ein Selbsttest nach dem Verbinden und ein Simulator als Anschluss, mit dem sich alles ohne Laser ausprobieren lässt.
- Dialog "Firmware" (Menü Maschine): zeigt Version, Optionen und alle Einstellungen des Lasers. Dazu gibt es eine Sicherung der Firmware und einen Dialog für ein Update (das Aufspielen selbst ist noch gesperrt).

**Im Blick behalten**
- **Meldung aufs Handy über ntfy:** bei Fehlern und Alarmen, auf Wunsch auch wenn ein Auftrag fertig ist.
- **Fernansicht im Heimnetz:** eine Webseite mit Fortschritt, Laufzeit, Restzeit, dem schon gebrannten Teil des Bildes, Pause/Weiter/Abbrechen und den Reglern für Speed und Leistung. Aus, bis du sie einschaltest; die Steuerung braucht eine zweite Freigabe.
- **Protokolle:** Auftrags- und Systemprotokoll als CSV, mit Statistik im Programm.

## Welcher Laser?

Entwickelt und getestet mit einem DIY8000-Desktop-Laser (Diode, GRBL 1.1z, USB-Chip CH340, 400 × 500 mm). Er hat keine Endschalter, keine Z-Achse und keinen Hardware-Not-Aus; das Programm ist darauf eingestellt. Andere GRBL-1.1-Laser sollten laufen, sind aber nicht geprüft. Wichtig ist `$32=1` (Laser-Modus) in GRBL.

## Installation

Fertige Pakete (deb, AppImage, Windows-Installer, Flatpak) baut GitHub Actions bei jeder Version und hängt sie an das [Release](https://github.com/ohrenbeisser/Laser-Dilettant/releases). Windows-Installer und Flatpak sind noch neu und nicht auf einem echten Rechner erprobt. Alternativ: aus dem Quellcode bauen.

### Bauen unter Linux

Gebraucht werden ein C++20-Compiler, CMake (ab 3.21), Ninja und **Qt 6.8** (Widgets, Svg, SerialPort, Network). Potrace und nanosvg liegen im Repository; es braucht **kein Internet** zum Bauen.

Unter Linux Mint / Ubuntu so einrichten (die Systempakete brauchen `sudo`; Qt 6.8.3 kommt mit dem Werkzeug `aqt` nach `~/Qt`, ohne Qt-Konto):

```bash
sudo apt install cmake ninja-build build-essential pkg-config git \
    libgl1-mesa-dev libxkbcommon-dev libxkbcommon-x11-0 libfontconfig1-dev libdbus-1-dev \
    libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 \
    libxcb-shape0 libxcb-xkb1 libxcb-xinerama0 libxcb-randr0 python3-venv python3-pip
python3 -m venv ~/aqt-venv && ~/aqt-venv/bin/pip install aqtinstall
~/aqt-venv/bin/aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O ~/Qt -m qtserialport qtimageformats
```

Danach:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
build/linux-debug/src/app/laser-dilettant
```

Die Presets erwarten Qt unter `~/Qt/6.8.3/gcc_64`; liegt es woanders, `CMAKE_PREFIX_PATH` anpassen.

### Paket für Debian, Ubuntu und Linux Mint (deb)

Das Paket bringt Qt 6.8 selbst mit und läuft ab **Ubuntu 24.04 / Linux Mint 22**. Bauen und installieren:

```bash
cmake --preset linux-release -DLD_BUILD_TESTS=OFF
cmake --build --preset linux-release
cd build/linux-release && cpack -G DEB
sudo apt install ./laser-dilettant_0.1.0_amd64.deb
```

Danach startet das Programm mit `laser-dilettant` oder aus dem Anwendungsmenü.

### AppImage

`packaging/linux/make-appimage.sh` baut einen Ordner, der ohne Installation läuft (`AppRun`), und packt ihn mit `appimagetool` (muss im Pfad liegen oder mit `APPIMAGETOOL=/Pfad/appimagetool` angegeben werden) zu einer einzelnen Datei. Es gilt dasselbe Mindestsystem wie beim deb-Paket. Zum Starten braucht der Rechner FUSE (`fusermount`, unter Ubuntu meist schon da); ohne FUSE geht `./Laser-Dilettant-0.1.0-x86_64.AppImage --appimage-extract-and-run`.

### Windows

Der Code ist plattformunabhängig. Der Windows-Installer (`Laser-Dilettant-<Version>-Setup.exe`) wird von GitHub Actions gebaut (MSVC 2022, Qt 6.8.3, NSIS); er ist noch nicht auf einem echten Windows-Rechner erprobt. Selbst bauen: `packaging/windows/windows.cmake` beschreibt die Schritte.

## Den Laser verbinden

1. Laser per USB anschließen. Baudrate **115200**.
2. **Linux:** Der Benutzer muss in der Gruppe `dialout` sein (`sudo usermod -aG dialout $USER`, danach neu anmelden). Der Anschluss heißt meist `/dev/ttyUSB0`.
3. **Windows:** Der USB-Chip braucht oft einen Treiber (CH340/CH341).
4. Im Programm links den Anschluss wählen und verbinden. Der Selbsttest vergleicht GRBL mit dem Maschinenprofil und meldet Abweichungen (zum Beispiel `$30` oder fehlenden Laser-Modus).
5. Den Kopf mit den Jog-Tasten an die gewünschte Stelle fahren und **"Hier ist Null"** setzen. Den Nullpunkt hat GRBL ohne Endschalter nicht von selbst.

Zum Ausprobieren ohne Laser gibt es den Anschluss **"Simulator (ohne Laser)"**.

## Erste Schritte

1. Bild laden (Strg+O, hierher ziehen oder aus der Zwischenablage einfügen).
2. Rechts Größe und Gravurart wählen, Speed und Leistung einstellen. Für den Anfang ein Material aus der Liste nehmen oder einen Materialtest brennen.
3. Mit der Vorschau und der Simulation prüfen, was der Laser fahren wird.
4. Verbinden, Nullpunkt setzen, mit **Rahmen** kontrollieren, wo das Bild landet.
5. **Start.** Mit Pause, Abbrechen und den Live-Reglern lässt sich der Lauf beeinflussen.

## Fernansicht und ntfy

Beides ist ausgeschaltet, bis du es in den Einstellungen einschaltest (Reiter "Webzugriff" und "Benachrichtigung").

- Die **Fernansicht** ist eine Webseite des Programms. Sie nimmt nur Verbindungen aus dem Heimnetz an und verlangt ein Passwort. **Sie ist nicht verschlüsselt (HTTP):** Das Passwort geht im WLAN im Klartext über die Leitung. Nur in einem vertrauten Netz benutzen. Das Passwort selbst wird nur als Hash gespeichert. Die Fernansicht ersetzt nicht die Aufsicht.
- Bei **ntfy** läuft die Nachricht über den Dienst ntfy.sh (oder einen eigenen Server). Der Themenname ist wie ein Passwort: Wer ihn kennt, liest mit. In den Nachrichten stehen keine Dateinamen.

## Firmware-Update: Vorsicht

Das Aufspielen einer neuen Firmware kann den Laser unbenutzbar machen und ist ohne ISP-Programmierer nicht rückgängig zu machen. Darum ist es im Programm gesperrt. Aktiv sind das Auslesen der Firmware, die Sicherung (Flash und EEPROM) und ein Trockenlauf, der nur prüft, ob der Bootloader antwortet. Dafür muss das Programm `avrdude` installiert sein (`sudo apt install avrdude`).

## Aufbau des Projekts

| Ordner | Inhalt |
|---|---|
| `src/core` | Fachlogik ohne Fenster und ohne Seriellport: Bildaufbereitung, G-Code, Profile, Vorschau, Protokolle, HTTP und Anmeldung |
| `src/grbl` | Verbindung zu GRBL, Streaming, simulierter Controller |
| `src/app` | Oberfläche (Qt Widgets) und die Steuerung der Abläufe |
| `src/web` | Die Seite der Fernansicht (wird ins Programm eingebettet) |
| `manual` | Bilder und Beispielbild für das Handbuch (`MANUAL.md`) |
| `third_party` | potrace, nanosvg |

## Stand und Ausblick

Version 0.1.0 (Beta). Das Programm ist am echten Laser getestet. Dazu gehören Firmware-Dialog, ntfy, Fernansicht, Hilfe im Programm und ein ausführliches Handbuch (`MANUAL.md`, mit Bildern). Pakete für Linux (deb, AppImage) lassen sich bauen; Flatpak und ein Windows-Installer fehlen noch. Auch die Ebenen (mehrere Arbeitsschritte in einem Auftrag) gibt es noch nicht. Die Oberfläche gibt es auf Deutsch und Englisch (Einstellungen → Sprache); Handbuch und README sind nur Deutsch.

## Lizenz

Laser-Dilettant steht unter der **GNU General Public License, Version 3** (siehe [LICENSE](LICENSE)). Das Programm kommt ohne jede Gewährleistung.

Verwendete Software und Dateien:

| Was | Lizenz |
|---|---|
| Qt 6.8 (Widgets, Svg, SerialPort, Network), dynamisch gelinkt | LGPL 3 |
| potrace 1.16 (Peter Selinger) | GPL 2 oder später |
| nanosvg (Mikko Mononen) | zlib |
| Schriften Roboto, Roboto Mono, Montserrat Alt1 (Fernansicht) | SIL Open Font License |
| Symbole Material Symbols (Fernansicht) | Apache 2.0 |

Die Lizenztexte der Schriften und Symbole liegen in `src/web/fonts`.

Quellcode: <https://github.com/ohrenbeisser/Laser-Dilettant>
