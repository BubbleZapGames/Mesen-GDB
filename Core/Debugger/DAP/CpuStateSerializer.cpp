#include "CpuStateSerializer.h"
#include "DapJson.h"
#include "Core/Shared/BaseState.h"
#include "Core/Shared/CpuType.h"
#include "Core/SNES/SnesCpuTypes.h"
#include "Core/SNES/SpcTypes.h"
#include "Core/NES/NesTypes.h"
#include "Core/Gameboy/GbTypes.h"
#include "Core/PCE/PceTypes.h"
#include "Core/SMS/SmsTypes.h"
#include "Core/GBA/GbaTypes.h"
#include "Core/WS/WsTypes.h"
#include <cstdio>

// ── Emit helpers ─────────────────────────────────────────────────

static void AddVar(JsonValue& variables, const char* name, const char* value)
{
	JsonValue var = JsonValue::MakeObject();
	var.Set("name", JsonValue::MakeString(name));
	var.Set("value", JsonValue::MakeString(value));
	var.Set("variablesReference", JsonValue::MakeNumber(0));
	variables.Push(std::move(var));
}

static void AddHex8(JsonValue& variables, const char* name, uint8_t value)
{
	char buf[16];
	snprintf(buf, sizeof(buf), "$%02X", value);
	AddVar(variables, name, buf);
}

static void AddHex16(JsonValue& variables, const char* name, uint16_t value)
{
	char buf[16];
	snprintf(buf, sizeof(buf), "$%04X", value);
	AddVar(variables, name, buf);
}

static void AddHex32(JsonValue& variables, const char* name, uint32_t value)
{
	char buf[16];
	snprintf(buf, sizeof(buf), "$%08X", value);
	AddVar(variables, name, buf);
}

static void AddFlag(JsonValue& variables, const char* name, bool value)
{
	AddVar(variables, name, value ? "1" : "0");
}

static const char* GbaModeName(GbaCpuMode mode)
{
	switch(mode) {
		case GbaCpuMode::User: return "User";
		case GbaCpuMode::Fiq: return "FIQ";
		case GbaCpuMode::Irq: return "IRQ";
		case GbaCpuMode::Supervisor: return "Supervisor";
		case GbaCpuMode::Abort: return "Abort";
		case GbaCpuMode::Undefined: return "Undefined";
		case GbaCpuMode::System: return "System";
	}
	return "Unknown";
}

// Emits a 16-bit register built from a high/low 8-bit pair (Z80/LR35902 style)
static void AddPair(JsonValue& variables, const char* name, uint8_t high, uint8_t low)
{
	char buf[16];
	snprintf(buf, sizeof(buf), "$%04X", (uint16_t)((high << 8) | low));
	AddVar(variables, name, buf);
}

// ── Registers ────────────────────────────────────────────────────

