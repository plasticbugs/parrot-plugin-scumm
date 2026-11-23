# frank-quest (ScummVM core + SCUMM engine), vendored

- Upstream: https://github.com/rh1tech/frank-quest, commit
  `949301b6ef87a5482340167378cd102fcdb82ef2` (2026-05-12). Copied 2026-11-12.
- Origin: frank-quest is "a ScummVM adventure-game engine running on the
  Raspberry Pi Pico 2 (RP2350)" by Mikhail Matveev. Its `src/` is
  [Cabal](https://github.com/project-cabal/cabal), a community fork of
  ScummVM (the 2016-era code base: `Common::String`, `Graphics::Surface`
  with `w`/`h`/`pixels`, static plugins), plus an RP2350 backend (HSTX HDMI,
  SD card through FatFs, I2S audio, PS/2 and USB HID input, a dlmalloc heap
  in 8 MB of PSRAM) and some trimming for small memory (`CABAL_MINIMAL_AUDIO`
  and friends). frank-quest builds AGI, GOB, KYRA, SCI and SCUMM v0-v7; only
  the core and SCUMM (v0-v6) are kept here. Not compared against Cabal
  itself.
- Licences, all kept verbatim:
  - `COPYING`: GNU GPL v2. Nearly everything under `src/` (ScummVM / Cabal,
    GPL-2.0-or-later per file) and frank-quest's own files (`src/main.c`,
    `src/frank_quest_*`, `src/board_config.h`, `src/backends/*/rp2350/`,
    `drivers/psram_*`, `drivers/usbhid/`), which say
    `SPDX-License-Identifier: GPL-2.0-or-later` in their headers.
  - `COPYING.LGPL`: LGPL 2.1, for `src/audio/softsynth/opl/mame.cpp/.h`
    (the MAME OPL emulator).
  - `LICENSE`: GPL v3, the licence of the frank-quest repository as a whole.
  - `COPYRIGHT`: the ScummVM / Cabal authors list.
  - `src/fatfs/`: ChaN's FatFs, BSD-style one-clause licence in `ff.c`/`ff.h`.
  - `drivers/dlmalloc.c`: Doug Lea's malloc, public domain (CC0).
  - `drivers/usbhid/hid_app.c` is GPL-2.0-or-later, based on the MIT TinyUSB
    HID host example.
  - Upstream's `COPYING.BSD` (ScummVM's note about its BSD-licensed parts)
    and `COPYING.FREEFONT` (fonts) are left out: no kept file is under them.

## Kept

421 upstream files, 7.6 MB, at their upstream paths (plus this file).

- The ScummVM core and the SCUMM engine, as far as a SCUMM-only build uses
  them. The set was worked out from the dependency files of a real link on
  the Mac (every object the linker pulled, every header those include), not
  from frank-quest's CMake lists. The 149 files that get compiled are listed
  in `../../test/sources.txt`. Roughly:
  - `src/common/`, `src/base/`, `src/graphics/` (surfaces, cursors,
    thumbnails, SJIS), `src/engines/` (engine, game, dialogs, savestate,
    metaengine), the default event, timer and save-file backends, and the
    headers all that includes (some of `src/gui/`, `src/backends/`).
  - `src/audio/`: the mixer, rate converter, decoders (PCM, ADPCM, VOC,
    WAV), MIDI driver/parser framework, MPU-401, the AdLib driver with the
    DOSBox and MAME OPL emulators, PC speaker, CMS, SID (reSID), FM Towns /
    PC-98 FM, Paula/TFMX.
  - `src/engines/scumm/`: SCUMM v0-v6 (scripts, graphics, costumes, AKOS,
    actors, boxes, charsets, saves), iMUSE, and every platform's music
    player (PC speaker/PCjr, CMS, AdLib, Amiga, Mac, C64 SID, NES, PCE,
    Towns, Apple II). The engine class refers to all of them, so they are
    linked even for a DOS game. Four HE files (`he/resource_he.cpp`,
    `script_v60he.cpp`, `script_v70he.cpp`, `sound_he.cpp`) are referenced
    from non-HE code and kept. The v7/v8 headers (`scumm_v7.h`, `scumm_v8.h`,
    `imuse_digi/dimuse.h`, `insane/insane.h`, `smush/*.h`, `nut_renderer.h`)
    are included from kept code; their contents are behind
    `ENABLE_SCUMM_7_8`, which is not defined.
