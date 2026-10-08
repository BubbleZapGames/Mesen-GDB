#include "test_harness.h"
#include "Core/Debugger/DAP/CpuStateSerializer.h"
#include "Core/Debugger/DAP/DapJson.h"
#include "Core/Shared/CpuType.h"
#include "Core/SNES/SnesCpuTypes.h"
#include "Core/SNES/SpcTypes.h"
#include "Core/NES/NesTypes.h"
#include "Core/Gameboy/GbTypes.h"
#include "Core/PCE/PceTypes.h"
#include "Core/SMS/SmsTypes.h"
#include "Core/GBA/GbaTypes.h"
#include "Core/WS/WsTypes.h"

// Returns the "value" of the named variable, or "" if it isn't in the array
static std::string FindVar(const JsonValue& vars, const std::string& name)
{
	for(const JsonValue& v : vars.GetArray()) {
		if(v["name"].GetString() == name) {
			return v["value"].GetString();
		}
	}
	return "";
}

static bool HasVar(const JsonValue& vars, const std::string& name)
{
	for(const JsonValue& v : vars.GetArray()) {
		if(v["name"].GetString() == name) return true;
	}
	return false;
}

// Every emitted variable must be a well-formed DAP Variable
static void CheckVariableShape(const JsonValue& vars)
{
	ASSERT_EQ((int)vars.GetType(), (int)JsonValue::Type::Array);
	ASSERT_TRUE(!vars.GetArray().empty());
	for(const JsonValue& v : vars.GetArray()) {
		ASSERT_EQ((int)v.GetType(), (int)JsonValue::Type::Object);
		ASSERT_TRUE(!v["name"].GetString().empty());
		ASSERT_EQ((int)v["value"].GetType(), (int)JsonValue::Type::String);
		ASSERT_EQ(v["variablesReference"].GetInt(), 0);
	}
}

TEST(serializer_snes)
{
	SnesCpuState s = {};
	s.PC = 0x8123; s.K = 0x01; s.A = 0x1234; s.X = 0x00FF; s.Y = 0x0010;
	s.SP = 0x01FF; s.D = 0x0000; s.DBR = 0x7E; s.PS = 0xA5; s.EmulationMode = false;

	JsonValue regs = CpuStateSerializer::SerializeRegisters(CpuType::Snes, s);
	CheckVariableShape(regs);
	ASSERT_STR_EQ(FindVar(regs, "PC"), "$01:8123");
	ASSERT_STR_EQ(FindVar(regs, "A"), "$1234");
	ASSERT_STR_EQ(FindVar(regs, "DBR"), "$7E");

	JsonValue flags = CpuStateSerializer::SerializeFlags(CpuType::Snes, s);
	CheckVariableShape(flags);
	ASSERT_STR_EQ(FindVar(flags, "N (Negative)"), "1");   // 0xA5 bit 7
	ASSERT_STR_EQ(FindVar(flags, "V (Overflow)"), "0");   // 0xA5 bit 6
	ASSERT_STR_EQ(FindVar(flags, "C (Carry)"), "1");      // 0xA5 bit 0
	ASSERT_STR_EQ(FindVar(flags, "E (Emulation)"), "0");
}

TEST(serializer_sa1_matches_snes)
{
	SnesCpuState s = {};
	s.PC = 0x9000; s.K = 0x00; s.A = 0x0042;
	JsonValue regs = CpuStateSerializer::SerializeRegisters(CpuType::Sa1, s);
	CheckVariableShape(regs);
	ASSERT_STR_EQ(FindVar(regs, "PC"), "$00:9000");
	ASSERT_STR_EQ(FindVar(regs, "A"), "$0042");
}

TEST(serializer_spc)
{
	SpcState s = {};
	s.PC = 0x0500; s.A = 0x12; s.X = 0x34; s.Y = 0x56; s.SP = 0xEF;
	s.PS = SpcFlags::Negative | SpcFlags::Carry;

	JsonValue regs = CpuStateSerializer::SerializeRegisters(CpuType::Spc, s);
	CheckVariableShape(regs);
	ASSERT_STR_EQ(FindVar(regs, "PC"), "$0500");
	ASSERT_STR_EQ(FindVar(regs, "A"), "$12");
	ASSERT_STR_EQ(FindVar(regs, "YA"), "$5612");
	ASSERT_STR_EQ(FindVar(regs, "SP"), "$EF");

	JsonValue flags = CpuStateSerializer::SerializeFlags(CpuType::Spc, s);
	CheckVariableShape(flags);
	ASSERT_STR_EQ(FindVar(flags, "N (Negative)"), "1");
	ASSERT_STR_EQ(FindVar(flags, "C (Carry)"), "1");
	ASSERT_STR_EQ(FindVar(flags, "Z (Zero)"), "0");
}

