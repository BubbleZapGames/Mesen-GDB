// Reading an installed Mesen's settings.json (mesen_settings.h). Fixtures:
// testdata/mesen/mesen2-settings.json is a Mesen2 2.0 (Linux) file, trimmed,
// with values changed to exercise each conversion; mesence-settings.json has
// what only MesenCE writes.
#include "test_harness.h"
#include "mesen_settings.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;

static std::string MesenFixture(const char* name)
{
	for(const char* dir : { "GDB/testdata/mesen/", "testdata/mesen/", "../GDB/testdata/mesen/" }) {
		std::string path = std::string(dir) + name;
		if(std::ifstream(path).good()) return path;
	}
	return std::string("GDB/testdata/mesen/") + name;
}

static bool LoadFixture(const char* name, MesenSettings& out)
{
	std::string error;
	return LoadMesenSettings(MesenFixture(name), out, error, MesenPadLayout::Linux);
}

// SDL's code for a pad button (Sdl/SdlKeyManager.cpp numbering)
static uint16_t SdlPad(int pad, int button)
{
	return (uint16_t)(0x1000 + pad * 0x100 + button);
}
enum SdlButton { SdlA = 0, SdlB = 1, SdlStart = 6, SdlSelect = 7, SdlUp = 8, SdlYPlus = 18, SdlYMinus = 19 };

TEST(mesen_settings_reads_mesen2_file)
{
	MesenSettings s;
	ASSERT_TRUE(LoadFixture("mesen2-settings.json", s));

	// Keyboard codes are the same in Mesen and mesen-gdb
	ASSERT_EQ(s.Snes.Port1.Type, ControllerType::SnesController);
	ASSERT_EQ(s.Snes.Port1.Keys.Mapping1.A, (uint16_t)62);
	ASSERT_EQ(s.Snes.Port1.Keys.Mapping1.Start, (uint16_t)6);
	// Linux Mesen's d-pad (hat 0) is SDL's
	ASSERT_EQ(s.Snes.Port1.Keys.Mapping1.Down, SdlPad(0, SdlUp + 1));
	ASSERT_EQ(s.Snes.Port2.Type, ControllerType::None);

	ASSERT_EQ(s.Snes.Region, ConsoleRegion::Pal);
	ASSERT_EQ(s.Snes.ChannelVolumes[2], (uint32_t)30);
	ASSERT_EQ(s.Snes.ChannelVolumes[0], (uint32_t)100);
	ASSERT_EQ(s.Snes.Overscan.Top, (uint32_t)7);
	ASSERT_EQ(s.Snes.SpcClockSpeedAdjustment, 40);
	// 2000-02-03 04:05:06 at +01:00, plus 01:02:03
	ASSERT_EQ(s.Snes.BsxCustomDate, (int64_t)949547106 + 3723);

	ASSERT_STR_EQ(s.Audio.AudioDevice, "Test Device");
	ASSERT_EQ(s.Audio.SampleRate, (uint32_t)44100);

	ASSERT_TRUE(std::fabs(s.Video.Brightness - 0.5) < 1e-9);
	ASSERT_TRUE(std::fabs(s.Video.LcdGridTopRightBrightness - 0.85) < 1e-9);
	ASSERT_EQ(s.Video.ScreenRotation, (uint32_t)90);
	// xBRZ is gone from this core: no filter
	ASSERT_EQ(s.Video.VideoFilter, VideoFilterType::None);

	ASSERT_EQ(s.Nes.ChannelVolumes[2], (uint32_t)40);
	ASSERT_EQ(s.Nes.ChannelVolumes[0], (uint32_t)100);
	ASSERT_EQ((int32_t)s.Nes.ChannelPanning[4], -20);
	ASSERT_FALSE(s.Nes.SpritesEnabled);
	ASSERT_TRUE(s.Nes.BackgroundEnabled);
	ASSERT_FALSE(s.Nes.IsFullColorPalette);
	ASSERT_EQ(s.Nes.UserPalette[0], (uint32_t)0xFF666666);

	ASSERT_EQ(s.SnesOverride.OverrideVideoFilter, true);
	ASSERT_EQ(s.SnesOverride.VideoFilter, VideoFilterType::NtscBlargg);

	ASSERT_EQ(s.Preferences.RewindBufferSize, (uint32_t)0);
	ASSERT_EQ(s.Preferences.AutoSaveStateDelay, (uint32_t)5);
	ASSERT_TRUE(s.Preferences.DisableGameSelectionScreen);
	// Mesen's save folder is never taken over
	ASSERT_TRUE(s.Preferences.SaveFolderOverride == nullptr);
}

