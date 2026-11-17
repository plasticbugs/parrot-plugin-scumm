# SCUMM

This plugin plays SCUMM adventure games on the ColecoVision, such as The Secret of Monkey Island. It runs ScummVM's SCUMM engine.

## Setup

Copy `SCUMM.CVP` into the `/PLUGINS` folder on the card. Copy each game's folder onto the card, in `/SCUMM` or anywhere else. To play, pick the game's `000.LFL` file in the menu, or `MONKEY2.000` for Monkey Island 2.

A USB mouse and keyboard work too. They need a powered hub on the cart's USB port.

## Controls

On the ColecoVision controller:

- The joystick moves the pointer. It speeds up as you hold it.
- Left fire and right fire are the left and right mouse buttons.
- Keypad * is Esc, which skips a cutscene.
- Keypad 0 skips a line of talk.
- Keypad 4 and 6 move the view left and right, and it stays there.
- Keypad 5 centers the view and lets it follow the pointer again.
- Hold keypad 7 for a second to save to slot 1, or 9 to load it.
- Hold keypad # for half a second to open the save and load screen.
- Press RESET on the console to go back to the menu.

On the save and load screen:

- Pick a slot with the joystick, or with keypad 0 to 9.
- Left fire saves to the slot. If the slot has a save already, press left fire again to save over it.
- Right fire loads the slot. Press right fire again to be sure.
- Keypad # or * goes back to the game.

## Saves

- There are ten slots, kept in the game's folder as `cart.s00` to `cart.s09`.
- Slot 0 is the autosave, made every 5 minutes. You can only load it.
- Slots 1 to 9 are yours.

## Notes

- The TV shows 256 by 192 pixels of the game's 320 by 200. The view follows the pointer.
- Music and sound play on the console's sound chip.
- If the cart's USB port is plugged into a computer when the game starts, the game's MT-32 music goes to the computer as MIDI instead. To hear it, use an MT-32 or an MT-32 emulator such as Munt. If the computer asks whether to allow the cart, say yes. Or press fire to play without MIDI.

## Credits

- frank-quest by Mikhail Matveev, a port of ScummVM to the RP2350, under the GPL-3.0. The source is at https://github.com/rh1tech/frank-quest
- The engine is Cabal, a community fork of ScummVM, by the ScummVM and Cabal authors, under the GPL-2.0-or-later. The source is at https://github.com/project-cabal/cabal
- The OPL emulator from MAME, under the LGPL-2.1.
- FatFs by ChaN, under a BSD-style license.
- TinyUSB by Ha Thach, under the MIT license. The source is at https://github.com/hathach/tinyusb
- The plugin itself is by Parrot Cart. Its full source is linked next to its download.

## Build it yourself

You need the Raspberry Pi Pico SDK 2.1.1, an Arm GNU toolchain, CMake,
Ninja, Python 3, sjasmplus and picotool. Releases are built with the Arm
GNU Toolchain 9-2019-q4 (GCC 9.2.1) and picotool 2.3.1. With those you get
the same file, byte for byte.

This plugin uses the pico-sdk's own copy of TinyUSB. Run
`git submodule update --init lib/tinyusb` in your pico-sdk folder first.

```sh
cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build
```

This makes `build/SCUMM.CVP`. Copy it into the `/PLUGINS` folder on the card.

On Ubuntu, `tools/ci/setup.sh` installs the same tools that GitHub uses to
build each release.

`CART-README.md` is the text the cart shows on the plugin's page. It is
the start of this README.

## License

The plugin is under the GNU GPL, version 3 (`LICENSE`). `THIRD-PARTY.md`
lists the parts that came from elsewhere, and their licenses.