TEST(serializer_nes)
{
	NesCpuState s = {};
	s.PC = 0xC000; s.A = 0x01; s.X = 0x02; s.Y = 0x03; s.SP = 0xFD; s.PS = 0x24;

	JsonValue regs = CpuStateSerializer::SerializeRegisters(CpuType::Nes, s);
	CheckVariableShape(regs);
	ASSERT_STR_EQ(FindVar(regs, "PC"), "$C000");
	ASSERT_STR_EQ(FindVar(regs, "SP"), "$FD");
	// The 6502 has no 16-bit accumulator mode, so no M/X size flags
	JsonValue flags = CpuStateSerializer::SerializeFlags(CpuType::Nes, s);
	CheckVariableShape(flags);
	ASSERT_STR_EQ(FindVar(flags, "I (IRQ Dis.)"), "1");   // 0x24 bit 2
	ASSERT_STR_EQ(FindVar(flags, "N (Negative)"), "0");
	ASSERT_FALSE(HasVar(flags, "M (Acc Size)"));
}

TEST(serializer_gameboy)
{
	GbCpuState s = {};
	s.PC = 0x0150; s.SP = 0xFFFE; s.A = 0x11; s.Flags = GbCpuFlags::Zero | GbCpuFlags::Carry;
	s.B = 0x00; s.C = 0x13; s.D = 0x00; s.E = 0xD8; s.H = 0x01; s.L = 0x4D;
	s.IME = true; s.HaltCounter = 0;

	JsonValue regs = CpuStateSerializer::SerializeRegisters(CpuType::Gameboy, s);
	CheckVariableShape(regs);
	ASSERT_STR_EQ(FindVar(regs, "PC"), "$0150");
	ASSERT_STR_EQ(FindVar(regs, "BC"), "$0013");
	ASSERT_STR_EQ(FindVar(regs, "DE"), "$00D8");
	ASSERT_STR_EQ(FindVar(regs, "HL"), "$014D");
	ASSERT_STR_EQ(FindVar(regs, "AF"), "$1190");

	JsonValue flags = CpuStateSerializer::SerializeFlags(CpuType::Gameboy, s);
	CheckVariableShape(flags);
	ASSERT_STR_EQ(FindVar(flags, "Z (Zero)"), "1");
	ASSERT_STR_EQ(FindVar(flags, "C (Carry)"), "1");
	ASSERT_STR_EQ(FindVar(flags, "N (Add/Sub)"), "0");
	ASSERT_STR_EQ(FindVar(flags, "IME"), "1");
}

TEST(serializer_pce)
{
	PceCpuState s = {};
	s.PC = 0xE000; s.A = 0xAB; s.X = 0xCD; s.Y = 0xEF; s.SP = 0xFF; s.PS = 0x20;

	JsonValue regs = CpuStateSerializer::SerializeRegisters(CpuType::Pce, s);
	CheckVariableShape(regs);
	ASSERT_STR_EQ(FindVar(regs, "PC"), "$E000");
	ASSERT_STR_EQ(FindVar(regs, "A"), "$AB");

	JsonValue flags = CpuStateSerializer::SerializeFlags(CpuType::Pce, s);
	CheckVariableShape(flags);
	// The HuC6280 replaces the 6502's unused bit 5 with the T (memory op) flag
	ASSERT_STR_EQ(FindVar(flags, "T (Memory Op)"), "1");
}

TEST(serializer_sms)
{
	SmsCpuState s = {};
	s.PC = 0x0038; s.SP = 0xDFF0; s.A = 0x7F;
	s.Flags = SmsCpuFlags::Zero | SmsCpuFlags::Carry;
	s.B = 0x12; s.C = 0x34; s.D = 0x56; s.E = 0x78; s.H = 0x9A; s.L = 0xBC;
	s.IXH = 0x11; s.IXL = 0x22; s.IYH = 0x33; s.IYL = 0x44;
	s.AltA = 0x01; s.AltFlags = 0x02;
	s.I = 0x00; s.R = 0x7E; s.IM = 1; s.IFF1 = true; s.IFF2 = false; s.Halted = false;

	JsonValue regs = CpuStateSerializer::SerializeRegisters(CpuType::Sms, s);
	CheckVariableShape(regs);
	ASSERT_STR_EQ(FindVar(regs, "PC"), "$0038");
	ASSERT_STR_EQ(FindVar(regs, "BC"), "$1234");
	ASSERT_STR_EQ(FindVar(regs, "IX"), "$1122");
	ASSERT_STR_EQ(FindVar(regs, "IY"), "$3344");
	ASSERT_STR_EQ(FindVar(regs, "AF'"), "$0102");
	ASSERT_STR_EQ(FindVar(regs, "IM"), "1");

	JsonValue flags = CpuStateSerializer::SerializeFlags(CpuType::Sms, s);
	CheckVariableShape(flags);
	ASSERT_STR_EQ(FindVar(flags, "Z (Zero)"), "1");
	ASSERT_STR_EQ(FindVar(flags, "C (Carry)"), "1");
	ASSERT_STR_EQ(FindVar(flags, "IFF1"), "1");
	ASSERT_STR_EQ(FindVar(flags, "IFF2"), "0");
}

