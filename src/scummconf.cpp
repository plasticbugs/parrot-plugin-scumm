// scummconf.cpp: see scummconf.h.
#include "scummconf.h"

#include "common/config-manager.h"

void scummconf_setup(const char *domain, const char *path, const char *music_driver) {
	ConfMan.registerDefault("confirm_exit", false);
	ConfMan.registerDefault("autosave_period", 0);
	ConfMan.registerDefault("enable_unsupported_game_warning", false);
	ConfMan.registerDefault("mute", false);
	ConfMan.registerDefault("music_mute", false);
	ConfMan.registerDefault("sfx_mute", false);
	ConfMan.registerDefault("speech_mute", false);
	ConfMan.registerDefault("music_volume", 192);
	ConfMan.registerDefault("sfx_volume", 192);
	ConfMan.registerDefault("speech_volume", 192);
	ConfMan.registerDefault("subtitles", true);
	ConfMan.registerDefault("talkspeed", 60);
	ConfMan.registerDefault("midi_gain", 100);
	ConfMan.registerDefault("native_mt32", false);
	ConfMan.registerDefault("enable_gs", false);
	ConfMan.registerDefault("multi_midi", false);
	ConfMan.registerDefault("aspect_ratio", false);
	ConfMan.registerDefault("fullscreen", false);
	ConfMan.registerDefault("filtering", false);
	ConfMan.registerDefault("copy_protection", false);
	ConfMan.registerDefault("music_driver", music_driver);
	ConfMan.registerDefault("gm_device", "null");
	ConfMan.registerDefault("mt32_device", "null");
	ConfMan.registerDefault("language", "en");
	ConfMan.registerDefault("gfx_mode", "normal");
	ConfMan.registerDefault("render_mode", "");

	ConfMan.addGameDomain(domain);
	ConfMan.setActiveDomain(domain);
	ConfMan.set("path", path);
	ConfMan.set("music_driver", music_driver);
	ConfMan.setInt("autosave_period", 0);
	ConfMan.setBool("subtitles", true);
	ConfMan.set("language", "en");
	ConfMan.set("original_gui", "false");
	ConfMan.setBool("dump_scripts", false);
	ConfMan.setBool("filtering", false);
	ConfMan.setBool("fullscreen", false);
	ConfMan.setBool("aspect_ratio", false);
	ConfMan.set("gfx_mode", "normal");
	ConfMan.setInt("dimuse_tempo", 10);
	ConfMan.setBool("copy_protection", false);
	ConfMan.setBool("demo_mode", false);
	ConfMan.setBool("nosubtitles", false);
	ConfMan.setBool("confirm_exit", false);
	ConfMan.setBool("object_labels", true);
	ConfMan.setInt("boot_param", 0);
	ConfMan.setInt("save_slot", -1);
	ConfMan.setInt("tempo", 0);
	ConfMan.set("render_mode", "");
	ConfMan.set("easter_egg", "");
}
