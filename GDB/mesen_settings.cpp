#include "mesen_settings.h"
#include "mesen_settings_defaults.h"
#include "Core/Debugger/DAP/DapJson.h"
#include "Core/Shared/KeyDefinitions.h"
#include "Utilities/magic_enum.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <type_traits>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;
using JType = JsonValue::Type;

// --- Which folder ---

// Where Mesen keeps its folder: .NET's MyDocuments on Windows, its
// ApplicationData ($XDG_CONFIG_HOME, else ~/.config) elsewhere
// (UI/Config/ConfigManager.cs DefaultDocumentsFolder)
static std::string MesenBaseFolder()
{
#ifdef _WIN32
	wchar_t path[MAX_PATH];
	if(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, path) != S_OK) return "";
	return fs::path(path).u8string();
#else
	const char* xdg = getenv("XDG_CONFIG_HOME");
	if(xdg && *xdg && fs::path(xdg).is_absolute()) return xdg;
	const char* home = getenv("HOME");
	if(!home || !*home) return "";
	return (fs::path(home) / ".config").string();
#endif
}

static bool HasSettings(const fs::path& folder)
{
	std::error_code ec;
	return fs::is_regular_file(folder / "settings.json", ec);
}

std::string FindMesenFolder(const std::string& folderOption)
{
	if(!folderOption.empty()) return folderOption;

	std::string base = MesenBaseFolder();
	if(base.empty()) return "";
	// MesenCE's own rule: its folder once it has settings, else Mesen2's
	// (MesenCE UI/Config/ConfigManager.cs HomeFolder)
	for(const char* name : { "MesenCE", "Mesen2" }) {
		fs::path folder = fs::path(base) / name;
		if(HasSettings(folder)) return folder.string();
	}
	return "";
}

// --- Key codes ---

// mesen-gdb's SDL pad buttons, by index in a code 0x1000 + pad*0x100 + index
// (Sdl/SdlKeyManager.cpp PadButtonNames)
static const char* SdlPadButtons[] = {
	"A", "B", "X", "Y", "L1", "R1", "Start", "Select",
	"Up", "Down", "Left", "Right", "L2", "R2", "L3", "R3",
	"X+", "X-", "Y+", "Y-", "X2+", "X2-", "Y2+", "Y2-"
};
static constexpr int SdlMaxPads = 8;
static constexpr uint16_t BaseGamepadIndex = 0x1000;

// Each frontend's pad buttons by index: its own name (for messages) and the
// SDL button it is, or "" if SDL has none
struct PadButton { const char* name; const char* sdl; };

// Linux/LinuxKeyManager.cpp. Y+ is down (evdev), as in SDL; the d-pad is a
// hat; Z axes, extra hats and raw axes have no SDL button.
static const std::vector<PadButton> LinuxPadButtons = {
	{ "A", "A" }, { "B", "B" }, { "C", "" }, { "X", "X" }, { "Y", "Y" }, { "Z", "" },
	{ "L1", "L1" }, { "R1", "R1" }, { "L2", "L2" }, { "R2", "R2" },
	{ "Select", "Select" }, { "Start", "Start" }, { "L3", "L3" }, { "R3", "R3" },
	{ "X+", "X+" }, { "X-", "X-" }, { "Y+", "Y+" }, { "Y-", "Y-" }, { "Z+", "" }, { "Z-", "" },
	{ "X2+", "X2+" }, { "X2-", "X2-" }, { "Y2+", "Y2+" }, { "Y2-", "Y2-" }, { "Z2+", "" }, { "Z2-", "" },
	{ "Right", "Right" }, { "Left", "Left" }, { "Down", "Down" }, { "Up", "Up" },
	{ "Right 2", "" }, { "Left 2", "" }, { "Down 2", "" }, { "Up 2", "" },
	{ "Right 3", "" }, { "Left 3", "" }, { "Down 3", "" }, { "Up 3", "" },
	{ "Right 4", "" }, { "Left 4", "" }, { "Down 4", "" }, { "Up 4", "" },
	{ "Trigger", "" }, { "Thumb", "" }, { "Thumb2", "" }, { "Top", "" }, { "Top2", "" },
	{ "Pinkie", "" }, { "Base", "" }, { "Base2", "" }, { "Base3", "" }, { "Base4", "" },
	{ "Base5", "" }, { "Base6", "" }, { "Dead", "" },
	{ "Y", "" }, { "X", "" }, { "Y2", "" }, { "X2", "" }, { "Z", "" }, { "Z2", "" }
};

// Windows/WindowsKeyManager.cpp, XInput: index 1 is the first. "LT"/"RT" are
// the left and right thumbsticks; up is SDL's minus Y. (DirectInput devices,
// from 0x2000, have no SDL equivalent.)
static const std::vector<PadButton> WindowsPadButtons = {
	{ "", "" },
	{ "Up", "Up" }, { "Down", "Down" }, { "Left", "Left" }, { "Right", "Right" },
	{ "Start", "Start" }, { "Back", "Select" },
	{ "L3", "L3" }, { "R3", "R3" }, { "L1", "L1" }, { "R1", "R1" },
	{ "?", "" }, { "?", "" },
	{ "A", "A" }, { "B", "B" }, { "X", "X" }, { "Y", "Y" },
	{ "L2", "L2" }, { "R2", "R2" },
	{ "RT Up", "Y2-" }, { "RT Down", "Y2+" }, { "RT Left", "X2-" }, { "RT Right", "X2+" },
	{ "LT Up", "Y-" }, { "LT Down", "Y+" }, { "LT Left", "X-" }, { "LT Right", "X+" },
	{ "LT Y", "" }, { "LT X", "" }, { "RT Y", "" }, { "RT X", "" }
};

// MacOS/MacOSKeyManager.mm: SDL's order, but its Y+ is up
static const std::vector<PadButton> MacPadButtons = {
	{ "A", "A" }, { "B", "B" }, { "X", "X" }, { "Y", "Y" }, { "L1", "L1" }, { "R1", "R1" },
	{ "Start", "Start" }, { "Select", "Select" },
	{ "Up", "Up" }, { "Down", "Down" }, { "Left", "Left" }, { "Right", "Right" },
	{ "L2", "L2" }, { "R2", "R2" }, { "L3", "L3" }, { "R3", "R3" },
	{ "X+", "X+" }, { "X-", "X-" }, { "Y+", "Y-" }, { "Y-", "Y+" },
	{ "X2+", "X2+" }, { "X2-", "X2-" }, { "Y2+", "Y2-" }, { "Y2-", "Y2+" },
	{ "X", "" }, { "Y", "" }, { "X2", "" }, { "Y2", "" }
};

