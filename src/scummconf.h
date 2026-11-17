// scummconf: ScummVM's configuration as the SCUMM plugin runs a game: the
// keys frank-quest's launcher sets (frank_quest_main.cpp, registerConfManDefaults
// and launchScummGame), and the game's own domain. The engine reads some of
// them with getBool/getInt and stops at error() on one that isn't set
// (dump_scripts: SCUMM 0.1.1 on the cart), so the cart (plugin/cart.cpp) and
// the Mac test (test/scumm_host.cpp) both set them here: what the test runs,
// the cart runs.
#pragma once

// the defaults, then `domain` made the active game domain, its folder `path`
// and its music driver
void scummconf_setup(const char *domain, const char *path, const char *music_driver);
