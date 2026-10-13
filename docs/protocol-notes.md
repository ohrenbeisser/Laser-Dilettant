# GRBL protocol notes

Notes on how Laser-Dilettant talks to GRBL 1.1 (see the GRBL wiki pages "Grbl v1.1 Interface",
"Grbl v1.1 Commands" and "Grbl v1.1 Laser Mode").

## Connecting

1. Open the port with 8N1, no flow control. The Arduino Nano resets (DTR).
2. Wait up to 5 s for the banner `Grbl X.Y...` (also `Grbl 1.1z`, `GrblHAL ...`).
   Without banner, send a soft reset (0x18) and wait another 3 s.
3. Query `$I`, `$$`, `$G`, then send `M5` (laser off).
4. Start polling with `?` every 200 ms. No status for 3 s means the connection is lost.

## Streaming

Character counting: GRBL's receive buffer holds 128 bytes (127 usable). The driver keeps the length
(including `\n`) of each line that was sent but not answered yet and only sends a new line when it fits.
Each `ok` or `error:N` answers the oldest line.

Lines that make GRBL write to its EEPROM (`$N=value`, `$RST`, `$H`, `$X` and the other `$` commands
except `$J=`, plus `G10`, `G28.1`, `G30.1`) are sent alone: only when nothing is in flight, and nothing
follows until they are answered. GRBL may lose characters while writing the EEPROM.

After a reset (banner) GRBL's receive buffer is empty: all lines in flight are forgotten.

## Stopping

- **Stop** (keeps the position): feed hold `!`, wait for `Hold:0` (or `Idle`), soft reset `0x18`,
  then `M5` after the banner.
- **Emergency stop**: soft reset `0x18` immediately. If the machine was moving, GRBL reports
  `ALARM:3` and the position may be lost.

## Focus laser

`M3 S<value>` followed by `G1 F100` without axis words. In laser mode (`$32=1`) GRBL switches the
laser on at standstill when the modal motion mode changes to G1 with M3 (constant power) active.
If a firmware stays dark anyway, the program can switch `$32=0` temporarily; the old value is stored
in the program settings and written back when the focus laser is switched off, on reset, disconnect,
quit and on the next connection.

## Simulator

`SimulatedGrbl` implements the same byte interface as the serial port and behaves like GRBL 1.1h:
banner, 128-byte receive buffer, 15-block planner, transfer time according to the baud rate, motion
with feed and acceleration, feed hold, overrides, `$` commands, error codes, laser mode rules.
All automatic tests run against it.