static const std::vector<PadButton>& PadButtons(MesenPadLayout layout)
{
	switch(layout) {
		case MesenPadLayout::Windows: return WindowsPadButtons;
		case MesenPadLayout::MacOS: return MacPadButtons;
		default: return LinuxPadButtons;
	}
}

static int SdlPadButton(const char* name)
{
	for(int i = 0; i < (int)(sizeof(SdlPadButtons) / sizeof(SdlPadButtons[0])); i++) {
		if(strcmp(SdlPadButtons[i], name) == 0) return i;
	}
	return -1;
}

// TranslateMesenKey, plus the name of a button that had to be dropped
static uint16_t TranslateKey(uint16_t code, MesenPadLayout layout, std::string* dropped)
{
	if(code < BaseGamepadIndex) return code;

	int pad = (code - BaseGamepadIndex) / 0x100;
	int index = (code - BaseGamepadIndex) % 0x100;
	const std::vector<PadButton>& buttons = PadButtons(layout);
	bool directInput = layout == MesenPadLayout::Windows && code >= 0x2000;
	if(directInput || index >= (int)buttons.size()) {
		if(dropped) *dropped = "key code " + std::to_string(code);
		return 0;
	}
	const PadButton& button = buttons[index];
	int sdl = *button.sdl ? SdlPadButton(button.sdl) : -1;
	if(sdl < 0 || pad >= SdlMaxPads) {
		if(dropped) *dropped = "Pad" + std::to_string(pad + 1) + " " + button.name;
		return 0;
	}
	return (uint16_t)(BaseGamepadIndex + pad * 0x100 + sdl);
}

uint16_t TranslateMesenKey(uint16_t code, MesenPadLayout layout)
{
	return TranslateKey(code, layout, nullptr);
}

// A key by its name in KeyDefinitions.h (keyboard and mouse), or 0
static uint16_t KeyCode(const char* name)
{
	static const std::map<std::string, uint16_t> codes = []() {
		std::map<std::string, uint16_t> m;
		for(const KeyDefinition& def : KeyDefinition::GetSharedKeyDefinitions()) {
			if(!def.name.empty() && !m.count(def.name)) m[def.name] = (uint16_t)def.keyCode;
		}
		return m;
	}();
	auto it = codes.find(name);
	return it == codes.end() ? 0 : it->second;
}

// --- Reading JSON ---

namespace {

// One settings.json being read
class Reader
{
public:
	Reader(MesenSettings& out, MesenPadLayout layout) : _out(out), _layout(layout) {}

	void Read(const JsonValue& root);

private:
	MesenSettings& _out;
	MesenPadLayout _layout;
	std::set<std::string> _dropped;

	template<typename T>
	static void Num(const JsonValue& obj, const char* name, T& field)
	{
		const JsonValue& v = obj.Get(name);
		if(v.GetType() != JType::Number) return;
		if constexpr(std::is_floating_point_v<T>) {
			field = (T)v.GetNumber();
		} else {
			field = (T)(int64_t)v.GetNumber();
		}
	}

	static void Bool(const JsonValue& obj, const char* name, bool& field)
	{
		const JsonValue& v = obj.Get(name);
		if(v.GetType() == JType::Bool) field = v.GetBool();
	}

	// An int percentage in the UI, a fraction in the core
	static void Percent(const JsonValue& obj, const char* name, double& field)
	{
		const JsonValue& v = obj.Get(name);
		if(v.GetType() == JType::Number) field = v.GetNumber() / 100.0;
	}

	// Enums are written by name; a name this core lacks keeps the default
	template<typename E>
	static void Enum(const JsonValue& obj, const char* name, E& field,
		std::initializer_list<std::pair<const char*, E>> aliases = {})
	{
		const JsonValue& v = obj.Get(name);
		if(v.GetType() == JType::Number) {
			auto value = magic_enum::enum_cast<E>((int)v.GetNumber());
			if(value) field = *value;
		} else if(v.GetType() == JType::String) {
			for(auto& [alias, value] : aliases) {
				if(v.GetString() == alias) {
					field = value;
					return;
				}
			}
			auto value = magic_enum::enum_cast<E>(v.GetString());
			if(value) field = *value;
		}
	}

	template<typename T, size_t N>
	static void Array(const JsonValue& obj, const char* name, T (&field)[N], size_t* count = nullptr)
	{
		const JsonValue& v = obj.Get(name);
		if(v.GetType() != JType::Array) return;
		const std::vector<JsonValue>& items = v.GetArray();
		size_t n = std::min(items.size(), N);
		for(size_t i = 0; i < n; i++) {
			field[i] = (T)(int64_t)items[i].GetNumber();
		}
		if(count) *count = items.size();
	}

	// Fields named <prefix>1..N<suffix> (Channel1Vol...) into an array
	template<typename T, size_t N>
	static void Numbered(const JsonValue& obj, const char* prefix, const char* suffix, T (&field)[N])
	{
		for(size_t i = 0; i < N; i++) {
			std::string name = prefix + std::to_string(i + 1) + suffix;
			if constexpr(std::is_same_v<T, bool>) {
				Bool(obj, name.c_str(), field[i]);
			} else {
				Num(obj, name.c_str(), field[i]);
			}
		}
	}

	static void Overscan(const JsonValue& obj, const char* name, OverscanDimensions& field)
	{
		const JsonValue& v = obj.Get(name);
		if(v.GetType() != JType::Object) return;
		Num(v, "Left", field.Left);
		Num(v, "Right", field.Right);
		Num(v, "Top", field.Top);
		Num(v, "Bottom", field.Bottom);
	}

	static void VideoOverride(const JsonValue& obj, const char* name, MesenVideoOverride& field);

	// A key code, as SDL numbers it (0 if it has none)
	uint16_t Translate(const JsonValue& v);
	uint16_t Key(const JsonValue& obj, const char* name) { return Translate(obj.Get(name)); }
	void Mapping(const JsonValue& obj, KeyMapping& mapping, ControllerType type, int index);
	void Controller(const JsonValue& obj, const char* name, ControllerConfig& config);
	// MesenCE has a multitap's first sub-port use its port's mappings
	static void InheritKeys(ControllerConfig& subPort, const ControllerConfig& port);

