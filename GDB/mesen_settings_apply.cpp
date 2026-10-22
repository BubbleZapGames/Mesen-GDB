// The emulator half of mesen_settings: kept apart from the reader, which the
// unit tests link without the core
#include "mesen_settings.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/RomInfo.h"

void ApplyMesenSettings(EmuSettings* settings, MesenSettings& mesen)
{
	mesen.Audio.AudioDevice = mesen.AudioDevice.c_str();
	// Saves, save states and screenshots stay in mesen-gdb's own folder
	mesen.Preferences.SaveFolderOverride = nullptr;
	mesen.Preferences.SaveStateFolderOverride = nullptr;
	mesen.Preferences.ScreenshotFolderOverride = nullptr;

	settings->SetVideoConfig(mesen.Video);
	settings->SetAudioConfig(mesen.Audio);
	settings->SetInputConfig(mesen.Input);
	settings->SetEmulationConfig(mesen.Emulation);
	settings->SetSnesConfig(mesen.Snes);
	settings->SetNesConfig(mesen.Nes);
	settings->SetGameboyConfig(mesen.Gameboy);
	settings->SetGbaConfig(mesen.Gba);
	settings->SetPcEngineConfig(mesen.PcEngine);
	settings->SetSmsConfig(mesen.Sms);
	settings->SetCvConfig(mesen.Cv);
	settings->SetWsConfig(mesen.Ws);
	settings->SetPreferences(mesen.Preferences);
	settings->SetAudioPlayerConfig(mesen.AudioPlayer);
}

// ConsoleOverrideConfig.GetActiveOverride and VideoConfig.ApplyConfig
void ApplyMesenVideoOverride(EmuSettings* settings, const MesenSettings& mesen, ConsoleType console, RomFormat format)
{
	const MesenVideoOverride* active = nullptr;
	switch(console) {
		case ConsoleType::Snes: active = &mesen.SnesOverride; break;
		case ConsoleType::Gameboy: active = &mesen.GameboyOverride; break;
		case ConsoleType::Nes: active = &mesen.NesOverride; break;
		case ConsoleType::PcEngine: active = &mesen.PcEngineOverride; break;
		case ConsoleType::Sms:
			active = format == RomFormat::ColecoVision ? &mesen.CvOverride :
				format == RomFormat::GameGear ? &mesen.GameGearOverride : &mesen.SmsOverride;
			break;
		case ConsoleType::Gba: active = &mesen.GbaOverride; break;
		case ConsoleType::Ws: active = &mesen.WsOverride; break;
	}
	if(!active || (!active->OverrideVideoFilter && !active->OverrideAspectRatio)) return;

	VideoConfig video = mesen.Video;
	if(active->OverrideVideoFilter) {
		video.VideoFilter = active->VideoFilter;
	}
	if(active->OverrideAspectRatio) {
		video.AspectRatio = active->AspectRatio;
		video.CustomAspectRatio = active->CustomAspectRatio;
	}
	settings->SetVideoConfig(video);
}
