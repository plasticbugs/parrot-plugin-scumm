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
- Keypad 1 shows the whole screen as one view. Press it again to give the verbs their own view.
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
- The verbs and inventory at the bottom have a view of their own, so they stay on screen when the room scrolls. When a game uses the whole screen for the room, as Fate of Atlantis does at the start, the plugin uses one view.
- Music and sound play on the console's sound chip. In Fate of Atlantis and Monkey Island 2 it plays the AdLib music, three notes and a drum at a time.
- Fate of Atlantis's speech plays on the sound chip when the music plays on a computer over MIDI (below). Without MIDI the game shows the lines as text.

## Game files

Put all of a game's files together in one folder. These are the games we've tried, and the file to pick in the menu for each:

| Game | File |
|---|---|
| The Secret of Monkey Island | `000.LFL` |
| Monkey Island 2: LeChuck's Revenge | `MONKEY2.000` |
| Indiana Jones and the Fate of Atlantis | `ATLANTIS.000` |

Fate of Atlantis's speech is in `MONSTER.SOU`, which only the CD version has.

## Music and speech

| On the cart's USB-C port | Music | Fate of Atlantis's speech |
|---|---|---|
| Nothing, or a hub with a keyboard or mouse | the ColecoVision's sound chip | shown as text |
| A computer running Parrot Cart MIDI | the computer, as MT-32 music | the ColecoVision's sound chip |

The USB-C port does one job at a time. While a computer takes the music, you can't use a keyboard or mouse.

## Hear the MT-32 music on your computer

The game also has music for the Roland MT-32. The cart can send it out of its USB-C port to a computer, and the free Parrot Cart MIDI program plays it there, on an emulated MT-32. Get it for macOS, Windows or Linux from https://github.com/plasticbugs/parrot-cart-midi/releases

It needs the ROM files from a Roland CM-32L or MT-32 that you own. CM-32L ROMs are best, because the game uses sound effects that only the CM-32L has.

1. Open Parrot Cart MIDI.
   - macOS: open the app. The first time, macOS blocks it. Go to System Settings > Privacy & Security, and click **Open Anyway**.
   - Windows: open `parrot-midi.exe`. If Windows says it protected your PC, click **More info**, then **Run anyway**.
   - Linux: run `./parrot-midi`.
2. Click **Choose ROM folder…** and pick the folder with your ROM files.
3. Plug the cart's USB-C port into the computer.
4. Start the game. The window says **Connected to Parrot Cart MIDI**, and the music plays on the computer.

Open the program before you start the game. The game sets up the MT-32's instruments as it starts.

On a Mac with Apple silicon, macOS asks whether to allow the accessory to connect the first time you plug in the cart. Click **Allow**. This is macOS checking before a new USB device can talk to it. The cart only sends music. If you clicked **Don't Allow**, unplug the cart, plug it in again, and click **Allow**. Until the computer allows the cart, the TV says that a computer is on the USB port. Press fire there to play without MIDI.

If you have a real MT-32 or CM-32L, connect it to the computer with a USB MIDI cable. In Parrot Cart MIDI, choose the cable under **Play on**. The program sends the music straight to it, so you don't need ROMs.

On the computer the cart is called "MovieCart MIDI", its old name. Parrot Cart MIDI finds it by that name, or by "Parrot Cart MIDI".

With MIDI playing, Fate of Atlantis's speech plays on the ColecoVision, about a fifth of a second after its text shows.

## Credits

- frank-quest by Mikhail Matveev, a port of ScummVM to the RP2350, under the GPL-3.0. The source is at https://github.com/rh1tech/frank-quest
- The engine is Cabal, a community fork of ScummVM, by the ScummVM and Cabal authors, under the GPL-2.0-or-later. The source is at https://github.com/project-cabal/cabal
- The OPL emulator from MAME, under the LGPL-2.1.
- FatFs by ChaN, under a BSD-style license.
- TinyUSB by Ha Thach, under the MIT license. The source is at https://github.com/hathach/tinyusb
- The plugin itself is by Parrot Cart. Its full source is linked next to its download.

## Changes since 0.2.9

- Fate of Atlantis and Monkey Island 2 play their music on the ColecoVision's sound chip. Before, they were silent without MIDI.
- With the music going to a computer over MIDI, Fate of Atlantis's speech plays on the ColecoVision.
- In Fate of Atlantis and Monkey Island 2, notes no longer go missing when the game is busy.
- The cart reads speech from the card faster.
- A USB keyboard or mouse works through more kinds of hub, whether you plug it in before you start or while you play.
- The USB code is the SDK's TinyUSB 0.21, in place of the pico-sdk's 0.18.
- A new icon.

## Build it yourself

You need the Raspberry Pi Pico SDK 2.1.1, an Arm GNU toolchain, CMake,
Ninja, Python 3, sjasmplus and picotool. Releases are built with the Arm
GNU Toolchain 9-2019-q4 (GCC 9.2.1) and picotool 2.3.1. With those you get
the same file, byte for byte.

```sh
cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build
```

This makes `build/SCUMM.CVP`. Copy it into the `/PLUGINS` folder on the card.

On Ubuntu, `tools/ci/setup.sh` installs the same tools that GitHub uses to
build each release.

`CART-README.md` is the text the cart shows on the plugin's page. It is
this README without the game files, the music and speech table and the
list of changes, and with a shorter MIDI section.

## License

The plugin is under the GNU GPL, version 3 (`LICENSE`). `THIRD-PARTY.md`
lists the parts that came from elsewhere, and their licenses.