	void Video(const JsonValue& v);
	void Audio(const JsonValue& v);
	void Input(const JsonValue& v);
	void Snes(const JsonValue& v);
	void Nes(const JsonValue& v);
	void Gameboy(const JsonValue& v);
	void Gba(const JsonValue& v);
	void PcEngine(const JsonValue& v);
	void Sms(const JsonValue& v);
	void Cv(const JsonValue& v);
	void Ws(const JsonValue& v);
	void Preferences(const JsonValue& v);
};

void Reader::VideoOverride(const JsonValue& obj, const char* name, MesenVideoOverride& field)
{
	const JsonValue& v = obj.Get(name);
	Bool(v, "OverrideVideoFilter", field.OverrideVideoFilter);
	Enum(v, "VideoFilter", field.VideoFilter);
	Bool(v, "OverrideAspectRatio", field.OverrideAspectRatio);
	Enum(v, "AspectRatio", field.AspectRatio);
	Num(v, "CustomAspectRatio", field.CustomAspectRatio);
}

uint16_t Reader::Translate(const JsonValue& v)
{
	if(v.GetType() != JType::Number) return 0;
	std::string dropped;
	uint16_t code = TranslateKey((uint16_t)v.GetNumber(), _layout, &dropped);
	if(!dropped.empty() && _dropped.insert(dropped).second) {
		_out.Warnings.push_back(dropped + " has no match in mesen-gdb's gamepad support; left unmapped");
	}
	return code;
}

// The *Buttons array a controller type keeps its custom buttons in
// (GetCustomButtons in Nes/Snes/Sms/CvControllerConfig.cs)
static const char* CustomButtonsName(ControllerType type)
{
	switch(type) {
		case ControllerType::PowerPadSideA:
		case ControllerType::PowerPadSideB:
		case ControllerType::FamilyTrainerMatSideA:
		case ControllerType::FamilyTrainerMatSideB: return "PowerPadButtons";
		case ControllerType::FamilyBasicKeyboard: return "FamilyBasicKeyboardButtons";
		case ControllerType::PartyTap: return "PartyTapButtons";
		case ControllerType::Pachinko: return "PachinkoButtons";
		case ControllerType::ExcitingBoxing: return "ExcitingBoxingButtons";
		case ControllerType::JissenMahjong: return "JissenMahjongButtons";
		case ControllerType::SuborKeyboard: return "SuborKeyboardButtons";
		case ControllerType::VirtualBoyController: return "VirtualBoyButtons";
		case ControllerType::KonamiHyperShot: return "KonamiHyperShotButtons";
		case ControllerType::FamicomArkanoidController:
		case ControllerType::NesArkanoidController: return "ArkanoidButtons";
		case ControllerType::FamicomZapper:
		case ControllerType::NesZapper: return "ZapperButtons";
		case ControllerType::SnesMouse:
		case ControllerType::SuborMouse: return "MouseButtons";
		case ControllerType::OekaKidsTablet: return "OekakidsButtons";
		case ControllerType::BandaiHyperShot: return "BandaiHypershotButtons";
		case ControllerType::BandaiMicrophone: return "BandaiMicrophoneButtons";
		case ControllerType::SuperScope: return "SuperScopeButtons";
		case ControllerType::SmsLightPhaser: return "LightPhaserButtons";
		case ControllerType::ColecoVisionController: return "ColecoVisionControllerButtons";
		default: return nullptr;
	}
}

// KeyMapping.ToInterop (UI/Config/InputConfig.cs)
void Reader::Mapping(const JsonValue& obj, KeyMapping& mapping, ControllerType type, int index)
{
	if(obj.GetType() != JType::Object) return;
	mapping.A = Key(obj, "A");
	mapping.B = Key(obj, "B");
	mapping.X = Key(obj, "X");
	mapping.Y = Key(obj, "Y");
	mapping.L = Key(obj, "L");
	mapping.R = Key(obj, "R");
	mapping.U = Key(obj, "U");
	mapping.D = Key(obj, "D");
	mapping.Up = Key(obj, "Up");
	mapping.Down = Key(obj, "Down");
	mapping.Left = Key(obj, "Left");
	mapping.Right = Key(obj, "Right");
	mapping.Select = Key(obj, "Select");
	mapping.Start = Key(obj, "Start");
	mapping.TurboA = Key(obj, "TurboA");
	mapping.TurboB = Key(obj, "TurboB");
	mapping.TurboX = Key(obj, "TurboX");
	mapping.TurboY = Key(obj, "TurboY");
	mapping.TurboL = Key(obj, "TurboL");
	mapping.TurboR = Key(obj, "TurboR");
	mapping.TurboSelect = Key(obj, "TurboSelect");
	mapping.TurboStart = Key(obj, "TurboStart");
	mapping.GenericKey1 = Key(obj, "GenericKey1");

	const char* buttonsName = CustomButtonsName(type);
	if(!buttonsName) return;
	const JsonValue& buttons = obj.Get(buttonsName);
	if(buttons.GetType() == JType::Array) {
		const std::vector<JsonValue>& items = buttons.GetArray();
		for(size_t i = 0; i < items.size() && i < 100; i++) {
			mapping.CustomKeys[i] = Translate(items[i]);
		}
	} else if(index == 0) {
		// Never configured: the type's defaults, as the UI fills them in
		for(const DefaultCustomKeys& entry : GetDefaultCustomKeyTable()) {
			if(entry.Type != type) continue;
			for(size_t i = 0; i < entry.Keys.size() && i < 100; i++) {
				mapping.CustomKeys[i] = KeyCode(entry.Keys[i]);
			}
			break;
		}
	}
}

// ControllerConfig.ToInterop (UI/Config/InputConfig.cs)
void Reader::Controller(const JsonValue& obj, const char* name, ControllerConfig& config)
{
	const JsonValue& v = obj.Get(name);
	if(v.GetType() != JType::Object) return;
	Enum(v, "Type", config.Type, { { "VbController", ControllerType::VirtualBoyController } });
	Num(v, "TurboSpeed", config.Keys.TurboSpeed);
	Mapping(v.Get("Mapping1"), config.Keys.Mapping1, config.Type, 0);
	Mapping(v.Get("Mapping2"), config.Keys.Mapping2, config.Type, 1);
	Mapping(v.Get("Mapping3"), config.Keys.Mapping3, config.Type, 2);
	Mapping(v.Get("Mapping4"), config.Keys.Mapping4, config.Type, 3);
}

void Reader::InheritKeys(ControllerConfig& subPort, const ControllerConfig& port)
{
	if(subPort.Keys.GetKeyMappingArray().empty()) {
		uint32_t turbo = subPort.Keys.TurboSpeed;
		subPort.Keys = port.Keys;
		if(turbo) subPort.Keys.TurboSpeed = turbo;
	}
}

// VideoConfig.ApplyConfig
void Reader::Video(const JsonValue& v)
{
	VideoConfig& c = _out.Video;
	Num(v, "CustomAspectRatio", c.CustomAspectRatio);
	// xBRZ and HQ filters were removed from this core: no filter instead
	Enum(v, "VideoFilter", c.VideoFilter);
	Enum(v, "AspectRatio", c.AspectRatio);
	Bool(v, "UseBilinearInterpolation", c.UseBilinearInterpolation);
	Bool(v, "UseSrgbTextureFormat", c.UseSrgbTextureFormat);
	Bool(v, "VerticalSync", c.VerticalSync);
	Bool(v, "IntegerFpsMode", c.IntegerFpsMode);
	Percent(v, "Brightness", c.Brightness);
	Percent(v, "Contrast", c.Contrast);
	Percent(v, "Hue", c.Hue);
	Percent(v, "Saturation", c.Saturation);
	Percent(v, "ScanlineIntensity", c.ScanlineIntensity);
	Percent(v, "LcdGridTopLeftBrightness", c.LcdGridTopLeftBrightness);
	Percent(v, "LcdGridTopRightBrightness", c.LcdGridTopRightBrightness);
	Percent(v, "LcdGridBottomLeftBrightness", c.LcdGridBottomLeftBrightness);
	Percent(v, "LcdGridBottomRightBrightness", c.LcdGridBottomRightBrightness);
	Percent(v, "NtscArtifacts", c.NtscArtifacts);
	Percent(v, "NtscBleed", c.NtscBleed);
	Percent(v, "NtscFringing", c.NtscFringing);
	Percent(v, "NtscGamma", c.NtscGamma);
	Percent(v, "NtscResolution", c.NtscResolution);
	Percent(v, "NtscSharpness", c.NtscSharpness);
	Bool(v, "NtscMergeFields", c.NtscMergeFields);
	Enum(v, "NtscScale", c.NtscScale);
	Percent(v, "NtscYFilterLength", c.NtscYFilterLength);
	Percent(v, "NtscIFilterLength", c.NtscIFilterLength);
	Percent(v, "NtscQFilterLength", c.NtscQFilterLength);
	Bool(v, "FullscreenForceIntegerScale", c.FullscreenForceIntegerScale);
	Bool(v, "UseExclusiveFullscreen", c.UseExclusiveFullscreen);
	Num(v, "ExclusiveFullscreenRefreshRateNtsc", c.ExclusiveFullscreenRefreshRateNtsc);
	Num(v, "ExclusiveFullscreenRefreshRatePal", c.ExclusiveFullscreenRefreshRatePal);
	// "_90Degrees" and so on
	const JsonValue& rotation = v.Get("ScreenRotation");
	if(rotation.GetType() == JType::Number) {
		c.ScreenRotation = (uint32_t)rotation.GetNumber();
	} else if(rotation.GetType() == JType::String) {
		c.ScreenRotation = (uint32_t)strtoul(rotation.GetString().c_str() + (rotation.GetString()[0] == '_' ? 1 : 0), nullptr, 10);
	}
}

// AudioConfig.ApplyConfig
void Reader::Audio(const JsonValue& v)
{
	AudioConfig& c = _out.Audio;
	if(v.Get("AudioDevice").GetType() == JType::String) _out.AudioDevice = v.Get("AudioDevice").GetString();
	Bool(v, "EnableAudio", c.EnableAudio);
	Bool(v, "DisableDynamicSampleRate", c.DisableDynamicSampleRate);
	Num(v, "MasterVolume", c.MasterVolume);
	// "_48000"
	const JsonValue& rate = v.Get("SampleRate");
	if(rate.GetType() == JType::Number) {
		c.SampleRate = (uint32_t)rate.GetNumber();
	} else if(rate.GetType() == JType::String) {
		uint32_t value = (uint32_t)strtoul(rate.GetString().c_str() + (rate.GetString()[0] == '_' ? 1 : 0), nullptr, 10);
		if(value) c.SampleRate = value;
	}
	Num(v, "AudioLatency", c.AudioLatency);
	Bool(v, "MuteSoundInBackground", c.MuteSoundInBackground);
	Bool(v, "ReduceSoundInBackground", c.ReduceSoundInBackground);
	Bool(v, "ReduceSoundInFastForward", c.ReduceSoundInFastForward);
	Num(v, "VolumeReduction", c.VolumeReduction);
	Bool(v, "ReverbEnabled", c.ReverbEnabled);
	Num(v, "ReverbStrength", c.ReverbStrength);
	Num(v, "ReverbDelay", c.ReverbDelay);
	Bool(v, "CrossFeedEnabled", c.CrossFeedEnabled);
	Num(v, "CrossFeedRatio", c.CrossFeedRatio);
	Bool(v, "EnableEqualizer", c.EnableEqualizer);
	double* bands[20] = {
		&c.Band1Gain, &c.Band2Gain, &c.Band3Gain, &c.Band4Gain, &c.Band5Gain,
		&c.Band6Gain, &c.Band7Gain, &c.Band8Gain, &c.Band9Gain, &c.Band10Gain,
		&c.Band11Gain, &c.Band12Gain, &c.Band13Gain, &c.Band14Gain, &c.Band15Gain,
		&c.Band16Gain, &c.Band17Gain, &c.Band18Gain, &c.Band19Gain, &c.Band20Gain
	};
	for(int i = 0; i < 20; i++) {
		Num(v, ("Band" + std::to_string(i + 1) + "Gain").c_str(), *bands[i]);
	}
	Bool(v, "AudioPlayerEnableTrackLength", c.AudioPlayerEnableTrackLength);
	Num(v, "AudioPlayerTrackLength", c.AudioPlayerTrackLength);
	Bool(v, "AudioPlayerAutoDetectSilence", c.AudioPlayerAutoDetectSilence);
	Num(v, "AudioPlayerSilenceDelay", c.AudioPlayerSilenceDelay);
}

// InputConfig.ApplyConfig
void Reader::Input(const JsonValue& v)
{
	InputConfig& c = _out.Input;
	Num(v, "ControllerDeadzoneSize", c.ControllerDeadzoneSize);
	Num(v, "MouseSensitivity", c.MouseSensitivity);
	Enum(v, "DisplayInputPosition", c.DisplayInputPosition);
	Numbered(v, "DisplayInputPort", "", c.DisplayInputPort);
	Bool(v, "DisplayInputHorizontally", c.DisplayInputHorizontally);
	const JsonValue& feedback = v.Get("ForceFeedbackIntensity");
	if(feedback.GetType() == JType::Number) {
		c.ForceFeedbackIntensity = std::clamp(0.1 * feedback.GetNumber(), 0.0, 1.0);
	}
}

// Days since 1970-01-01 of a proleptic Gregorian date
static int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d)
{
	y -= m <= 2;
	int64_t era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned)(y - era * 400);
	unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int64_t)doe - 719468;
}

