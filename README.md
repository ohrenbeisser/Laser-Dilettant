# Laser-Dilettant

**English** · [Deutsch](README.de.md)

Open source control software and G-code generator for GRBL laser engravers, for Linux and Windows.
It replaces "Engraver Master" (especially its "NC Sender" tab) and uses device profiles to work with
many GRBL diode lasers and with K40 lasers that have a GRBL board.

> **Status: stage 1 of 7.** Connecting, status display, jogging, work origin, focus laser, console,
> emergency stop, device profiles, GRBL parameters and the firmware assistant are done.
> Sending jobs (NC sender), pictures, text, SVG/DXF and the test grid follow in the next stages.

## Safety

**A laser can start fires and cause permanent eye damage.**

- Wear safety glasses that match the wavelength of your laser (diode lasers: usually 445 nm).
- Never leave the laser unattended. Keep a fire extinguisher at hand.
- Extract the smoke, especially with plastics and coated materials.
- **The emergency stop of the program (button or Esc key) does not replace a hardware emergency stop.**
  If the PC hangs or the USB connection breaks, it has no effect. Use a switch that cuts the power
  of the laser module.

## Features (stage 1)

- Connection via USB serial (9600 to 230400 baud) and a built-in **simulator** to try things safely
- Status display (state, position, feed, power, overrides, firmware), status query every 200 ms
- Jogging with buttons and arrow keys, set/reset work origin, unlock, homing, reset
- Focus laser with power setting and automatic switch-off
- Console with history, real-time commands and an optional communication log
- Emergency stop (button and Esc, even while typing in a text field)
- Device profiles (built-in: DIY8000 4050/1720, generic GRBL diode laser, K40 with GRBL board)
- Table of all GRBL parameters with explanations: read, change, back up to a file, restore
- Back up the firmware and flash GRBL 1.1h (guided assistant using avrdude)
- User interface in English and German, light and dark design

## Installation

Ready-made packages (AppImage, Windows installer) follow in stage 7. Until then, build from source.

### Linux: access to the serial port

The laser shows up as `/dev/ttyUSB0` (CH340) or `/dev/ttyACM0`. Normal users may only open it when they
belong to the group `dialout`:

```sh
sudo usermod -aG dialout $USER
```

Then log out and back in.

avrdude is needed to back up and flash the firmware:

```sh
sudo apt install avrdude
```

### Windows: CH340 driver

Many low-cost lasers use the CH340 USB serial converter. If no COM port appears, install the CH340
driver from the manufacturer (WCH).

## Building from source

Requirements:

- CMake 3.21 or newer, a C++20 compiler (GCC 11+, Clang 14+, MSVC 2022)
- **Qt 6.5 or newer** (6.8 LTS recommended) with the modules Qt Serial Port and Qt SVG.
  Qt 6.4 from the Ubuntu 24.04 / Linux Mint 22 packages also builds.

Linux Mint 22 only ships Qt 6.4. The easiest way to get a newer Qt is `aqtinstall`:

```sh
pip install aqtinstall
aqt install-qt linux desktop 6.8.3 linux_gcc_64 -m qtserialport -O ~/Qt
```

Or use Qt 6.4 from the distribution:

```sh
sudo apt install build-essential cmake qt6-base-dev qt6-serialport-dev qt6-svg-dev \
                 qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools libgl-dev
```

Build and test:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=~/Qt/6.8.3/gcc_64
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/laser-dilettant
```

`-DLD_BUILD_APP=OFF` builds only the core library and the tests (no user interface).

## Switching the firmware to 115200 baud

Many DIY8000 devices come with a manufacturer firmware ("Grbl 1.1z") at 9600 baud. In the tab
**Settings → Firmware** an assistant guides you to GRBL 1.1h (115200 baud):

1. back up the GRBL settings (`$$`) (required)
2. back up the current firmware with avrdude (strongly recommended)
3. flash GRBL 1.1h
4. connect again at 115200 baud
5. compare and restore the saved settings, check laser mode (`$32=1`)

The saved manufacturer firmware can be written back the same way.

## License

GPL-3.0-or-later, see [LICENSE](LICENSE). The licenses of third-party components are in [LICENSES](LICENSES).
GRBL 1.1h and the GRBL message texts come from [gnea/grbl](https://github.com/gnea/grbl) (GPL-3.0).
