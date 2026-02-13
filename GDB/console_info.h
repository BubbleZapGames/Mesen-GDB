#pragma once
#include <vector>
#include "Shared/CpuType.h"
#include "Shared/MemoryType.h"
#include "Shared/SettingTypes.h"
#include "Debugger/DebugUtilities.h"

namespace ConsoleInfo {

inline DebuggerFlags GetDebuggerFlag(CpuType cpu)
{
	switch(cpu) {
		case CpuType::Snes: return DebuggerFlags::SnesDebuggerEnabled;
		case CpuType::Spc: return DebuggerFlags::SpcDebuggerEnabled;
		case CpuType::NecDsp: return DebuggerFlags::NecDspDebuggerEnabled;
		case CpuType::Sa1: return DebuggerFlags::Sa1DebuggerEnabled;
		case CpuType::Gsu: return DebuggerFlags::GsuDebuggerEnabled;
		case CpuType::Cx4: return DebuggerFlags::Cx4DebuggerEnabled;
		case CpuType::St018: return DebuggerFlags::St018DebuggerEnabled;
		case CpuType::Gameboy: return DebuggerFlags::GbDebuggerEnabled;
		case CpuType::Nes: return DebuggerFlags::NesDebuggerEnabled;
		case CpuType::Pce: return DebuggerFlags::PceDebuggerEnabled;
		case CpuType::Sms: return DebuggerFlags::SmsDebuggerEnabled;
		case CpuType::Gba: return DebuggerFlags::GbaDebuggerEnabled;
		case CpuType::Ws: return DebuggerFlags::WsDebuggerEnabled;
	}
	return DebuggerFlags::SnesDebuggerEnabled;
}

inline const char* GetCpuName(CpuType cpu)
{
	switch(cpu) {
		case CpuType::Snes: return "SNES 65816";
		case CpuType::Spc: return "SNES SPC700";
		case CpuType::NecDsp: return "SNES NEC DSP";
		case CpuType::Sa1: return "SNES SA-1";
		case CpuType::Gsu: return "SNES GSU";
		case CpuType::Cx4: return "SNES CX4";
		case CpuType::St018: return "SNES ST018";
		case CpuType::Gameboy: return "GB LR35902";
		case CpuType::Nes: return "NES 6502";
		case CpuType::Pce: return "PCE HuC6280";
		case CpuType::Sms: return "SMS Z80";
		case CpuType::Gba: return "GBA ARM7";
		case CpuType::Ws: return "WS V30MZ";
	}
	return "Unknown";
}

inline const char* GetConsoleName(ConsoleType console)
{
	switch(console) {
		case ConsoleType::Snes: return "Super Nintendo";
		case ConsoleType::Gameboy: return "Game Boy";
		case ConsoleType::Nes: return "Nintendo Entertainment System";
		case ConsoleType::PcEngine: return "PC Engine";
		case ConsoleType::Sms: return "Sega Master System";
		case ConsoleType::Gba: return "Game Boy Advance";
		case ConsoleType::Ws: return "WonderSwan";
	}
	return "Unknown";
}

inline MemoryType GetCpuMemoryType(CpuType cpu)
{
	return DebugUtilities::GetCpuMemoryType(cpu);
}

struct MemoryRegion {
	MemoryType type;
	const char* name;
	const char* shortName;
};

inline std::vector<MemoryRegion> GetMemoryRegions(ConsoleType console)
{
	switch(console) {
		case ConsoleType::Nes:
			return {
				{MemoryType::NesInternalRam, "Internal RAM", "ram"},
				{MemoryType::NesPrgRom, "PRG ROM", "rom"},
				{MemoryType::NesSaveRam, "Save RAM", "sram"},
				{MemoryType::NesWorkRam, "Work RAM", "wram"},
				{MemoryType::NesChrRam, "CHR RAM", "chr"},
				{MemoryType::NesChrRom, "CHR ROM", "chrrom"},
				{MemoryType::NesSpriteRam, "Sprite RAM", "oam"},
				{MemoryType::NesPaletteRam, "Palette RAM", "pal"},
				{MemoryType::NesNametableRam, "Nametable RAM", "nt"},
			};

		case ConsoleType::Snes:
			return {
				{MemoryType::SnesWorkRam, "Work RAM", "wram"},
				{MemoryType::SnesVideoRam, "Video RAM", "vram"},
				{MemoryType::SnesCgRam, "Palette RAM", "cgram"},
				{MemoryType::SnesSpriteRam, "Sprite RAM", "oam"},
				{MemoryType::SnesPrgRom, "PRG ROM", "rom"},
				{MemoryType::SnesSaveRam, "Save RAM", "sram"},
			};

		case ConsoleType::Gameboy:
			return {
				{MemoryType::GbWorkRam, "Work RAM", "wram"},
				{MemoryType::GbVideoRam, "Video RAM", "vram"},
				{MemoryType::GbCartRam, "Cart RAM", "cartram"},
				{MemoryType::GbHighRam, "High RAM", "hram"},
				{MemoryType::GbSpriteRam, "Sprite RAM", "oam"},
				{MemoryType::GbPrgRom, "PRG ROM", "rom"},
			};

		case ConsoleType::Gba:
			return {
				{MemoryType::GbaIntWorkRam, "Internal Work RAM", "iwram"},
				{MemoryType::GbaExtWorkRam, "External Work RAM", "ewram"},
				{MemoryType::GbaVideoRam, "Video RAM", "vram"},
				{MemoryType::GbaSpriteRam, "Sprite RAM", "oam"},
				{MemoryType::GbaPaletteRam, "Palette RAM", "pal"},
				{MemoryType::GbaPrgRom, "PRG ROM", "rom"},
				{MemoryType::GbaSaveRam, "Save RAM", "sram"},
			};

		case ConsoleType::PcEngine:
			return {
				{MemoryType::PceWorkRam, "Work RAM", "wram"},
				{MemoryType::PceVideoRam, "Video RAM", "vram"},
				{MemoryType::PcePaletteRam, "Palette RAM", "pal"},
				{MemoryType::PceSpriteRam, "Sprite RAM", "oam"},
				{MemoryType::PcePrgRom, "PRG ROM", "rom"},
				{MemoryType::PceSaveRam, "Save RAM", "sram"},
				{MemoryType::PceCdromRam, "CD-ROM RAM", "cdram"},
				{MemoryType::PceAdpcmRam, "ADPCM RAM", "adpcm"},
			};

		case ConsoleType::Sms:
			return {
				{MemoryType::SmsWorkRam, "Work RAM", "wram"},
				{MemoryType::SmsVideoRam, "Video RAM", "vram"},
				{MemoryType::SmsPaletteRam, "Palette RAM", "pal"},
				{MemoryType::SmsPrgRom, "PRG ROM", "rom"},
				{MemoryType::SmsCartRam, "Cart RAM", "cartram"},
			};

		case ConsoleType::Ws:
			return {
				{MemoryType::WsWorkRam, "Work RAM", "wram"},
				{MemoryType::WsPrgRom, "PRG ROM", "rom"},
				{MemoryType::WsCartRam, "Cart RAM", "cartram"},
				{MemoryType::WsBootRom, "Boot ROM", "bootrom"},
				{MemoryType::WsInternalEeprom, "Internal EEPROM", "eeprom"},
			};
	}
	return {};
}

} // namespace ConsoleInfo