// A DateTimeOffset ("1995-01-01T00:00:00+00:00") as Unix seconds
static bool ParseDateTimeOffset(const std::string& s, int64_t& out)
{
	int y, mo, d, h, mi, sec;
	if(sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6) return false;
	int64_t t = DaysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + sec;
	// The offset, after any fraction of a second
	size_t zone = s.find_first_of("+-Z", 19);
	if(zone != std::string::npos && s[zone] != 'Z') {
		int oh = 0, om = 0;
		sscanf(s.c_str() + zone + 1, "%d:%d", &oh, &om);
		int64_t offset = oh * 3600 + om * 60;
		t -= s[zone] == '+' ? offset : -offset;
	}
	out = t;
	return true;
}

// A TimeSpan ("hh:mm:ss", or "d.hh:mm:ss") as seconds
static int64_t ParseTimeSpan(const std::string& s)
{
	int64_t days = 0;
	int h = 0, m = 0, sec = 0;
	const char* p = s.c_str();
	const char* dot = strchr(p, '.');
	const char* colon = strchr(p, ':');
	if(dot && colon && dot < colon) {
		days = strtoll(p, nullptr, 10);
		p = dot + 1;
	}
	sscanf(p, "%d:%d:%d", &h, &m, &sec);
	return days * 86400 + h * 3600 + m * 60 + sec;
}

