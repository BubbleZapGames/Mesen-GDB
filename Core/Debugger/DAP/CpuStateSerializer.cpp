#include "CpuStateSerializer.h"
#include "DapJson.h"
#include "Core/Shared/BaseState.h"
#include "Core/SNES/SnesCpuTypes.h"
#include "Core/Shared/CpuType.h"
#include <cstdio>

JsonValue CpuStateSerializer::SerializeRegisters(CpuType cpuType, BaseState& state)
{
	JsonValue variables = JsonValue::MakeArray();

	if(cpuType == CpuType::Snes || cpuType == CpuType::Sa1) {
		SnesCpuState& s = static_cast<SnesCpuState&>(state);

		auto addReg = [&variables](const char* name, const char* value) {
			JsonValue var = JsonValue::MakeObject();
			var.Set("name", JsonValue::MakeString(name));
			var.Set("value", JsonValue::MakeString(value));
			var.Set("variablesReference", JsonValue::MakeNumber(0));
			variables.Push(std::move(var));
		};

		char buf[16];

		snprintf(buf, sizeof(buf), "$%02X:%04X", s.K, s.PC);
		addReg("PC", buf);

		snprintf(buf, sizeof(buf), "$%04X", s.A);
		addReg("A", buf);

		snprintf(buf, sizeof(buf), "$%04X", s.X);
		addReg("X", buf);

		snprintf(buf, sizeof(buf), "$%04X", s.Y);
		addReg("Y", buf);

		snprintf(buf, sizeof(buf), "$%04X", s.SP);
		addReg("SP", buf);

		snprintf(buf, sizeof(buf), "$%04X", s.D);
		addReg("D", buf);

		snprintf(buf, sizeof(buf), "$%02X", s.K);
		addReg("K", buf);

		snprintf(buf, sizeof(buf), "$%02X", s.DBR);
		addReg("DBR", buf);

		snprintf(buf, sizeof(buf), "$%02X", s.PS);
		addReg("P", buf);
	}

	return variables;
}

JsonValue CpuStateSerializer::SerializeFlags(CpuType cpuType, BaseState& state)
{
	JsonValue variables = JsonValue::MakeArray();

	if(cpuType == CpuType::Snes || cpuType == CpuType::Sa1) {
		SnesCpuState& s = static_cast<SnesCpuState&>(state);
		uint8_t ps = s.PS;

		auto addFlag = [&variables](const char* name, bool value) {
			JsonValue var = JsonValue::MakeObject();
			var.Set("name", JsonValue::MakeString(name));
			var.Set("value", JsonValue::MakeString(value ? "1" : "0"));
			var.Set("variablesReference", JsonValue::MakeNumber(0));
			variables.Push(std::move(var));
		};

		addFlag("N (Negative)",  (ps & 0x80) != 0);
		addFlag("V (Overflow)",  (ps & 0x40) != 0);
		addFlag("M (Acc Size)",  (ps & 0x20) != 0);
		addFlag("X (Idx Size)",  (ps & 0x10) != 0);
		addFlag("D (Decimal)",   (ps & 0x08) != 0);
		addFlag("I (IRQ Dis.)",  (ps & 0x04) != 0);
		addFlag("Z (Zero)",      (ps & 0x02) != 0);
		addFlag("C (Carry)",     (ps & 0x01) != 0);

		addFlag("E (Emulation)", s.EmulationMode);
	}

	return variables;
}

JsonValue CpuStateSerializer::SerializeCpuState(CpuType cpuType, BaseState& state)
{
	JsonValue obj = JsonValue::MakeObject();
	obj.Set("registers", SerializeRegisters(cpuType, state));
	obj.Set("flags", SerializeFlags(cpuType, state));
	return obj;
}
