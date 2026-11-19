# CLAUDE.md

This is the SCUMM plugin for Parrot Cart. Parrot Cart is a ColecoVision
cartridge with an RP2350 microcontroller, 8 MB of PSRAM and a microSD card. A
plugin is a program for the cart, packed into one `.CVP` file that goes in the
card's `/PLUGINS` folder. It runs SCUMM adventure games (The Secret of Monkey
Island first) on the cart. `src/scummcv.c` and `src/nescv.c` turn the 320x200
picture into the console's tiles, and the music goes to the PSG or out of the
USB port as MIDI.

README.md is for players. CART-README.md is the text the cart shows on the
plugin's details page. This file is for working on the code.

## The plugin

| | |
|---|---|
| File | `build/SCUMM.CVP` |
| ID | `parrotcart.scumm` |
| CMake target | `scumm` |
| Version | `VERSION` in the `cvcart_plugin(...)` call in CMakeLists.txt |
| License | GPL-3.0, from ScummVM (LICENSE, THIRD-PARTY.md) |

The engine is ScummVM's SCUMM engine, by way of Cabal and the frank-quest
port, in `vendor/frank-quest` (THIRD-PARTY.md).

## Layout

- `src/`: the plugin's code, and its icons (`*_icon.txt`, drawn as text).
- `vendor/`: the emulator or engine, as vendored from upstream. Keep changes
  there small and listed in THIRD-PARTY.md.
- `sdk/`: a copy of the Parrot Cart SDK, from the commit in `sdk/SOURCE.txt`.
  Don't edit it here. SDK changes are made upstream, and `tools/update-sdk.sh`
  copies a newer SDK in. The starter kit
  (https://github.com/plasticbugs/parrot-plugin-template) has the same SDK and
  its docs.
- `CART-README.md`: packed into the .CVP and shown by the cart, 30 characters
  wide.
- `RELEASE_NOTES.md`: the notes for the next release.
- `tools/ci/`: the CI's setup script, and `cvpinfo.py`, which reads a .CVP's
  header.

## Build

You need CMake, Ninja, Python 3, the Raspberry Pi pico-sdk 2.1.1 and the Arm
GNU Toolchain 9-2019-q4 (GCC 9.2.1). `tools/ci/setup.sh` installs those exact
versions on Ubuntu, and it lists the commits.

```sh
cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build
python3 tools/ci/cvpinfo.py build/SCUMM.CVP     # what the file's header says
```

The same source and compiler make the same file, byte for byte. `BUILD_EPOCH`
in CMakeLists.txt sets SOURCE_DATE_EPOCH for that. Another GCC makes a
different file. It may work, but it won't match the release.

To try it, copy `build/SCUMM.CVP` into `/PLUGINS` on the card, and each game's
folder in `/SCUMM`. Then start it from the cart's menu. RESET on the console
goes back to the menu.

## Releases

- Change the version in CMakeLists.txt and commit, then push a tag `vX.Y.Z`
  with the same number. `.github/workflows/release.yml` builds the plugin,
  checks that the .CVP's version matches the tag, and makes a GitHub release
  with the .CVP, the screenshots and RELEASE_NOTES.md. A tag that doesn't
  match fails.
- `screenshots/` holds up to three 256x192 PNGs of 100 KB at most. The first
  shows the cart's details page, which shows the version, so redraw it for a
  new version.
- The plugins site (https://plasticbugs.github.io/parrot-cart-plugins/) checks
  listed plugins once a day and adds new releases.
- `.github/workflows/build.yml` builds every push and pull request.

## Rules

- No ROMs, BIOS files or game data go in this repository, its releases or its
  screenshots. Game data (`.lfl`, `.000`, `.001`, `.sou`) is the player's.
  Never add any, even a demo, or a screenshot of a commercial game.
- Keep the license notices: LICENSE, THIRD-PARTY.md and the credits in both
  READMEs. Anything new in `vendor/` gets a line in THIRD-PARTY.md.
- The SDK starts core 1, which answers the console's reads of the cartridge.
  The plugin runs on core 0. Whatever core 1 runs has to stay in RAM and
  quick, so read `sdk/firmware/revc/sdk/README.md` before changing it.
- READMEs and release notes are short and plain.

## Notes

- It runs in place from flash (`XIP` in `cvcart_plugin()`), because it is too
  big to copy into RAM.
- The binary holds ScummVM's build date, so `BUILD_EPOCH` in CMakeLists.txt is
  the released build's exact second, not a day. Set it to the new release's
  time when you make one.
- MIDI out works with Parrot Cart MIDI
  (https://github.com/plasticbugs/parrot-cart-midi), which plays the MT-32
  music on a computer.
- Saves are `cart.s00` to `cart.s09` in the game's folder.

## The Claude skill

The starter kit (https://github.com/plasticbugs/parrot-plugin-template) has a
Claude skill for writing Parrot Cart plugins in
`.claude/skills/parrot-cart-plugin`. To use it in this repository, copy that
folder into `~/.claude/skills/`.