// SnesConfig.ApplyConfig
void Reader::Snes(const JsonValue& v)
{
	SnesConfig& c = _out.Snes;
	VideoOverride(v, "ConfigOverrides", _out.SnesOverride);
	Controller(v, "Port1", c.Port1);
	Controller(v, "Port2", c.Port2);
	const char* subPorts[4] = { "A", "B", "C", "D" };
	for(int i = 0; i < 4; i++) {
		Controller(v, (std::string("Port1") + subPorts[i]).c_str(), c.Port1SubPorts[i]);
		Controller(v, (std::string("Port2") + subPorts[i]).c_str(), c.Port2SubPorts[i]);
	}
	InheritKeys(c.Port1SubPorts[0], c.Port1);
	InheritKeys(c.Port2SubPorts[0], c.Port2);
	Bool(v, "AllowInvalidInput", c.AllowInvalidInput);
	Enum(v, "Region", c.Region);
	Bool(v, "BlendHighResolutionModes", c.BlendHighResolutionModes);
	// MesenCE replaced it with a choice of blend
	const JsonValue& blend = v.Get("HighResBlendMode");
	if(blend.GetType() == JType::String) c.BlendHighResolutionModes = blend.GetString() != "None";
	Bool(v, "HideBgLayer1", c.HideBgLayer1);
	Bool(v, "HideBgLayer2", c.HideBgLayer2);
	Bool(v, "HideBgLayer3", c.HideBgLayer3);
	Bool(v, "HideBgLayer4", c.HideBgLayer4);
	Bool(v, "HideSprites", c.HideSprites);
	Bool(v, "DisableFrameSkipping", c.DisableFrameSkipping);
	Bool(v, "ForceFixedResolution", c.ForceFixedResolution);
	Overscan(v, "Overscan", c.Overscan);
	Enum(v, "InterpolationType", c.InterpolationType);
	Numbered(v, "Channel", "Vol", c.ChannelVolumes);
	Bool(v, "EnableRandomPowerOnState", c.EnableRandomPowerOnState);
	Bool(v, "EnableStrictBoardMappings", c.EnableStrictBoardMappings);
	Enum(v, "RamPowerOnState", c.RamPowerOnState);
	Num(v, "SpcClockSpeedAdjustment", c.SpcClockSpeedAdjustment);
	Num(v, "PpuExtraScanlinesBeforeNmi", c.PpuExtraScanlinesBeforeNmi);
	Num(v, "PpuExtraScanlinesAfterNmi", c.PpuExtraScanlinesAfterNmi);
	Num(v, "GsuClockSpeed", c.GsuClockSpeed);

	bool customTime = false;
	Bool(v, "BsxUseCustomTime", customTime);
	c.BsxCustomDate = -1;
	if(customTime) {
		// The UI's default date, 1995-01-01
		int64_t date = 788918400;
		if(v.Get("BsxCustomDate").GetType() == JType::String) ParseDateTimeOffset(v.Get("BsxCustomDate").GetString(), date);
		int64_t time = 0;
		if(v.Get("BsxCustomTime").GetType() == JType::String) time = ParseTimeSpan(v.Get("BsxCustomTime").GetString());
		c.BsxCustomDate = date + time;
	}
}