JsonValue CpuStateSerializer::SerializeRegisters(CpuType cpuType, BaseState& state)
{
	JsonValue vars = JsonValue::MakeArray();
	char buf[24];

	switch(cpuType) {
		case CpuType::Snes:
		case CpuType::Sa1: {
			SnesCpuState& s = static_cast<SnesCpuState&>(state);
			snprintf(buf, sizeof(buf), "$%02X:%04X", s.K, s.PC);
			AddVar(vars, "PC", buf);
			AddHex16(vars, "A", s.A);
			AddHex16(vars, "X", s.X);
			AddHex16(vars, "Y", s.Y);
			AddHex16(vars, "SP", s.SP);
			AddHex16(vars, "D", s.D);
			AddHex8(vars, "K", s.K);
			AddHex8(vars, "DBR", s.DBR);
			AddHex8(vars, "P", s.PS);
			break;
		}

		case CpuType::Spc: {
			SpcState& s = static_cast<SpcState&>(state);
			AddHex16(vars, "PC", s.PC);
			AddHex8(vars, "A", s.A);
			AddHex8(vars, "X", s.X);
			AddHex8(vars, "Y", s.Y);
			// YA is the 16-bit accumulator pair used by the SPC700's word ops
			AddPair(vars, "YA", s.Y, s.A);
			AddHex8(vars, "SP", s.SP);
			AddHex8(vars, "P", s.PS);
			break;
		}

		case CpuType::Nes: {
			NesCpuState& s = static_cast<NesCpuState&>(state);
			AddHex16(vars, "PC", s.PC);
			AddHex8(vars, "A", s.A);
			AddHex8(vars, "X", s.X);
			AddHex8(vars, "Y", s.Y);
			AddHex8(vars, "SP", s.SP);
			AddHex8(vars, "P", s.PS);
			break;
		}

		case CpuType::Gameboy: {
			GbCpuState& s = static_cast<GbCpuState&>(state);
			AddHex16(vars, "PC", s.PC);
			AddHex16(vars, "SP", s.SP);
			AddHex8(vars, "A", s.A);
			AddHex8(vars, "F", s.Flags);
			AddPair(vars, "AF", s.A, s.Flags);
			AddHex8(vars, "B", s.B);
			AddHex8(vars, "C", s.C);
			AddPair(vars, "BC", s.B, s.C);
			AddHex8(vars, "D", s.D);
			AddHex8(vars, "E", s.E);
			AddPair(vars, "DE", s.D, s.E);
			AddHex8(vars, "H", s.H);
			AddHex8(vars, "L", s.L);
			AddPair(vars, "HL", s.H, s.L);
			break;
		}

		case CpuType::Pce: {
			PceCpuState& s = static_cast<PceCpuState&>(state);
			AddHex16(vars, "PC", s.PC);
			AddHex8(vars, "A", s.A);
			AddHex8(vars, "X", s.X);
			AddHex8(vars, "Y", s.Y);
			AddHex8(vars, "SP", s.SP);
			AddHex8(vars, "P", s.PS);
			break;
		}

		case CpuType::Sms: {
			SmsCpuState& s = static_cast<SmsCpuState&>(state);
			AddHex16(vars, "PC", s.PC);
			AddHex16(vars, "SP", s.SP);
			AddHex8(vars, "A", s.A);
			AddHex8(vars, "F", s.Flags);
			AddPair(vars, "AF", s.A, s.Flags);
			AddPair(vars, "BC", s.B, s.C);
			AddPair(vars, "DE", s.D, s.E);
			AddPair(vars, "HL", s.H, s.L);
			AddPair(vars, "IX", s.IXH, s.IXL);
			AddPair(vars, "IY", s.IYH, s.IYL);
			AddHex8(vars, "I", s.I);
			AddHex8(vars, "R", s.R);
			AddPair(vars, "AF'", s.AltA, s.AltFlags);
			AddPair(vars, "BC'", s.AltB, s.AltC);
			AddPair(vars, "DE'", s.AltD, s.AltE);
			AddPair(vars, "HL'", s.AltH, s.AltL);
			snprintf(buf, sizeof(buf), "%d", s.IM);
			AddVar(vars, "IM", buf);
			break;
		}

		case CpuType::Gba: {
			GbaCpuState& s = static_cast<GbaCpuState&>(state);
			// R15 is the PC, but it reads ahead of the executing instruction —
			// report the address actually being executed instead.
			AddHex32(vars, "PC", s.Pipeline.Execute.Address);
			static const char* names[16] = {
				"R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7",
				"R8", "R9", "R10", "R11", "R12", "SP", "LR", "R15"
			};
			for(int i = 0; i < 16; i++) {
				AddHex32(vars, names[i], s.R[i]);
			}
			AddHex32(vars, "CPSR", s.CPSR.ToInt32());
			break;
		}

		case CpuType::Ws: {
			WsCpuState& s = static_cast<WsCpuState&>(state);
			snprintf(buf, sizeof(buf), "$%04X:%04X", s.CS, s.IP);
			AddVar(vars, "PC", buf);
			AddHex16(vars, "AX", s.AX);
			AddHex16(vars, "BX", s.BX);
			AddHex16(vars, "CX", s.CX);
			AddHex16(vars, "DX", s.DX);
			AddHex16(vars, "SI", s.SI);
			AddHex16(vars, "DI", s.DI);
			AddHex16(vars, "BP", s.BP);
			AddHex16(vars, "SP", s.SP);
			AddHex16(vars, "CS", s.CS);
			AddHex16(vars, "DS", s.DS);
			AddHex16(vars, "ES", s.ES);
			AddHex16(vars, "SS", s.SS);
			AddHex16(vars, "IP", s.IP);
			AddHex16(vars, "F", s.Flags.Get());
			break;
		}

		default:
			// Coprocessors (GSU, CX4, NEC DSP, ST018) have no serializer yet
			break;
	}

	return vars;
}