TEST(mesen_settings_translates_linux_pad_codes)
{
	MesenSettings s;
	ASSERT_TRUE(LoadFixture("mesen2-settings.json", s));
	// Linux Mesen's Pad1 Up (hat, index 29) is SDL's Pad1 Up
	ASSERT_EQ(s.Snes.Port1.Keys.Mapping2.Up, SdlPad(0, SdlUp));
	// Linux Pad1 C: no SDL button, left unmapped and reported once
	ASSERT_EQ(s.Snes.Port1.Keys.Mapping2.B, (uint16_t)0);
	bool reported = false;
	for(const std::string& w : s.Warnings) reported |= w.find("Pad1 C") != std::string::npos;
	ASSERT_TRUE(reported);
}

TEST(mesen_settings_pad_layouts)
{
	// Keyboard and mouse codes pass through
	ASSERT_EQ(TranslateMesenKey(62, MesenPadLayout::Linux), (uint16_t)62);
	ASSERT_EQ(TranslateMesenKey(0x200, MesenPadLayout::Windows), (uint16_t)0x200);

	ASSERT_EQ(TranslateMesenKey(0x1000, MesenPadLayout::Linux), SdlPad(0, SdlA));
	ASSERT_EQ(TranslateMesenKey(0x110B, MesenPadLayout::Linux), SdlPad(1, SdlStart));
	ASSERT_EQ(TranslateMesenKey(0x1010, MesenPadLayout::Linux), SdlPad(0, SdlYPlus));

	// Windows XInput counts from 1, and Back is Select
	ASSERT_EQ(TranslateMesenKey(0x1001, MesenPadLayout::Windows), SdlPad(0, SdlUp));
	ASSERT_EQ(TranslateMesenKey(0x1006, MesenPadLayout::Windows), SdlPad(0, SdlSelect));
	ASSERT_EQ(TranslateMesenKey(0x100D, MesenPadLayout::Windows), SdlPad(0, SdlA));
	// Left stick up
	ASSERT_EQ(TranslateMesenKey(0x1017, MesenPadLayout::Windows), SdlPad(0, SdlYMinus));
	// DirectInput has no SDL equivalent
	ASSERT_EQ(TranslateMesenKey(0x2000, MesenPadLayout::Windows), (uint16_t)0);

	// macOS has SDL's order with Y+ up
	ASSERT_EQ(TranslateMesenKey(0x1001, MesenPadLayout::MacOS), SdlPad(0, SdlB));
	ASSERT_EQ(TranslateMesenKey(0x1012, MesenPadLayout::MacOS), SdlPad(0, SdlYMinus));

	// SDL has 8 pads
	ASSERT_EQ(TranslateMesenKey(0x1800, MesenPadLayout::Linux), (uint16_t)0);
}