// NesConfig.ApplyConfig
void Reader::Nes(const JsonValue& v)
{
	NesConfig& c = _out.Nes;
	VideoOverride(v, "ConfigOverrides", _out.NesOverride);
	Controller(v, "Port1", c.Port1);
	Controller(v, "Port2", c.Port2);
	Controller(v, "ExpPort", c.ExpPort);
	const char* subPorts[4] = { "A", "B", "C", "D" };
	for(int i = 0; i < 4; i++) {
		Controller(v, (std::string("Port1") + subPorts[i]).c_str(), c.Port1SubPorts[i]);
		Controller(v, (std::string("ExpPort") + subPorts[i]).c_str(), c.ExpPortSubPorts[i]);
	}
	InheritKeys(c.Port1SubPorts[0], c.Port1);
	InheritKeys(c.ExpPortSubPorts[0], c.ExpPort);
	Controller(v, "MapperInput", c.MapperInput);

	Num(v, "LightDetectionRadius", c.LightDetectionRadius);
	Bool(v, "AutoConfigureInput", c.AutoConfigureInput);
	Enum(v, "Region", c.Region);
	Bool(v, "EnableHdPacks", c.EnableHdPacks);
	Bool(v, "DisableGameDatabase", c.DisableGameDatabase);
	Bool(v, "FdsAutoLoadDisk", c.FdsAutoLoadDisk);
	Bool(v, "FdsFastForwardOnLoad", c.FdsFastForwardOnLoad);
	Bool(v, "FdsAutoInsertDisk", c.FdsAutoInsertDisk);
	Enum(v, "VsDualVideoOutput", c.VsDualVideoOutput);
	Enum(v, "VsDualAudioOutput", c.VsDualAudioOutput);

	bool disableSprites = !c.SpritesEnabled;
	bool disableBackground = !c.BackgroundEnabled;
	Bool(v, "DisableSprites", disableSprites);
	Bool(v, "DisableBackground", disableBackground);
	c.SpritesEnabled = !disableSprites;
	c.BackgroundEnabled = !disableBackground;

	Bool(v, "ForceBackgroundFirstColumn", c.ForceBackgroundFirstColumn);
	Bool(v, "ForceSpritesFirstColumn", c.ForceSpritesFirstColumn);
	Bool(v, "RemoveSpriteLimit", c.RemoveSpriteLimit);
	Bool(v, "AdaptiveSpriteLimit", c.AdaptiveSpriteLimit);
	Bool(v, "EnablePalBorders", c.EnablePalBorders);
	Bool(v, "UseCustomVsPalette", c.UseCustomVsPalette);
	Overscan(v, "NtscOverscan", c.NtscOverscan);
	Overscan(v, "PalOverscan", c.PalOverscan);

	Enum(v, "ConsoleType", c.ConsoleType);
	Bool(v, "DisablePpuReset", c.DisablePpuReset);
	Bool(v, "AllowInvalidInput", c.AllowInvalidInput);
	Bool(v, "DisableGameGenieBusConflicts", c.DisableGameGenieBusConflicts);
	Bool(v, "DisableFlashSaves", c.DisableFlashSaves);
	Bool(v, "OverwriteOriginalRom", c.OverwriteOriginalRom);
	Bool(v, "EnableOamDecay", c.EnableOamDecay);
	Bool(v, "EnablePpuOamRowCorruption", c.EnablePpuOamRowCorruption);
	Bool(v, "EnablePpuSpriteEvalBug", c.EnablePpuSpriteEvalBug);
	Bool(v, "DisableOamAddrBug", c.DisableOamAddrBug);
	Bool(v, "DisablePaletteRead", c.DisablePaletteRead);
	Bool(v, "DisablePpu2004Reads", c.DisablePpu2004Reads);
	Bool(v, "EnablePpu2000ScrollGlitch", c.EnablePpu2000ScrollGlitch);
	Bool(v, "EnablePpu2006ScrollGlitch", c.EnablePpu2006ScrollGlitch);
	Bool(v, "RestrictPpuAccessOnFirstFrame", c.RestrictPpuAccessOnFirstFrame);
	Bool(v, "EnableDmcSampleDuplicationGlitch", c.EnableDmcSampleDuplicationGlitch);
	Bool(v, "EnableCpuTestMode", c.EnableCpuTestMode);
	Bool(v, "RandomizeMapperPowerOnState", c.RandomizeMapperPowerOnState);
	Bool(v, "RandomizeCpuPpuAlignment", c.RandomizeCpuPpuAlignment);
	Enum(v, "RamPowerOnState", c.RamPowerOnState);
	Num(v, "PpuExtraScanlinesBeforeNmi", c.PpuExtraScanlinesBeforeNmi);
	Num(v, "PpuExtraScanlinesAfterNmi", c.PpuExtraScanlinesAfterNmi);
	Bool(v, "DisableNoiseModeFlag", c.DisableNoiseModeFlag);
	Bool(v, "ReduceDmcPopping", c.ReduceDmcPopping);
	Bool(v, "SilenceTriangleHighFreq", c.SilenceTriangleHighFreq);
	Bool(v, "SwapDutyCycles", c.SwapDutyCycles);
	Bool(v, "ReverseDpcmBitOrder", c.ReverseDpcmBitOrder);
	// BreakOnCrash is a debugger option: not applied

	// The core's channel arrays, in InteropNesConfig's order
	const char* channels[11] = {
		"Square1", "Square2", "Triangle", "Noise", "Dmc", "Fds", "Mmc5", "Vrc6", "Vrc7", "Namco163", "Sunsoft5b"
	};
	for(int i = 0; i < 11; i++) {
		Num(v, (std::string(channels[i]) + "Volume").c_str(), c.ChannelVolumes[i]);
		int32_t panning = (int32_t)c.ChannelPanning[i];
		Num(v, (std::string(channels[i]) + "Panning").c_str(), panning);
		c.ChannelPanning[i] = (uint32_t)panning;
	}
	Num(v, "EpsmVolume", c.EpsmVolume);
	Enum(v, "StereoFilter", c.StereoFilter);
	Num(v, "StereoDelay", c.StereoDelay);
	Num(v, "StereoPanningAngle", c.StereoPanningAngle);
	Num(v, "StereoCombFilterDelay", c.StereoCombFilterDelay);
	Num(v, "StereoCombFilterStrength", c.StereoCombFilterStrength);
	Num(v, "InputScanline", c.InputScanline);

	// 64 colors, or 512 for a full palette with every emphasis combination
	size_t colors = 0;
	uint32_t palette[512] = {};
	Array(v, "UserPalette", palette, &colors);
	if(colors == 64 || colors == 512) {
		std::copy(palette, palette + 512, c.UserPalette);
		c.IsFullColorPalette = colors == 512;
	}
}

// GameboyConfig.ApplyConfig
void Reader::Gameboy(const JsonValue& v)
{
	GameboyConfig& c = _out.Gameboy;
	VideoOverride(v, "ConfigOverrides", _out.GameboyOverride);
	Controller(v, "Controller", c.Controller);
	Enum(v, "Model", c.Model);
	Bool(v, "UseSgb2", c.UseSgb2);
	Bool(v, "BlendFrames", c.BlendFrames);
	Bool(v, "GbcAdjustColors", c.GbcAdjustColors);
	Bool(v, "DisableBackground", c.DisableBackground);
	Bool(v, "DisableSprites", c.DisableSprites);
	Bool(v, "HideSgbBorders", c.HideSgbBorders);
	Enum(v, "RamPowerOnState", c.RamPowerOnState);
	Bool(v, "AllowInvalidInput", c.AllowInvalidInput);
	Array(v, "BgColors", c.BgColors);
	Array(v, "Obj0Colors", c.Obj0Colors);
	Array(v, "Obj1Colors", c.Obj1Colors);
	Num(v, "Square1Vol", c.Square1Vol);
	Num(v, "Square2Vol", c.Square2Vol);
	Num(v, "NoiseVol", c.NoiseVol);
	Num(v, "WaveVol", c.WaveVol);
}