// ── Flags ────────────────────────────────────────────────────────

JsonValue CpuStateSerializer::SerializeFlags(CpuType cpuType, BaseState& state)
{
	JsonValue vars = JsonValue::MakeArray();

	switch(cpuType) {
		case CpuType::Snes:
		case CpuType::Sa1: {
			SnesCpuState& s = static_cast<SnesCpuState&>(state);
			uint8_t ps = s.PS;
			AddFlag(vars, "N (Negative)",  (ps & 0x80) != 0);
			AddFlag(vars, "V (Overflow)",  (ps & 0x40) != 0);
			AddFlag(vars, "M (Acc Size)",  (ps & 0x20) != 0);
			AddFlag(vars, "X (Idx Size)",  (ps & 0x10) != 0);
			AddFlag(vars, "D (Decimal)",   (ps & 0x08) != 0);
			AddFlag(vars, "I (IRQ Dis.)",  (ps & 0x04) != 0);
			AddFlag(vars, "Z (Zero)",      (ps & 0x02) != 0);
			AddFlag(vars, "C (Carry)",     (ps & 0x01) != 0);
			AddFlag(vars, "E (Emulation)", s.EmulationMode);
			break;
		}

		case CpuType::Spc: {
			SpcState& s = static_cast<SpcState&>(state);
			uint8_t ps = s.PS;
			AddFlag(vars, "N (Negative)",   (ps & SpcFlags::Negative) != 0);
			AddFlag(vars, "V (Overflow)",   (ps & SpcFlags::Overflow) != 0);
			AddFlag(vars, "P (Direct Pg.)", (ps & SpcFlags::DirectPage) != 0);
			AddFlag(vars, "B (Break)",      (ps & SpcFlags::Break) != 0);
			AddFlag(vars, "H (Half Carry)", (ps & SpcFlags::HalfCarry) != 0);
			AddFlag(vars, "I (IRQ Enable)", (ps & SpcFlags::IrqEnable) != 0);
			AddFlag(vars, "Z (Zero)",       (ps & SpcFlags::Zero) != 0);
			AddFlag(vars, "C (Carry)",      (ps & SpcFlags::Carry) != 0);
			break;
		}

		case CpuType::Nes: {
			NesCpuState& s = static_cast<NesCpuState&>(state);
			uint8_t ps = s.PS;
			AddFlag(vars, "N (Negative)",  (ps & 0x80) != 0);
			AddFlag(vars, "V (Overflow)",  (ps & 0x40) != 0);
			AddFlag(vars, "B (Break)",     (ps & 0x10) != 0);
			AddFlag(vars, "D (Decimal)",   (ps & 0x08) != 0);
			AddFlag(vars, "I (IRQ Dis.)",  (ps & 0x04) != 0);
			AddFlag(vars, "Z (Zero)",      (ps & 0x02) != 0);
			AddFlag(vars, "C (Carry)",     (ps & 0x01) != 0);
			break;
		}

		case CpuType::Gameboy: {
			GbCpuState& s = static_cast<GbCpuState&>(state);
			uint8_t f = s.Flags;
			AddFlag(vars, "Z (Zero)",       (f & GbCpuFlags::Zero) != 0);
			AddFlag(vars, "N (Add/Sub)",    (f & GbCpuFlags::AddSub) != 0);
			AddFlag(vars, "H (Half Carry)", (f & GbCpuFlags::HalfCarry) != 0);
			AddFlag(vars, "C (Carry)",      (f & GbCpuFlags::Carry) != 0);
			AddFlag(vars, "IME",            s.IME);
			AddFlag(vars, "Halted",         s.HaltCounter > 0);
			break;
		}

		case CpuType::Pce: {
			PceCpuState& s = static_cast<PceCpuState&>(state);
			uint8_t ps = s.PS;
			AddFlag(vars, "N (Negative)",   (ps & 0x80) != 0);
			AddFlag(vars, "V (Overflow)",   (ps & 0x40) != 0);
			AddFlag(vars, "T (Memory Op)",  (ps & 0x20) != 0);
			AddFlag(vars, "B (Break)",      (ps & 0x10) != 0);
			AddFlag(vars, "D (Decimal)",    (ps & 0x08) != 0);
			AddFlag(vars, "I (IRQ Dis.)",   (ps & 0x04) != 0);
			AddFlag(vars, "Z (Zero)",       (ps & 0x02) != 0);
			AddFlag(vars, "C (Carry)",      (ps & 0x01) != 0);
			break;
		}

		case CpuType::Sms: {
			SmsCpuState& s = static_cast<SmsCpuState&>(state);
			uint8_t f = s.Flags;
			AddFlag(vars, "S (Sign)",       (f & SmsCpuFlags::Sign) != 0);
			AddFlag(vars, "Z (Zero)",       (f & SmsCpuFlags::Zero) != 0);
			AddFlag(vars, "H (Half Carry)", (f & SmsCpuFlags::HalfCarry) != 0);
			AddFlag(vars, "P/V (Parity)",   (f & SmsCpuFlags::Parity) != 0);
			AddFlag(vars, "N (Add/Sub)",    (f & SmsCpuFlags::AddSub) != 0);
			AddFlag(vars, "C (Carry)",      (f & SmsCpuFlags::Carry) != 0);
			AddFlag(vars, "IFF1",           s.IFF1);
			AddFlag(vars, "IFF2",           s.IFF2);
			AddFlag(vars, "Halted",         s.Halted);
			break;
		}

		case CpuType::Gba: {
			GbaCpuState& s = static_cast<GbaCpuState&>(state);
			AddFlag(vars, "N (Negative)",  s.CPSR.Negative);
			AddFlag(vars, "Z (Zero)",      s.CPSR.Zero);
			AddFlag(vars, "C (Carry)",     s.CPSR.Carry);
			AddFlag(vars, "V (Overflow)",  s.CPSR.Overflow);
			AddFlag(vars, "I (IRQ Dis.)",  s.CPSR.IrqDisable);
			AddFlag(vars, "F (FIQ Dis.)",  s.CPSR.FiqDisable);
			AddFlag(vars, "T (Thumb)",     s.CPSR.Thumb);
			AddVar(vars, "Mode", GbaModeName(s.CPSR.Mode));
			break;
		}

		case CpuType::Ws: {
			WsCpuState& s = static_cast<WsCpuState&>(state);
			AddFlag(vars, "C (Carry)",      s.Flags.Carry);
			AddFlag(vars, "P (Parity)",     s.Flags.Parity);
			AddFlag(vars, "A (Aux Carry)",  s.Flags.AuxCarry);
			AddFlag(vars, "Z (Zero)",       s.Flags.Zero);
			AddFlag(vars, "S (Sign)",       s.Flags.Sign);
			AddFlag(vars, "T (Trap)",       s.Flags.Trap);
			AddFlag(vars, "I (IRQ Enable)", s.Flags.Irq);
			AddFlag(vars, "D (Direction)",  s.Flags.Direction);
			AddFlag(vars, "O (Overflow)",   s.Flags.Overflow);
			AddFlag(vars, "Halted",         s.Halted);
			break;
		}

		default:
			break;
	}

	return vars;
}

JsonValue CpuStateSerializer::SerializeCpuState(CpuType cpuType, BaseState& state)
{
	JsonValue obj = JsonValue::MakeObject();
	obj.Set("registers", SerializeRegisters(cpuType, state));
	obj.Set("flags", SerializeFlags(cpuType, state));
	return obj;
}
