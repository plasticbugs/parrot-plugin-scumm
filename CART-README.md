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

## Hear the MT-32 music on your computer

The game's MT-32 music can play on a computer. Get the free Parrot Cart MIDI program for macOS, Windows or Linux. It's on GitHub at `plasticbugs/parrot-cart-midi`.

1. Open Parrot Cart MIDI. Pick the folder with the ROMs from your CM-32L or MT-32.
2. Plug the cart's USB-C port into the computer.
3. Start the game.

If a Mac asks to allow the accessory, click Allow. If the TV says a computer is on the USB port, press fire to play without MIDI.

For a real MT-32, choose its USB MIDI cable under Play on.

Older versions call the cart MovieCart MIDI. Parrot Cart MIDI finds either name.

With nothing plugged into the USB-C port, the music plays on the ColecoVision's sound chip.

## Credits

- frank-quest by Mikhail Matveev, a port of ScummVM to the RP2350, under the GPL-3.0. The source is at https://github.com/rh1tech/frank-quest
- The engine is Cabal, a community fork of ScummVM, by the ScummVM and Cabal authors, under the GPL-2.0-or-later. The source is at https://github.com/project-cabal/cabal
- The OPL emulator from MAME, under the LGPL-2.1.
- FatFs by ChaN, under a BSD-style license.
- TinyUSB by Ha Thach, under the MIT license. The source is at https://github.com/hathach/tinyusb
- The plugin itself is by Parrot Cart. Its full source is linked next to its download.
