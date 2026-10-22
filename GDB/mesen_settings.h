#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "Core/Shared/SettingTypes.h"

class EmuSettings;
enum class RomFormat;

// The settings of an installed Mesen (Mesen2, or its MesenCE fork): its
// settings.json, read the way Mesen's UI turns it into the core's settings
// (UI/Config/*.cs ApplyConfig). Read only: mesen-gdb never writes it.
//
// Applied: video, audio, input and each console's options and controllers,
// emulation speeds, and the HUD/rewind preferences. Not applied: the Debug
// section (break options keep mesen-gdb's defaults), shortcuts, and Mesen's
// save/save state/screenshot folder overrides — mesen-gdb keeps those files
// in its own folder, so it never overwrites Mesen's.

// A console's video override (<Console>.ConfigOverrides in settings.json)
struct MesenVideoOverride
{
	bool OverrideVideoFilter = false;
	VideoFilterType VideoFilter = VideoFilterType::None;
	bool OverrideAspectRatio = false;
	VideoAspectRatio AspectRatio = VideoAspectRatio::NoStretching;
	double CustomAspectRatio = 1.0;
};

// Which Mesen frontend numbered the gamepad buttons in a settings.json: each
// OS's Mesen has its own table, and mesen-gdb's SDL one matches none of them.
enum class MesenPadLayout
{
	Linux,
	Windows,
	MacOS,
#if defined(_WIN32)
	Native = Windows
#elif defined(__APPLE__)
	Native = MacOS
#else
	Native = Linux
#endif
};

struct MesenSettings
{
	VideoConfig Video;
	AudioConfig Audio;
	InputConfig Input;
	EmulationConfig Emulation;
	SnesConfig Snes;
	NesConfig Nes;
	GameboyConfig Gameboy;
	GbaConfig Gba;
	PcEngineConfig PcEngine;
	SmsConfig Sms;
	CvConfig Cv;
	WsConfig Ws;
	PreferencesConfig Preferences;
	AudioPlayerConfig AudioPlayer;

	MesenVideoOverride SnesOverride;
	MesenVideoOverride NesOverride;
	MesenVideoOverride GameboyOverride;
	MesenVideoOverride GbaOverride;
	MesenVideoOverride PcEngineOverride;
	MesenVideoOverride SmsOverride;
	MesenVideoOverride GameGearOverride;
	MesenVideoOverride CvOverride;
	MesenVideoOverride WsOverride;

	// Audio.AudioDevice points into this
	std::string AudioDevice;

	// What could not be carried over (a gamepad button SDL has no match for)
	std::vector<std::string> Warnings;

	// Mesen's UI defaults, which are what a key missing from settings.json
	// means; they differ from the core's struct defaults in places
	MesenSettings();
};

// The folder whose settings.json to use: folderOption if given, else
// MesenCE's, else Mesen2's, as each finds its own (Documents on Windows,
// $XDG_CONFIG_HOME or ~/.config elsewhere). Empty if none has one.
std::string FindMesenFolder(const std::string& folderOption);

// Fill out from settings.json text or a file. False (out left at the
// defaults, error set) if it cannot be read or parsed.
bool ParseMesenSettings(const std::string& text, MesenSettings& out, std::string& error,
	MesenPadLayout layout = MesenPadLayout::Native);
bool LoadMesenSettings(const std::string& path, MesenSettings& out, std::string& error,
	MesenPadLayout layout = MesenPadLayout::Native);

// A key code from a settings.json written by a Mesen with this layout, as
// mesen-gdb's SDL key manager numbers it. Keyboard and mouse codes are the
// same everywhere; a gamepad button SDL has no match for gives 0.
uint16_t TranslateMesenKey(uint16_t code, MesenPadLayout layout);

// Hand the settings to the emulator (mesen_settings_apply.cpp)
void ApplyMesenSettings(EmuSettings* settings, MesenSettings& mesen);

// After a ROM loads: its console's video override, as Mesen applies it then
void ApplyMesenVideoOverride(EmuSettings* settings, const MesenSettings& mesen, ConsoleType console, RomFormat format);
