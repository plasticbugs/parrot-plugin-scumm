# What in this repository came from elsewhere

The plugin's own code, in `src/`, is under the GNU GPL, version 3
(`LICENSE`). The copy of the cart's SDK in `sdk/` is under the MIT license
(`sdk/LICENSE`). These parts came from elsewhere and keep their own terms.

| Where | What | License |
|---|---|---|
| `vendor/frank-quest/src/` | [Cabal](https://github.com/project-cabal/cabal), a community fork of ScummVM, by the ScummVM and Cabal authors (`vendor/frank-quest/COPYRIGHT`), in [frank-quest](https://github.com/rh1tech/frank-quest), Mikhail Matveev's port to the RP2350, with the changes listed in `vendor/frank-quest/VENDORED.md` | GPL-2.0-or-later (`vendor/frank-quest/COPYING`), and frank-quest as a whole GPL-3.0 (`vendor/frank-quest/LICENSE`) |
| `vendor/frank-quest/src/audio/softsynth/opl/mame.cpp`, `mame.h` | the OPL emulator from MAME | LGPL-2.1 (`vendor/frank-quest/COPYING.LGPL`) |
| `vendor/frank-quest/src/fatfs/` | FatFs by ChaN | its BSD-style license (in `ff.c` and `ff.h`) |
| `vendor/frank-quest/drivers/dlmalloc.c` | Doug Lea's malloc | public domain (CC0) |
| `vendor/frank-quest/drivers/usbhid/` | frank-quest's USB HID host code | GPL-2.0-or-later |
| `sdk/firmware/revc/sdk/tinyusb/` | [TinyUSB](https://github.com/hathach/tinyusb) (Ha Thach and contributors), with the cart's fixes, listed in its `VENDORED.md` | MIT (`sdk/firmware/revc/sdk/tinyusb/LICENSE`) |
| `cmake/pico_sdk_import.cmake` | from the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | BSD 3-Clause |

`vendor/frank-quest-sources.txt` lists the engine's files that the plugin
compiles.

The built plugin also contains the Raspberry Pi Pico SDK's runtime (BSD
3-Clause) and TinyUSB (MIT).

The 8x8 screen font's letter shapes come from the 1982 arcade game Mr. Do!