// GbaConfig.ApplyConfig
void Reader::Gba(const JsonValue& v)
{
	GbaConfig& c = _out.Gba;
	VideoOverride(v, "ConfigOverrides", _out.GbaOverride);
	Controller(v, "Controller", c.Controller);
	Bool(v, "SkipBootScreen", c.SkipBootScreen);
	Bool(v, "DisableFrameSkipping", c.DisableFrameSkipping);
	Bool(v, "BlendFrames", c.BlendFrames);
	Bool(v, "GbaAdjustColors", c.GbaAdjustColors);
	Numbered(v, "HideBgLayer", "", c.HideBgLayers);
	Bool(v, "DisableSprites", c.DisableSprites);
	Num(v, "OverclockScanlineCount", c.OverclockScanlineCount);
	Enum(v, "RamPowerOnState", c.RamPowerOnState);
	Enum(v, "SaveType", c.SaveType);
	Enum(v, "RtcType", c.RtcType);
	Bool(v, "AllowInvalidInput", c.AllowInvalidInput);
	Bool(v, "EnableMgbaLogApi", c.EnableMgbaLogApi);
	Num(v, "ChannelAVol", c.ChannelAVol);
	Num(v, "ChannelBVol", c.ChannelBVol);
	Num(v, "Square1Vol", c.Square1Vol);
	Num(v, "Square2Vol", c.Square2Vol);
	Num(v, "NoiseVol", c.NoiseVol);
	Num(v, "WaveVol", c.WaveVol);
}

// PcEngineConfig.ApplyConfig
void Reader::PcEngine(const JsonValue& v)
{
	PcEngineConfig& c = _out.PcEngine;
	VideoOverride(v, "ConfigOverrides", _out.PcEngineOverride);
	Controller(v, "Port1", c.Port1);
	const char* subPorts[5] = { "A", "B", "C", "D", "E" };
	for(int i = 0; i < 5; i++) {
		Controller(v, (std::string("Port1") + subPorts[i]).c_str(), c.Port1SubPorts[i]);
	}
	InheritKeys(c.Port1SubPorts[0], c.Port1);
	Bool(v, "AllowInvalidInput", c.AllowInvalidInput);
	Bool(v, "PreventSelectRunReset", c.PreventSelectRunReset);
	Enum(v, "ConsoleType", c.ConsoleType);
	Enum(v, "CdRomType", c.CdRomType);
	Bool(v, "EnableCdRomForHuCardGames", c.EnableCdRomForHuCardGames);
	Bool(v, "DisableCdRomSaveRamForHuCardGames", c.DisableCdRomSaveRamForHuCardGames);
	Enum(v, "RamPowerOnState", c.RamPowerOnState);
	Bool(v, "EnableRandomPowerOnState", c.EnableRandomPowerOnState);
	Numbered(v, "Channel", "Vol", c.ChannelVol);
	Num(v, "CdAudioVolume", c.CdAudioVolume);
	Num(v, "AdpcmVolume", c.AdpcmVolume);
	Bool(v, "UseHuC6280aAudio", c.UseHuC6280aAudio);
	Bool(v, "RemoveSpriteLimit", c.RemoveSpriteLimit);
	Bool(v, "DisableSprites", c.DisableSprites);
	Bool(v, "DisableSpritesVdc2", c.DisableSpritesVdc2);
	Bool(v, "DisableBackground", c.DisableBackground);
	Bool(v, "DisableBackgroundVdc2", c.DisableBackgroundVdc2);
	Bool(v, "DisableFrameSkipping", c.DisableFrameSkipping);
	Bool(v, "ForceFixedResolution", c.ForceFixedResolution);
	Overscan(v, "Overscan", c.Overscan);
	Array(v, "Palette", c.Palette);
}

// SmsConfig.ApplyConfig
void Reader::Sms(const JsonValue& v)
{
	SmsConfig& c = _out.Sms;
	VideoOverride(v, "ConfigOverrides", _out.SmsOverride);
	VideoOverride(v, "GgConfigOverrides", _out.GameGearOverride);
	Controller(v, "Port1", c.Port1);
	Controller(v, "Port2", c.Port2);
	Bool(v, "AllowInvalidInput", c.AllowInvalidInput);
	Enum(v, "Region", c.Region);
	Enum(v, "GameGearRegion", c.GameGearRegion);
	Enum(v, "RamPowerOnState", c.RamPowerOnState);
	Enum(v, "Revision", c.Revision);
	Bool(v, "UseSgPalette", c.UseSgPalette);
	Bool(v, "GgBlendFrames", c.GgBlendFrames);
	Bool(v, "RemoveSpriteLimit", c.RemoveSpriteLimit);
	Bool(v, "DisableSprites", c.DisableSprites);
	Bool(v, "DisableBackground", c.DisableBackground);
	Num(v, "Tone1Vol", c.ChannelVolumes[0]);
	Num(v, "Tone2Vol", c.ChannelVolumes[1]);
	Num(v, "Tone3Vol", c.ChannelVolumes[2]);
	Num(v, "NoiseVol", c.ChannelVolumes[3]);
	Num(v, "FmAudioVolume", c.FmAudioVolume);
	Bool(v, "EnableFmAudio", c.EnableFmAudio);
	Overscan(v, "NtscOverscan", c.NtscOverscan);
	Overscan(v, "PalOverscan", c.PalOverscan);
	Overscan(v, "GameGearOverscan", c.GameGearOverscan);
}

// CvConfig.ApplyConfig
void Reader::Cv(const JsonValue& v)
{
	CvConfig& c = _out.Cv;
	VideoOverride(v, "ConfigOverrides", _out.CvOverride);
	Controller(v, "Port1", c.Port1);
	Controller(v, "Port2", c.Port2);
	Enum(v, "Region", c.Region);
	Enum(v, "RamPowerOnState", c.RamPowerOnState);
	Bool(v, "RemoveSpriteLimit", c.RemoveSpriteLimit);
	Bool(v, "DisableSprites", c.DisableSprites);
	Bool(v, "DisableBackground", c.DisableBackground);
	Num(v, "Tone1Vol", c.ChannelVolumes[0]);
	Num(v, "Tone2Vol", c.ChannelVolumes[1]);
	Num(v, "Tone3Vol", c.ChannelVolumes[2]);
	Num(v, "NoiseVol", c.ChannelVolumes[3]);
}

// WsConfig.ApplyConfig
void Reader::Ws(const JsonValue& v)
{
	WsConfig& c = _out.Ws;
	VideoOverride(v, "ConfigOverrides", _out.WsOverride);
	Controller(v, "ControllerHorizontal", c.ControllerHorizontal);
	Controller(v, "ControllerVertical", c.ControllerVertical);
	// The UI fixes the types: a WonderSwan's controls are built in
	c.ControllerHorizontal.Type = ControllerType::WsController;
	c.ControllerVertical.Type = ControllerType::WsControllerVertical;
	Enum(v, "Model", c.Model);
	Bool(v, "UseBootRom", c.UseBootRom);
	Bool(v, "AutoRotate", c.AutoRotate);
	Bool(v, "BlendFrames", c.BlendFrames);
	Bool(v, "LcdAdjustColors", c.LcdAdjustColors);
	Bool(v, "LcdShowIcons", c.LcdShowIcons);
	Numbered(v, "HideBgLayer", "", c.HideBgLayers);
	Bool(v, "DisableSprites", c.DisableSprites);
	Enum(v, "AudioMode", c.AudioMode, { { "Speaker", WsAudioMode::Speakers } });
	Num(v, "Channel1Vol", c.Channel1Vol);
	Num(v, "Channel2Vol", c.Channel2Vol);
	Num(v, "Channel3Vol", c.Channel3Vol);
	Num(v, "Channel4Vol", c.Channel4Vol);
	Num(v, "Channel5Vol", c.Channel5Vol);
}

