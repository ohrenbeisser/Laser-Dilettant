# Laser-Dilettant

[English](README.md) · **Deutsch**

Quelloffenes Steuerprogramm und G-Code-Erzeuger für GRBL-Lasergravierer, für Linux und Windows.
Es entsteht als Ersatz für „Engraver Master“ (vor allem den Tab „NC Sender“) und soll über
Geräteprofile mit vielen GRBL-Diodenlasern und K40-Lasern mit GRBL-Board laufen.

> **Stand: Etappe 1 von 7.** Verbinden, Statusanzeige, Verfahren, Nullpunkt, Fokus-Laser,
> Konsole, Not-Aus, Geräteprofile, GRBL-Parameter und Firmware-Assistent sind fertig.
> Aufträge senden (NC-Sender), Bilder, Text, SVG/DXF und das Testraster folgen in den nächsten Etappen.

## Sicherheit

**Ein Laser kann Brände und bleibende Augenschäden verursachen.**

- Schutzbrille tragen, die zur Wellenlänge des Lasers passt (Diodenlaser meist 445 nm).
- Den Laser nie unbeaufsichtigt lassen. Feuerlöscher bereithalten.
- Rauch absaugen, vor allem bei Kunststoffen und beschichteten Materialien.
- **Der Not-Aus im Programm (Knopf oder Taste Esc) ersetzt keinen Hardware-Not-Aus.**
  Hängt der PC oder reißt die USB-Verbindung ab, wirkt er nicht. Empfehlung: ein Schalter,
  der die Stromversorgung des Lasermoduls trennt.

## Funktionen (Etappe 1)

- Verbindung über USB-Seriell (9600 bis 230400 Baud) und eingebauter **Simulator** zum gefahrlosen Ausprobieren
- Statusanzeige (Zustand, Position, Vorschub, Leistung, Overrides, Firmware), Statusabfrage alle 200 ms
- Verfahren mit Knöpfen und Pfeiltasten, Nullpunkt setzen/zurücksetzen, Entsperren, Referenzfahrt, Reset
- Fokus-Laser mit Leistungsregler und automatischer Abschaltung
- Konsole mit Verlauf, Echtzeitbefehlen und optionalem Kommunikationsprotokoll
- Not-Aus (Knopf und Esc, auch während der Eingabe in einem Textfeld)
- Geräteprofile (vordefiniert: DIY8000 4050/1720, allgemeiner GRBL-Diodenlaser, K40 mit GRBL-Board)
- Tabelle aller GRBL-Parameter mit deutscher Erklärung: lesen, ändern, in Datei sichern, wiederherstellen
- Firmware sichern und GRBL 1.1h flashen (geführter Assistent mit avrdude)
- Oberfläche auf Deutsch und Englisch, helles und dunkles Design

## Installation

Fertige Pakete (AppImage, Windows-Installer) gibt es ab Etappe 7. Bis dahin aus dem Quellcode bauen.

### Linux: Zugriff auf den seriellen Port

Der Laser erscheint als `/dev/ttyUSB0` (CH340) oder `/dev/ttyACM0`. Normale Benutzer dürfen den Port
erst öffnen, wenn sie zur Gruppe `dialout` gehören:

```sh
sudo usermod -aG dialout $USER
```

Danach ab- und wieder anmelden.

Für Firmware-Sicherung und Flashen wird avrdude gebraucht:

```sh
sudo apt install avrdude
```

### Windows: CH340-Treiber

Viele günstige Laser nutzen den USB-Seriell-Wandler CH340. Erscheint kein COM-Port, den CH340-Treiber
des Herstellers (WCH) installieren.

## Bauen aus dem Quellcode

Voraussetzungen:

- CMake 3.21 oder neuer, ein C++20-Compiler (GCC 11+, Clang 14+, MSVC 2022)
- **Qt 6.5 oder neuer** (empfohlen 6.8 LTS) mit den Modulen Qt Serial Port und Qt SVG.
  Qt 6.4 aus den Paketquellen von Ubuntu 24.04 / Linux Mint 22 reicht zum Bauen ebenfalls.

Linux Mint 22 bringt über apt nur Qt 6.4 mit. Neueres Qt am einfachsten mit `aqtinstall`:

```sh
pip install aqtinstall
aqt install-qt linux desktop 6.8.3 linux_gcc_64 -m qtserialport -O ~/Qt
```

Oder mit Qt 6.4 aus den Paketquellen:

```sh
sudo apt install build-essential cmake qt6-base-dev qt6-serialport-dev qt6-svg-dev \
                 qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools libgl-dev
```

Bauen und testen:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=~/Qt/6.8.3/gcc_64
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/laser-dilettant
```

Mit `-DLD_BUILD_APP=OFF` werden nur die Kernbibliothek und die Tests gebaut (ohne Oberfläche).

## Firmware auf 115200 Baud umstellen

Viele DIY8000-Geräte kommen mit einer Hersteller-Firmware („Grbl 1.1z“) mit 9600 Baud. Im Tab
**Einstellungen → Firmware** führt ein Assistent durch den Wechsel auf GRBL 1.1h (115200 Baud):

1. GRBL-Einstellungen (`$$`) sichern (Pflicht)
2. vorhandene Firmware mit avrdude sichern (dringend empfohlen)
3. GRBL 1.1h flashen
4. mit 115200 Baud neu verbinden
5. gesicherte Einstellungen vergleichen und zurückschreiben, Lasermodus (`$32=1`) prüfen

Die gesicherte Hersteller-Firmware lässt sich über denselben Weg wieder aufspielen.

## Lizenz

GPL-3.0-or-later, siehe [LICENSE](LICENSE). Die Lizenzen der Fremdkomponenten liegen in [LICENSES](LICENSES).
GRBL 1.1h und die GRBL-Meldungstexte stammen aus [gnea/grbl](https://github.com/gnea/grbl) (GPL-3.0).
