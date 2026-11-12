# Sources and provenance

## Game data

Orcs & Elves II, BREW English release 7.0.41, id Software / Fountainhead Entertainment / Electronic Arts. Retrieved from https://www.myabandonware.com/game/orcs-elves-ii-vvv#BREW on 2026-11-04.

Original files are local inputs, excluded from Git:

| File              |   Bytes | CRC32    | SHA-256                                                          |
| ----------------- | ------: | -------- | ---------------------------------------------------------------- |
| orcsandelves2.mod |  292116 | 1631c7f1 | 1b8664e937ef8c43c474dff9b82c398b46425c5cca43eff5919e4ce96fc3d750 |
| orcsandelves2.bar | 2130702 | ad370c54 | 508aa62a368cc77c64113a68c5485f43871bf18fa819a58ae8e3aac085265c2f |

## Original Engine

Public source: https://github.com/new-thegrex-source/OEII_BREW_SymbianPort
Pinned revision: 2e9c7bfb79694ffbd8e648513edd229ee212c10a.
Engine reconstruction credited upstream to George Gorzhiyev (ygorz/oe2-re).
GPLv3 is preserved in engine/LICENSE. The public fork includes an SDL desktop host in addition to the Symbian host.

## Remastered Fork

Public source: https://github.com/PilotOfAsuka/orcs-elves-ii-remastered
Original author: PilotOfAsuka.
Adds the Remastered software lighting renderer (`host/lighting.c`, `host/presentation.c`), and BREW CMF/QCELP decoding pipeline on top of the BREW engine reconstruction.