// PreferencesConfig.ApplyConfig, less shortcuts and folder overrides
void Reader::Preferences(const JsonValue& v)
{
	PreferencesConfig& c = _out.Preferences;
	Bool(v, "ShowFps", c.ShowFps);
	Bool(v, "ShowFrameCounter", c.ShowFrameCounter);
	Bool(v, "ShowGameTimer", c.ShowGameTimer);
	Bool(v, "ShowLagCounter", c.ShowLagCounter);
	Bool(v, "ShowDebugInfo", c.ShowDebugInfo);
	Bool(v, "DisableOsd", c.DisableOsd);
	Bool(v, "AllowBackgroundInput", c.AllowBackgroundInput);
	Bool(v, "ShowTurboRewindIcons", c.ShowTurboRewindIcons);
	const JsonValue& selection = v.Get("GameSelectionScreenMode");
	if(selection.GetType() == JType::String) c.DisableGameSelectionScreen = selection.GetString() == "Disabled";
	Enum(v, "HudSize", c.HudSize);

	bool rewind = true;
	Bool(v, "EnableRewind", rewind);
	Num(v, "RewindBufferSize", c.RewindBufferSize);
	if(!rewind) c.RewindBufferSize = 0;

	bool autoSave = true;
	Bool(v, "EnableAutoSaveState", autoSave);
	Num(v, "AutoSaveStateDelay", c.AutoSaveStateDelay);
	if(!autoSave) c.AutoSaveStateDelay = 0;
}

void Reader::Read(const JsonValue& root)
{
	Video(root.Get("Video"));
	Audio(root.Get("Audio"));
	Input(root.Get("Input"));
	const JsonValue& emulation = root.Get("Emulation");
	Num(emulation, "EmulationSpeed", _out.Emulation.EmulationSpeed);
	Num(emulation, "TurboSpeed", _out.Emulation.TurboSpeed);
	Num(emulation, "RewindSpeed", _out.Emulation.RewindSpeed);
	Num(emulation, "RunAheadFrames", _out.Emulation.RunAheadFrames);
	Snes(root.Get("Snes"));
	Nes(root.Get("Nes"));
	Gameboy(root.Get("Gameboy"));
	Gba(root.Get("Gba"));
	PcEngine(root.Get("PcEngine"));
	Sms(root.Get("Sms"));
	Cv(root.Get("Cv"));
	Ws(root.Get("Ws"));
	Preferences(root.Get("Preferences"));
	const JsonValue& player = root.Get("AudioPlayer");
	Num(player, "Volume", _out.AudioPlayer.Volume);
	Bool(player, "Repeat", _out.AudioPlayer.Repeat);
	Bool(player, "Shuffle", _out.AudioPlayer.Shuffle);
}

} // namespace

// --- Defaults ---

// Where Mesen's UI defaults (its config classes' initializers) differ from
// the core's struct defaults
MesenSettings::MesenSettings()
{
	Video.LcdGridTopRightBrightness = 0.85;
	Video.LcdGridBottomLeftBrightness = 0.85;
	Video.LcdGridBottomRightBrightness = 0.85;
	Video.NtscYFilterLength = 0;
	Video.NtscIFilterLength = 0.5;
	Video.NtscQFilterLength = 0.5;

	Audio.ReverbStrength = 5;
	Audio.ReverbDelay = 10;

	Input.MouseSensitivity = 5;
	Input.DisplayInputPosition = InputDisplayPosition::BottomRight;
	Input.ForceFeedbackIntensity = 0.5;

	Snes.SpcClockSpeedAdjustment = 40;
	Snes.Overscan = { 0, 0, 7, 8 };

	for(int i = 0; i < 11; i++) Nes.ChannelVolumes[i] = 100;
	Nes.RamPowerOnState = RamState::AllZeros;
	Nes.StereoDelay = 15;
	Nes.StereoPanningAngle = 15;
	Nes.StereoCombFilterDelay = 5;
	Nes.StereoCombFilterStrength = 100;
	std::copy(NesDefaultPalette, NesDefaultPalette + 64, Nes.UserPalette);

	for(int i = 0; i < 4; i++) {
		Gameboy.BgColors[i] |= 0xFF000000;
		Gameboy.Obj0Colors[i] |= 0xFF000000;
		Gameboy.Obj1Colors[i] |= 0xFF000000;
	}

	PcEngine.PreventSelectRunReset = true;
	PcEngine.Overscan = { 18, 18, 3, 0 };
	std::copy(PceDefaultPalette, PceDefaultPalette + 512, PcEngine.Palette);

	Sms.UseSgPalette = true;
	for(int i = 0; i < 4; i++) Sms.ChannelVolumes[i] = 100;
	Sms.NtscOverscan = { 0, 0, 24, 24 };
	Sms.PalOverscan = { 0, 0, 24, 24 };
	Sms.GameGearOverscan = { 48, 48, 48, 48 };

	for(int i = 0; i < 4; i++) Cv.ChannelVolumes[i] = 100;
	Cv.RamPowerOnState = RamState::AllZeros;

	Ws.AutoRotate = true;
	Ws.BlendFrames = true;
	Ws.LcdAdjustColors = true;
	Ws.LcdShowIcons = true;

	Preferences.ShowTurboRewindIcons = true;
}

bool ParseMesenSettings(const std::string& text, MesenSettings& out, std::string& error, MesenPadLayout layout)
{
	// Mesen writes the file with a UTF-8 byte order mark
	std::string json = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? text.substr(3) : text;
	std::optional<JsonValue> root = ParseJson(json);
	if(!root || root->GetType() != JType::Object) {
		error = "not valid JSON";
		return false;
	}
	Reader(out, layout).Read(*root);
	out.Audio.AudioDevice = out.AudioDevice.c_str();
	return true;
}

bool LoadMesenSettings(const std::string& path, MesenSettings& out, std::string& error, MesenPadLayout layout)
{
	std::ifstream file(path, std::ios::binary);
	if(!file) {
		error = "cannot be read";
		return false;
	}
	std::stringstream text;
	text << file.rdbuf();
	return ParseMesenSettings(text.str(), out, error, layout);
}