- frank-quest's device base, for reference when writing the cart's backend;
  none of it is built here:
  - `src/main.c` (clocks, PSRAM, HDMI, second core), `src/frank_quest_*`
    (game selector, launcher, `new`/`sbrk`/memory glue, FatFs wrapper with
    audio pumped between 4 KB reads), `src/board_config.h`.
  - `src/backends/platform/rp2350/`: `rp2350-minimal.cpp/.h` is the OSystem
    frank-quest uses; `rp2350-system.cpp/.h` its audio and timing;
    `rp2350.cpp/.h` an older ModularBackend OSystem that frank-quest no
    longer compiles (kept with the three headers it needs:
    `backends/modular-backend.h`, `backends/graphics/graphics.h`,
    `backends/mutex/null/null-mutex.h`).
  - `src/backends/fs/rp2350/` (filesystem nodes and buffered streams on
    FatFs), `src/backends/audiocd/rp2350/` (a do-nothing CD manager),
    `src/backends/fs/abstract-fs.cpp`.
  - `src/fatfs/` (FatFs R0.15; `ffunicode.c` is 2 MB of code-page tables).
  - `drivers/psram_*`, `drivers/dlmalloc.c` (the PSRAM heap),
    `drivers/usbhid/` (TinyUSB HID host: keyboard and mouse).
  - `linker/memmap_frank_quest.ld` (a 64 KB stack, which frank-quest needs
    for GOB).
- Mac only (`test/run.sh` builds them with `-DPOSIX`):
  `src/backends/fs/posix/`, `src/backends/fs/stdiostream.cpp/.h`.

## Left out

- The other engines: AGI, GOB, KYRA and SCI, which frank-quest builds, and
  every other Cabal engine still in upstream `src/engines/`.
- SCUMM v7/v8 code (Full Throttle's iMUSE Digital, SMUSH, INSANE, and
  `nut_renderer.cpp`, which does not compile without `ENABLE_SCUMM_7_8`), and
  the other `he/*.cpp` (empty without `ENABLE_HE`).
- Core files frank-quest compiles that nothing in a SCUMM-only link uses:
  `audio/midiplayer`, `audio/mods/{infogrames,maxtrax,module,protracker}`,
  `audio/softsynth/fmtowns_pc98/towns_pc98_driver`,
  `backends/base-backend.cpp`, `common/{dcl,iff_container,ini-file,tokenizer}`,
  `engines/advancedDetector.cpp`, `scumm/help.cpp`,
  `graphics/{pixelformat,yuv_to_rgb}.cpp`, `gui/gui-manager.cpp`, `image/`,
  `video/`.
- frank-quest's board drivers: `drivers/HDMI.*`, `drivers/ps2/`,
  `drivers/audio/` (I2S), `drivers/sdcard/`.
- `CMakeLists.txt`, `build.sh`, `flash.sh`, `release.sh`,
  `pico_sdk_import.cmake`, `dists/`, `screenshots/`, `README.md`,
  `version.txt`.

Because of those, the kept device files do not compile as they are:
`src/main.c` includes `HDMI.h`, `rp2350-minimal.cpp` the PS/2 headers,
`rp2350-system.cpp` the I2S `audio.h`, and `frank_quest_main.cpp` the AGI,
GOB, KYRA and SCI engines, `scumm/scumm_v7.h` and `YUVToRGBManager` (in
`teardownSingletons`).

## Changes

Marked `MovieCart` in the source; every other file is byte-identical to
upstream (`cmp` of each against the clone, 2026-11-13).