TEST(serializer_gba)
{
	GbaCpuState s = {};
	for(int i = 0; i < 16; i++) {
		s.R[i] = 0x1000 + i;
	}
	s.R[15] = 0x08000108;
	s.Pipeline.Execute.Address = 0x08000100;
	s.CPSR.Mode = GbaCpuMode::Supervisor;
	s.CPSR.Thumb = false;
	s.CPSR.Zero = true;
	s.CPSR.IrqDisable = true;

	JsonValue regs = CpuStateSerializer::SerializeRegisters(CpuType::Gba, s);
	CheckVariableShape(regs);
	// PC reports the executing instruction, not the pipeline-ahead R15
	ASSERT_STR_EQ(FindVar(regs, "PC"), "$08000100");
	ASSERT_STR_EQ(FindVar(regs, "R15"), "$08000108");
	ASSERT_STR_EQ(FindVar(regs, "R0"), "$00001000");
	ASSERT_STR_EQ(FindVar(regs, "SP"), "$0000100D");
	ASSERT_STR_EQ(FindVar(regs, "LR"), "$0000100E");

	JsonValue flags = CpuStateSerializer::SerializeFlags(CpuType::Gba, s);
	CheckVariableShape(flags);
	ASSERT_STR_EQ(FindVar(flags, "Z (Zero)"), "1");
	ASSERT_STR_EQ(FindVar(flags, "N (Negative)"), "0");
	ASSERT_STR_EQ(FindVar(flags, "T (Thumb)"), "0");
	ASSERT_STR_EQ(FindVar(flags, "I (IRQ Dis.)"), "1");
	ASSERT_STR_EQ(FindVar(flags, "Mode"), "Supervisor");
}

TEST(serializer_ws)
{
	WsCpuState s = {};
	s.CS = 0xF000; s.IP = 0x0100; s.SS = 0x0000; s.SP = 0x2000; s.BP = 0x1234;
	s.DS = 0x0001; s.ES = 0x0002; s.SI = 0x0003; s.DI = 0x0004;
	s.AX = 0xAAAA; s.BX = 0xBBBB; s.CX = 0xCCCC; s.DX = 0xDDDD;
	s.Flags.Carry = true; s.Flags.Zero = true; s.Halted = false;

	JsonValue regs = CpuStateSerializer::SerializeRegisters(CpuType::Ws, s);
	CheckVariableShape(regs);
	ASSERT_STR_EQ(FindVar(regs, "PC"), "$F000:0100");
	ASSERT_STR_EQ(FindVar(regs, "AX"), "$AAAA");
	ASSERT_STR_EQ(FindVar(regs, "DX"), "$DDDD");
	ASSERT_STR_EQ(FindVar(regs, "BP"), "$1234");

	JsonValue flags = CpuStateSerializer::SerializeFlags(CpuType::Ws, s);
	CheckVariableShape(flags);
	ASSERT_STR_EQ(FindVar(flags, "C (Carry)"), "1");
	ASSERT_STR_EQ(FindVar(flags, "Z (Zero)"), "1");
	ASSERT_STR_EQ(FindVar(flags, "S (Sign)"), "0");
}

TEST(serializer_combined_state)
{
	NesCpuState s = {};
	s.PC = 0x8000;
	JsonValue obj = CpuStateSerializer::SerializeCpuState(CpuType::Nes, s);
	ASSERT_EQ((int)obj.GetType(), (int)JsonValue::Type::Object);
	CheckVariableShape(obj["registers"]);
	CheckVariableShape(obj["flags"]);
}

// Coprocessor CPUs have no serializer yet — they must return empty arrays
// rather than misinterpreting the state struct.
TEST(serializer_unsupported_cpu_is_empty)
{
	NesCpuState s = {};
	JsonValue regs = CpuStateSerializer::SerializeRegisters(CpuType::Gsu, s);
	ASSERT_EQ((int)regs.GetType(), (int)JsonValue::Type::Array);
	ASSERT_TRUE(regs.GetArray().empty());
	JsonValue flags = CpuStateSerializer::SerializeFlags(CpuType::Cx4, s);
	ASSERT_TRUE(flags.GetArray().empty());
}