TEST(mesen_settings_reads_mesence_file)
{
	MesenSettings s;
	ASSERT_TRUE(LoadFixture("mesence-settings.json", s));
	ASSERT_TRUE(s.Snes.BlendHighResolutionModes);
	// A multitap's first sub-port uses its port's mappings
	ASSERT_EQ(s.Snes.Port1SubPorts[0].Keys.Mapping1.A, (uint16_t)44);
	// A controller this core doesn't have: nothing plugged in
	ASSERT_EQ(s.Snes.Port2.Type, ControllerType::None);
	ASSERT_EQ(s.Nes.Port1.Type, ControllerType::VirtualBoyController);
	// Never configured: the UI's default keys (arrows first)
	ASSERT_EQ(s.Cv.Port1.Keys.Mapping1.CustomKeys[0], (uint16_t)24);
	ASSERT_EQ(s.Cv.ChannelVolumes[0], (uint32_t)70);
	ASSERT_EQ(s.Cv.ChannelVolumes[1], (uint32_t)100);
	ASSERT_EQ(s.Gameboy.Model, GameboyModel::AutoFavorGbc);
	ASSERT_EQ(s.Ws.AudioMode, WsAudioMode::Speakers);
	ASSERT_FALSE(s.Ws.AutoRotate);
	ASSERT_TRUE(s.Ws.LcdShowIcons);
	ASSERT_EQ(s.Ws.ControllerHorizontal.Type, ControllerType::WsController);
}

TEST(mesen_settings_missing_keys_take_ui_defaults)
{
	MesenSettings s;
	std::string error;
	ASSERT_TRUE(ParseMesenSettings("{}", s, error, MesenPadLayout::Linux));
	ASSERT_EQ(s.Nes.ChannelVolumes[10], (uint32_t)100);
	ASSERT_EQ(s.Nes.RamPowerOnState, RamState::AllZeros);
	ASSERT_EQ(s.Snes.RamPowerOnState, RamState::Random);
	ASSERT_EQ(s.Audio.ReverbStrength, (uint32_t)5);
	ASSERT_EQ(s.Input.DisplayInputPosition, InputDisplayPosition::BottomRight);
	ASSERT_TRUE(s.PcEngine.PreventSelectRunReset);
	ASSERT_EQ(s.PcEngine.Palette[1], (uint32_t)0xFF000016);
	ASSERT_EQ(s.Sms.GameGearOverscan.Left, (uint32_t)48);
	ASSERT_EQ(s.Preferences.RewindBufferSize, (uint32_t)300);
	ASSERT_EQ(s.Snes.BsxCustomDate, (int64_t)-1);
}

TEST(mesen_settings_rejects_malformed_json)
{
	MesenSettings s;
	std::string error;
	ASSERT_FALSE(ParseMesenSettings("{ \"Video\": ", s, error, MesenPadLayout::Linux));
	ASSERT_FALSE(error.empty());
	ASSERT_FALSE(LoadMesenSettings("/nonexistent/settings.json", s, error, MesenPadLayout::Linux));
}

#ifndef _WIN32
TEST(mesen_settings_finds_folder)
{
	fs::path base = fs::temp_directory_path() / ("mesen-settings-test-" + std::to_string(getpid()));
	fs::remove_all(base);
	fs::create_directories(base);
	const char* saved = getenv("XDG_CONFIG_HOME");
	std::string savedValue = saved ? saved : "";
	setenv("XDG_CONFIG_HOME", base.c_str(), 1);

	auto touch = [](const fs::path& folder) {
		fs::create_directories(folder);
		std::ofstream(folder / "settings.json") << "{}";
	};

	ASSERT_STR_EQ(FindMesenFolder(""), "");
	// A folder without settings doesn't count
	fs::create_directories(base / "MesenCE");
	ASSERT_STR_EQ(FindMesenFolder(""), "");
	touch(base / "Mesen2");
	ASSERT_STR_EQ(FindMesenFolder(""), (base / "Mesen2").string());
	touch(base / "MesenCE");
	ASSERT_STR_EQ(FindMesenFolder(""), (base / "MesenCE").string());
	// --mesen-folder wins
	ASSERT_STR_EQ(FindMesenFolder("/portable/Mesen"), "/portable/Mesen");

	if(saved) setenv("XDG_CONFIG_HOME", savedValue.c_str(), 1);
	else unsetenv("XDG_CONFIG_HOME");
	fs::remove_all(base);
}
#endif