- `src/engines/scumm/players/player_v2.h/.cpp`: `g_movieCartPCjrTap`, a
  function pointer (null by default). When set, `generatePCjrSamples()`
  calls it for each of the 4 PCjr channels every time it renders a stretch
  of samples, which is at least once per player tick (236 Hz):
  `(channel, freqReg, attenuation, on)`, channel 0-2 tone and 3 noise,
  `freqReg` the 10-bit tone period (noise: the 3-bit noise control),
  attenuation 0 (loudest) to 15 (silent), `on` false when the channel is
  silent. The PCjr's SN76496 is the ColecoVision's SN76489 at the same
  3.579545 MHz clock, so these are the console PSG's own register values.
  The test host logs them (`SCUMM_MUSIC=pcjr`, `pcjr.txt`).
- `src/engines/scumm/string.cpp`: `g_movieCartMsgRect`, a `Common::Rect`
  that `CHARSET_1()` sets to `_charset->_str` (the message's extent on
  screen) each time it has printed some of an actor's message. The cart
  keeps that in its 256-pixel window while `_haveMsg` is set and the pointer
  is still (`_charset->_str` itself is reused by the verbs and the sentence
  line, so it can't be read later).

- `src/engines/scumm/players/player_v2cms.h/.cpp`: `g_movieCartCMSTap`.
  When set, `playMusicChips()` passes it each SAA1099's music registers
  (amplitudes, frequencies, octaves of its 4 music voices), and
  `offAllChannels()` calls it with chip -1 (all voices off).
- `src/engines/scumm/sound.h/.cpp`: `g_movieCartPreferAD`. In PC speaker /
  PCjr mode, `readSoundResourceSmallHeader()` asks it about a sound whose
  AD data is a music track (byte 2 = $80); when it says yes, the AD data is
  loaded as the CMS branch loads it, instead of the WA data. (MI1's floppy
  VGA has a 176-byte WA placeholder for 8 songs.)
- `src/engines/scumm/music.h`, `scumm.cpp`: `g_movieCartMusicEngine`, a
  factory `setupMusic()` uses for v3-v4 games in PCjr mode instead of
  `new Player_V2`. The cart's (`../../psgmix.cpp`) plays PCjr data on a
  Player_V2 and AD tracks on a Player_V2CMS.
- `src/engines/scumm/vars.cpp`: with `g_movieCartMusicEngine` set, PCjr
  mode tells the game it has the Game Blaster (`VAR_SOUNDCARD` 2, not 1):
  that engine plays the CMS songs too, and MI1 starts the SCUMM Bar's song
  (and others) only on that card or better.
- `src/engines/scumm/players/player_v2cms.cpp`: `stopSound()` and
  `stopAllSounds()` clear `_loadedMidiSong` (upstream leaves it, so a
  stopped AD song still reads as running and MI1's bar never restarts it).
- `src/graphics/scaler/thumbnail_intern.cpp`: `scaleThumbnail()` copies an
  input already the thumbnail's size (320 x 200 halved) instead of
  resampling it, and the resampler's vertical scale is `targetHeight /
  inHeight` (upstream: `/ inWidth`, which squashed the picture into the
  top 60 % and read past it for the rest).
- `src/audio/audiodev/opl.h/.cpp`: `OPL::g_movieCartOPL`, a factory
  `Config::create()` uses instead of an emulator when it is set. The cart's
  (`../../psgmix.cpp`) is an OPL2 that keeps the registers and makes no
  sound: the AdLib driver's notes go to the PSG (SCUMM v5, iMUSE).
- `src/engines/scumm/scumm.h`: `movieCartCutscene()` (a cutscene, or the
  player's input off), `movieCartCutsceneDepth()` and `movieCartUserPut()`
  (for the test host's `SCUMM_STATELOG`).
- `src/frank_quest_fs.c`: files of 1 MB or more get FatFs's fast seek, the
  cluster map made the first time a file opens and kept, so a seek into
  Fate of Atlantis's MONSTER.SOU (each line of speech) doesn't walk the
  FAT from the file's start.

## Building it

- Include paths: `src` and `src/engines`.
- Definitions (frank-quest's, SCUMM only): `ENABLE_SCUMM=1` (not
  `ENABLE_SCUMM_7_8`), `CABAL_HAVE_STDINT_H=1`, `CABAL_LOW_MEMORY=1` (no kept
  file tests it), `DISABLE_GUI=1`, `DISABLE_DEFAULT_AUDIOCD_MANAGER=1`,
  `CABAL_MINIMAL_AUDIO=1` (the static music plugins are then AdLib, PC
  speaker and PCjr only: `base/plugins.cpp`), `CABAL_SCREEN_WIDTH=320`,
  `CABAL_SCREEN_HEIGHT=200`, `CABAL_VERSION="..."`. Endianness and
  alignment come from `common/scummsys.h`: define `__RP2350__=1` or
  `PICO_ON_DEVICE=1` (little endian, `SCUMM_NEED_ALIGNMENT`), or
  `HAVE_CONFIG_H` and supply a `config.h` (`test/config.h` does that on the
  Mac). Nothing in the core or the engine includes a Pico SDK header.
- C++17 (`-std=gnu++17`) with `-fno-exceptions -fno-rtti` builds with the
  Mac's clang and with arm-none-eabi-g++ 9.2.1. The code uses `register`, so
  `-Wno-register`; it is old and warns a lot (`test/run.sh` uses `-w` for
  vendor files).
- Two symbols are referenced but not defined in the kept tree, both from
  code a SCUMM game never reaches: `Audio::makeQuickTimeStream`
  (`audio/audiostream.cpp`, the ".m4a" entry of a table) and
  `Common::NEResources::getResource` (`graphics/wincursor.cpp`). A GNU ld
  link with `--gc-sections` drops them; a link without (the Mac's ld64)
  needs stubs, which `test/scumm_host.cpp` has.
- The backend has to provide an `OSystem` (screen, palette, cursor, timer,
  events, mutexes, mixer, save files, filesystem). `test/arm_stub.cpp` is
  the smallest one; `test/scumm_host.cpp` a complete one for the Mac;
  `src/backends/platform/rp2350/rp2350-minimal.cpp` frank-quest's.

## Using it

- Start a game as `test/scumm_host.cpp` does (after frank-quest's
  `frank_quest_main.cpp`): set `g_system`, `initBackend()`, register the
  ConfMan keys frank-quest's launcher sets (the engine reads
  `dump_scripts`, `filtering`, `fullscreen`, `aspect_ratio`, `gfx_mode`,
  `dimuse_tempo` among others and stops with an error if one is missing),
  `PluginManager::instance().loadAllPlugins()`, find the "scumm" engine
  plugin, `detectGames()` on the game folder's file list,
  `createInstance()`, `run()`.
- The engine draws through `copyRectToScreen()` into a 320x200 CLUT8
  screen and calls `updateScreen()` when a frame is done; palette through
  `setPalette()`, cursor through `setMouseCursor()` (CLUT8, with its own
  palette when `kFeatureCursorPalette` is on), screen shake through
  `setShakePos()`. Time: `getMillis()`, `delayMillis()`, and timer
  callbacks (iMUSE runs off one) through `DefaultTimerManager::handler()`.
  Sound: whoever owns the audio output pulls
  `Audio::MixerImpl::mixCallback()`.
- Music: with `music_driver=adlib` the game plays its AdLib arrangements
  through iMUSE and an OPL2 emulator; with `pcjr` its PCjr arrangements
  through `Player_V2` (see Changes for the tap). See `test/` for what The
  Secret of Monkey Island does with each.
- Memory on the M33 (`test/arm_size.sh`, `sizeof` with arm-none-eabi-g++):
  the `ScummEngine_v4` object alone is 55,080 bytes; static RAM (.data +
  .bss) about 16 KB; the rest is heap, which on the Mac peaked at 2.6 MB
  for Monkey Island 1 (64-bit pointers, AdLib emulation on). frank-quest
  puts the whole heap in PSRAM.
