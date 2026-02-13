#include "pch.h"
#include "formatter.h"
#include "NES/NesTypes.h"
#include "SNES/SnesCpuTypes.h"
#include "Gameboy/GbTypes.h"
#include "GBA/GbaTypes.h"
#include "PCE/PceTypes.h"
#include "SMS/SmsTypes.h"
#include "WS/WsTypes.h"
#include <sstream>
#include <cstdio>

namespace Formatter {

// --- NES 6502 ---

static std::string FormatNesFlags(uint8_t ps)
{
	char buf[9];
	buf[0] = (ps & PSFlags::Negative)  ? 'N' : 'n';
	buf[1] = (ps & PSFlags::Overflow)  ? 'V' : 'v';
	buf[2] = (ps & PSFlags::Reserved)  ? '1' : '0';
	buf[3] = (ps & PSFlags::Break)     ? 'B' : 'b';
	buf[4] = (ps & PSFlags::Decimal)   ? 'D' : 'd';
	buf[5] = (ps & PSFlags::Interrupt) ? 'I' : 'i';
	buf[6] = (ps & PSFlags::Zero)      ? 'Z' : 'z';
	buf[7] = (ps & PSFlags::Carry)     ? 'C' : 'c';
	buf[8] = '\0';
	return std::string(buf);
}

static std::string FormatNesRegisters(const NesCpuState& s)
{
	char buf[256];
	snprintf(buf, sizeof(buf),
		"PC=$%04X  A=$%02X  X=$%02X  Y=$%02X  SP=$%02X\n"
		"Flags: %s  Cycles: %lu",
		s.PC, s.A, s.X, s.Y, s.SP,
		FormatNesFlags(s.PS).c_str(),
		(unsigned long)s.CycleCount);
	return std::string(buf);
}

static std::string FormatNesRegistersJson(const NesCpuState& s)
{
	char buf[512];
	uint8_t ps = s.PS;
	snprintf(buf, sizeof(buf),
		"{\"registers\":{"
		"\"pc\":\"%04x\","
		"\"a\":\"%02x\","
		"\"x\":\"%02x\","
		"\"y\":\"%02x\","
		"\"sp\":\"%02x\","
		"\"ps\":\"%02x\","
		"\"flags\":{"
		"\"n\":%s,\"v\":%s,\"d\":%s,\"i\":%s,\"z\":%s,\"c\":%s"
		"}},"
		"\"cycles\":%lu"
		"}",
		s.PC, s.A, s.X, s.Y, s.SP, ps,
		(ps & PSFlags::Negative)  ? "true" : "false",
		(ps & PSFlags::Overflow)  ? "true" : "false",
		(ps & PSFlags::Decimal)   ? "true" : "false",
		(ps & PSFlags::Interrupt) ? "true" : "false",
		(ps & PSFlags::Zero)      ? "true" : "false",
		(ps & PSFlags::Carry)     ? "true" : "false",
		(unsigned long)s.CycleCount);
	return std::string(buf);
}

// --- SNES 65816 ---

static std::string FormatSnesFlags(uint8_t ps)
{
	char buf[9];
	buf[0] = (ps & ProcFlags::Negative)    ? 'N' : 'n';
	buf[1] = (ps & ProcFlags::Overflow)    ? 'V' : 'v';
	buf[2] = (ps & ProcFlags::MemoryMode8) ? 'M' : 'm';
	buf[3] = (ps & ProcFlags::IndexMode8)  ? 'X' : 'x';
	buf[4] = (ps & ProcFlags::Decimal)     ? 'D' : 'd';
	buf[5] = (ps & ProcFlags::IrqDisable)  ? 'I' : 'i';
	buf[6] = (ps & ProcFlags::Zero)        ? 'Z' : 'z';
	buf[7] = (ps & ProcFlags::Carry)       ? 'C' : 'c';
	buf[8] = '\0';
	return std::string(buf);
}

static std::string FormatSnesRegisters(const SnesCpuState& s)
{
	char buf[256];
	bool m8 = (s.PS & ProcFlags::MemoryMode8) != 0;
	bool x8 = (s.PS & ProcFlags::IndexMode8) != 0;

	snprintf(buf, sizeof(buf),
		"PC=$%02X:%04X  A=$%04X  X=$%04X  Y=$%04X  SP=$%04X  D=$%04X  DBR=$%02X\n"
		"Flags: %s  [%s %s]  Cycles: %lu",
		s.K, s.PC,
		s.A, s.X, s.Y,
		s.SP, s.D, s.DBR,
		FormatSnesFlags(s.PS).c_str(),
		m8 ? "m8" : "m16",
		x8 ? "x8" : "x16",
		(unsigned long)s.CycleCount);
	return std::string(buf);
}

static std::string FormatSnesRegistersJson(const SnesCpuState& s)
{
	char buf[1024];
	uint8_t ps = s.PS;
	snprintf(buf, sizeof(buf),
		"{\"registers\":{"
		"\"pc\":\"%02x%04x\","
		"\"a\":\"%04x\","
		"\"x\":\"%04x\","
		"\"y\":\"%04x\","
		"\"sp\":\"%04x\","
		"\"d\":\"%04x\","
		"\"dbr\":\"%02x\","
		"\"ps\":\"%02x\","
		"\"flags\":{"
		"\"n\":%s,\"v\":%s,\"m\":%s,\"x\":%s,"
		"\"d\":%s,\"i\":%s,\"z\":%s,\"c\":%s"
		"}},"
		"\"cycles\":%lu,"
		"\"stop_state\":%d"
		"}",
		s.K, s.PC,
		s.A, s.X, s.Y,
		s.SP, s.D, s.DBR, ps,
		(ps & ProcFlags::Negative)    ? "true" : "false",
		(ps & ProcFlags::Overflow)    ? "true" : "false",
		(ps & ProcFlags::MemoryMode8) ? "true" : "false",
		(ps & ProcFlags::IndexMode8)  ? "true" : "false",
		(ps & ProcFlags::Decimal)     ? "true" : "false",
		(ps & ProcFlags::IrqDisable)  ? "true" : "false",
		(ps & ProcFlags::Zero)        ? "true" : "false",
		(ps & ProcFlags::Carry)       ? "true" : "false",
		(unsigned long)s.CycleCount,
		(int)s.StopState);
	return std::string(buf);
}

// --- Game Boy LR35902 ---

static std::string FormatGbFlags(uint8_t f)
{
	char buf[5];
	buf[0] = (f & GbCpuFlags::Zero)      ? 'Z' : 'z';
	buf[1] = (f & GbCpuFlags::AddSub)    ? 'N' : 'n';
	buf[2] = (f & GbCpuFlags::HalfCarry) ? 'H' : 'h';
	buf[3] = (f & GbCpuFlags::Carry)     ? 'C' : 'c';
	buf[4] = '\0';
	return std::string(buf);
}

static std::string FormatGbRegisters(const GbCpuState& s)
{
	char buf[256];
	snprintf(buf, sizeof(buf),
		"PC=$%04X  SP=$%04X  A=$%02X  F=$%02X  B=$%02X  C=$%02X  D=$%02X  E=$%02X  H=$%02X  L=$%02X\n"
		"Flags: %s  Cycles: %lu",
		s.PC, s.SP, s.A, s.Flags,
		s.B, s.C, s.D, s.E, s.H, s.L,
		FormatGbFlags(s.Flags).c_str(),
		(unsigned long)s.CycleCount);
	return std::string(buf);
}

static std::string FormatGbRegistersJson(const GbCpuState& s)
{
	char buf[512];
	uint8_t f = s.Flags;
	snprintf(buf, sizeof(buf),
		"{\"registers\":{"
		"\"pc\":\"%04x\",\"sp\":\"%04x\","
		"\"a\":\"%02x\",\"f\":\"%02x\","
		"\"b\":\"%02x\",\"c\":\"%02x\","
		"\"d\":\"%02x\",\"e\":\"%02x\","
		"\"h\":\"%02x\",\"l\":\"%02x\","
		"\"flags\":{"
		"\"z\":%s,\"n\":%s,\"h\":%s,\"c\":%s"
		"}},"
		"\"cycles\":%lu"
		"}",
		s.PC, s.SP, s.A, f,
		s.B, s.C, s.D, s.E, s.H, s.L,
		(f & GbCpuFlags::Zero)      ? "true" : "false",
		(f & GbCpuFlags::AddSub)    ? "true" : "false",
		(f & GbCpuFlags::HalfCarry) ? "true" : "false",
		(f & GbCpuFlags::Carry)     ? "true" : "false",
		(unsigned long)s.CycleCount);
	return std::string(buf);
}

// --- GBA ARM7 ---

static const char* GbaModeName(GbaCpuMode mode)
{
	switch(mode) {
		case GbaCpuMode::User: return "USR";
		case GbaCpuMode::Fiq: return "FIQ";
		case GbaCpuMode::Irq: return "IRQ";
		case GbaCpuMode::Supervisor: return "SVC";
		case GbaCpuMode::Abort: return "ABT";
		case GbaCpuMode::Undefined: return "UND";
		case GbaCpuMode::System: return "SYS";
	}
	return "???";
}

static std::string FormatGbaRegisters(const GbaCpuState& s)
{
	char buf[512];
	int n = 0;
	for(int i = 0; i < 16; i++) {
		if(i == 13) n += snprintf(buf + n, sizeof(buf) - n, "SP=");
		else if(i == 14) n += snprintf(buf + n, sizeof(buf) - n, "LR=");
		else if(i == 15) n += snprintf(buf + n, sizeof(buf) - n, "PC=");
		else n += snprintf(buf + n, sizeof(buf) - n, "R%d=", i);
		n += snprintf(buf + n, sizeof(buf) - n, "$%08X  ", s.R[i]);
		if(i == 3 || i == 7 || i == 11) n += snprintf(buf + n, sizeof(buf) - n, "\n");
	}
	uint32_t cpsr = const_cast<GbaCpuFlags&>(s.CPSR).ToInt32();
	n += snprintf(buf + n, sizeof(buf) - n,
		"\nCPSR=$%08X  [%c%c%c%c %s %s]  Cycles: %lu",
		cpsr,
		s.CPSR.Negative ? 'N' : 'n',
		s.CPSR.Zero     ? 'Z' : 'z',
		s.CPSR.Carry    ? 'C' : 'c',
		s.CPSR.Overflow ? 'V' : 'v',
		s.CPSR.Thumb ? "THUMB" : "ARM",
		GbaModeName(s.CPSR.Mode),
		(unsigned long)s.CycleCount);
	return std::string(buf);
}

static std::string FormatGbaRegistersJson(const GbaCpuState& s)
{
	std::ostringstream oss;
	oss << "{\"registers\":{";
	for(int i = 0; i < 16; i++) {
		char key[8], val[16];
		snprintf(key, sizeof(key), "\"r%d\"", i);
		snprintf(val, sizeof(val), "\"%08x\"", s.R[i]);
		if(i > 0) oss << ",";
		oss << key << ":" << val;
	}
	uint32_t cpsr = const_cast<GbaCpuFlags&>(s.CPSR).ToInt32();
	char extra[256];
	snprintf(extra, sizeof(extra),
		",\"cpsr\":\"%08x\","
		"\"flags\":{"
		"\"n\":%s,\"z\":%s,\"c\":%s,\"v\":%s,"
		"\"thumb\":%s,\"mode\":\"%s\""
		"}},"
		"\"cycles\":%lu"
		"}",
		cpsr,
		s.CPSR.Negative ? "true" : "false",
		s.CPSR.Zero     ? "true" : "false",
		s.CPSR.Carry    ? "true" : "false",
		s.CPSR.Overflow ? "true" : "false",
		s.CPSR.Thumb    ? "true" : "false",
		GbaModeName(s.CPSR.Mode),
		(unsigned long)s.CycleCount);
	oss << extra;
	return oss.str();
}

// --- PCE HuC6280 ---

static std::string FormatPceFlags(uint8_t ps)
{
	char buf[9];
	buf[0] = (ps & PceCpuFlags::Negative)  ? 'N' : 'n';
	buf[1] = (ps & PceCpuFlags::Overflow)  ? 'V' : 'v';
	buf[2] = (ps & PceCpuFlags::Memory)    ? 'T' : 't';
	buf[3] = (ps & PceCpuFlags::Break)     ? 'B' : 'b';
	buf[4] = (ps & PceCpuFlags::Decimal)   ? 'D' : 'd';
	buf[5] = (ps & PceCpuFlags::Interrupt) ? 'I' : 'i';
	buf[6] = (ps & PceCpuFlags::Zero)      ? 'Z' : 'z';
	buf[7] = (ps & PceCpuFlags::Carry)     ? 'C' : 'c';
	buf[8] = '\0';
	return std::string(buf);
}

static std::string FormatPceRegisters(const PceCpuState& s)
{
	char buf[256];
	snprintf(buf, sizeof(buf),
		"PC=$%04X  A=$%02X  X=$%02X  Y=$%02X  SP=$%02X\n"
		"Flags: %s  Cycles: %lu",
		s.PC, s.A, s.X, s.Y, s.SP,
		FormatPceFlags(s.PS).c_str(),
		(unsigned long)s.CycleCount);
	return std::string(buf);
}

static std::string FormatPceRegistersJson(const PceCpuState& s)
{
	char buf[512];
	uint8_t ps = s.PS;
	snprintf(buf, sizeof(buf),
		"{\"registers\":{"
		"\"pc\":\"%04x\","
		"\"a\":\"%02x\","
		"\"x\":\"%02x\","
		"\"y\":\"%02x\","
		"\"sp\":\"%02x\","
		"\"ps\":\"%02x\","
		"\"flags\":{"
		"\"n\":%s,\"v\":%s,\"t\":%s,\"d\":%s,\"i\":%s,\"z\":%s,\"c\":%s"
		"}},"
		"\"cycles\":%lu"
		"}",
		s.PC, s.A, s.X, s.Y, s.SP, ps,
		(ps & PceCpuFlags::Negative)  ? "true" : "false",
		(ps & PceCpuFlags::Overflow)  ? "true" : "false",
		(ps & PceCpuFlags::Memory)    ? "true" : "false",
		(ps & PceCpuFlags::Decimal)   ? "true" : "false",
		(ps & PceCpuFlags::Interrupt) ? "true" : "false",
		(ps & PceCpuFlags::Zero)      ? "true" : "false",
		(ps & PceCpuFlags::Carry)     ? "true" : "false",
		(unsigned long)s.CycleCount);
	return std::string(buf);
}

// --- SMS Z80 ---

static std::string FormatSmsFlags(uint8_t f)
{
	char buf[9];
	buf[0] = (f & SmsCpuFlags::Sign)      ? 'S' : 's';
	buf[1] = (f & SmsCpuFlags::Zero)      ? 'Z' : 'z';
	buf[2] = (f & SmsCpuFlags::F5)        ? '5' : '.';
	buf[3] = (f & SmsCpuFlags::HalfCarry) ? 'H' : 'h';
	buf[4] = (f & SmsCpuFlags::F3)        ? '3' : '.';
	buf[5] = (f & SmsCpuFlags::Parity)    ? 'P' : 'p';
	buf[6] = (f & SmsCpuFlags::AddSub)    ? 'N' : 'n';
	buf[7] = (f & SmsCpuFlags::Carry)     ? 'C' : 'c';
	buf[8] = '\0';
	return std::string(buf);
}

static std::string FormatSmsRegisters(const SmsCpuState& s)
{
	char buf[512];
	snprintf(buf, sizeof(buf),
		"PC=$%04X  SP=$%04X  A=$%02X  F=$%02X  B=$%02X  C=$%02X  D=$%02X  E=$%02X  H=$%02X  L=$%02X\n"
		"IX=$%02X%02X  IY=$%02X%02X  I=$%02X  R=$%02X\n"
		"Flags: %s  Cycles: %lu",
		s.PC, s.SP, s.A, s.Flags,
		s.B, s.C, s.D, s.E, s.H, s.L,
		s.IXH, s.IXL, s.IYH, s.IYL,
		s.I, s.R,
		FormatSmsFlags(s.Flags).c_str(),
		(unsigned long)s.CycleCount);
	return std::string(buf);
}

static std::string FormatSmsRegistersJson(const SmsCpuState& s)
{
	char buf[1024];
	uint8_t f = s.Flags;
	snprintf(buf, sizeof(buf),
		"{\"registers\":{"
		"\"pc\":\"%04x\",\"sp\":\"%04x\","
		"\"a\":\"%02x\",\"f\":\"%02x\","
		"\"b\":\"%02x\",\"c\":\"%02x\","
		"\"d\":\"%02x\",\"e\":\"%02x\","
		"\"h\":\"%02x\",\"l\":\"%02x\","
		"\"ix\":\"%04x\",\"iy\":\"%04x\","
		"\"i\":\"%02x\",\"r\":\"%02x\","
		"\"flags\":{"
		"\"s\":%s,\"z\":%s,\"h\":%s,\"p\":%s,\"n\":%s,\"c\":%s"
		"}},"
		"\"cycles\":%lu"
		"}",
		s.PC, s.SP, s.A, f,
		s.B, s.C, s.D, s.E, s.H, s.L,
		(s.IXH << 8) | s.IXL, (s.IYH << 8) | s.IYL,
		s.I, s.R,
		(f & SmsCpuFlags::Sign)      ? "true" : "false",
		(f & SmsCpuFlags::Zero)      ? "true" : "false",
		(f & SmsCpuFlags::HalfCarry) ? "true" : "false",
		(f & SmsCpuFlags::Parity)    ? "true" : "false",
		(f & SmsCpuFlags::AddSub)    ? "true" : "false",
		(f & SmsCpuFlags::Carry)     ? "true" : "false",
		(unsigned long)s.CycleCount);
	return std::string(buf);
}

// --- WonderSwan V30MZ ---

static std::string FormatWsRegisters(const WsCpuState& s)
{
	char buf[512];
	uint16_t flags = const_cast<WsCpuFlags&>(s.Flags).Get();
	snprintf(buf, sizeof(buf),
		"CS:IP=$%04X:%04X  AX=$%04X  BX=$%04X  CX=$%04X  DX=$%04X\n"
		"SP=$%04X  BP=$%04X  SI=$%04X  DI=$%04X  DS=$%04X  ES=$%04X  SS=$%04X\n"
		"Flags=$%04X [%c%c%c%c%c%c%c%c%c]  Cycles: %lu",
		s.CS, s.IP,
		s.AX, s.BX, s.CX, s.DX,
		s.SP, s.BP, s.SI, s.DI,
		s.DS, s.ES, s.SS,
		flags,
		s.Flags.Overflow  ? 'O' : 'o',
		s.Flags.Direction ? 'D' : 'd',
		s.Flags.Irq       ? 'I' : 'i',
		s.Flags.Trap       ? 'T' : 't',
		s.Flags.Sign       ? 'S' : 's',
		s.Flags.Zero       ? 'Z' : 'z',
		s.Flags.AuxCarry   ? 'A' : 'a',
		s.Flags.Parity     ? 'P' : 'p',
		s.Flags.Carry      ? 'C' : 'c',
		(unsigned long)s.CycleCount);
	return std::string(buf);
}

static std::string FormatWsRegistersJson(const WsCpuState& s)
{
	char buf[1024];
	uint16_t flags = const_cast<WsCpuFlags&>(s.Flags).Get();
	snprintf(buf, sizeof(buf),
		"{\"registers\":{"
		"\"cs\":\"%04x\",\"ip\":\"%04x\","
		"\"ax\":\"%04x\",\"bx\":\"%04x\","
		"\"cx\":\"%04x\",\"dx\":\"%04x\","
		"\"sp\":\"%04x\",\"bp\":\"%04x\","
		"\"si\":\"%04x\",\"di\":\"%04x\","
		"\"ds\":\"%04x\",\"es\":\"%04x\",\"ss\":\"%04x\","
		"\"flags\":\"%04x\","
		"\"flag_bits\":{"
		"\"o\":%s,\"d\":%s,\"i\":%s,\"t\":%s,"
		"\"s\":%s,\"z\":%s,\"a\":%s,\"p\":%s,\"c\":%s"
		"}},"
		"\"cycles\":%lu"
		"}",
		s.CS, s.IP,
		s.AX, s.BX, s.CX, s.DX,
		s.SP, s.BP, s.SI, s.DI,
		s.DS, s.ES, s.SS,
		flags,
		s.Flags.Overflow  ? "true" : "false",
		s.Flags.Direction ? "true" : "false",
		s.Flags.Irq       ? "true" : "false",
		s.Flags.Trap       ? "true" : "false",
		s.Flags.Sign       ? "true" : "false",
		s.Flags.Zero       ? "true" : "false",
		s.Flags.AuxCarry   ? "true" : "false",
		s.Flags.Parity     ? "true" : "false",
		s.Flags.Carry      ? "true" : "false",
		(unsigned long)s.CycleCount);
	return std::string(buf);
}

// --- Dispatch functions ---

std::string FormatRegisters(CpuType cpu, const BaseState& state)
{
	switch(cpu) {
		case CpuType::Nes:
			return FormatNesRegisters(static_cast<const NesCpuState&>(state));
		case CpuType::Snes:
		case CpuType::Sa1:
			return FormatSnesRegisters(static_cast<const SnesCpuState&>(state));
		case CpuType::Gameboy:
			return FormatGbRegisters(static_cast<const GbCpuState&>(state));
		case CpuType::Gba:
			return FormatGbaRegisters(static_cast<const GbaCpuState&>(state));
		case CpuType::Pce:
			return FormatPceRegisters(static_cast<const PceCpuState&>(state));
		case CpuType::Sms:
			return FormatSmsRegisters(static_cast<const SmsCpuState&>(state));
		case CpuType::Ws:
			return FormatWsRegisters(static_cast<const WsCpuState&>(state));
		default:
			return "[Register display not implemented for this CPU type]";
	}
}

std::string FormatRegistersJson(CpuType cpu, const BaseState& state)
{
	switch(cpu) {
		case CpuType::Nes:
			return FormatNesRegistersJson(static_cast<const NesCpuState&>(state));
		case CpuType::Snes:
		case CpuType::Sa1:
			return FormatSnesRegistersJson(static_cast<const SnesCpuState&>(state));
		case CpuType::Gameboy:
			return FormatGbRegistersJson(static_cast<const GbCpuState&>(state));
		case CpuType::Gba:
			return FormatGbaRegistersJson(static_cast<const GbaCpuState&>(state));
		case CpuType::Pce:
			return FormatPceRegistersJson(static_cast<const PceCpuState&>(state));
		case CpuType::Sms:
			return FormatSmsRegistersJson(static_cast<const SmsCpuState&>(state));
		case CpuType::Ws:
			return FormatWsRegistersJson(static_cast<const WsCpuState&>(state));
		default:
			return "{\"error\":\"unsupported cpu type\"}";
	}
}

std::string FormatMemoryHex(const uint8_t* data, uint32_t len, uint32_t startAddr)
{
	std::ostringstream oss;
	for(uint32_t i = 0; i < len; i += 16) {
		char addr[16];
		snprintf(addr, sizeof(addr), "%06X: ", startAddr + i);
		oss << addr;

		// Hex bytes
		for(uint32_t j = 0; j < 16 && (i + j) < len; j++) {
			char hex[4];
			snprintf(hex, sizeof(hex), "%02X ", data[i + j]);
			oss << hex;
		}

		// ASCII
		oss << " ";
		for(uint32_t j = 0; j < 16 && (i + j) < len; j++) {
			uint8_t c = data[i + j];
			oss << (char)((c >= 0x20 && c <= 0x7E) ? c : '.');
		}
		oss << "\n";
	}
	return oss.str();
}

std::string FormatDisassembly(CodeLineData* lines, uint32_t count)
{
	std::ostringstream oss;
	for(uint32_t i = 0; i < count; i++) {
		CodeLineData& line = lines[i];
		if(line.Address < 0) continue;

		char addr[16];
		snprintf(addr, sizeof(addr), "%06X: ", line.Address);
		oss << addr;

		// Bytecode
		for(int j = 0; j < line.OpSize && j < 4; j++) {
			char hex[4];
			snprintf(hex, sizeof(hex), "%02X ", line.ByteCode[j]);
			oss << hex;
		}
		// Pad to align text
		for(int j = line.OpSize; j < 4; j++) {
			oss << "   ";
		}

		oss << line.Text;
		if(line.Comment[0] != '\0') {
			oss << "  ; " << line.Comment;
		}
		oss << "\n";
	}
	return oss.str();
}

std::string FormatCallstack(StackFrameInfo* frames, uint32_t count)
{
	std::ostringstream oss;
	for(uint32_t i = 0; i < count; i++) {
		char buf[64];
		const char* type = "";
		if((int)frames[i].Flags & 1) type = " [NMI]";
		else if((int)frames[i].Flags & 2) type = " [IRQ]";
		snprintf(buf, sizeof(buf), "#%d  $%06X -> $%06X%s\n",
			i, frames[i].Source, frames[i].Target, type);
		oss << buf;
	}
	return oss.str();
}

} // namespace Formatter
