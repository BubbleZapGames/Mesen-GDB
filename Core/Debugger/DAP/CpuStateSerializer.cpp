#include "CpuStateSerializer.h"
#include "DapJson.h"
#include "Core/Shared/BaseState.h"
#include "Core/SNES/SnesCpuTypes.h"
#include "Core/Shared/CpuType.h"

JsonValue CpuStateSerializer::SerializeRegisters(CpuType cpuType, BaseState& state) {
	JsonValue variables = JsonValue::MakeArray();
	
	if(cpuType == CpuType::Snes) {
		SnesCpuState& snesState = static_cast<SnesCpuState&>(state);
		
		auto addReg = [&variables](const char* name, uint32_t value) {
			JsonValue var = JsonValue::MakeObject();
			var.Set("name", JsonValue::MakeString(name));
			char buf[16];
			snprintf(buf, sizeof(buf), "0x%02X", (uint8_t)value);
			var.Set("value", JsonValue::MakeString(buf));
			var.Set(".variablesReference", JsonValue::MakeNumber(0.0));
			variables.Push(std::move(var));
		};
		
		addReg("A", snesState.A);
		addReg("X", snesState.X);
		addReg("Y", snesState.Y);
		addReg("SP", snesState.SP);
		addReg("PC", snesState.PC);
	}
	
	return variables;
}

JsonValue CpuStateSerializer::SerializeFlags(CpuType cpuType, BaseState& state) {
	JsonValue variables = JsonValue::MakeArray();
	
	if(cpuType == CpuType::Snes) {
		SnesCpuState& snesState = static_cast<SnesCpuState&>(state);
		uint8_t ps = snesState.PS;
		
		auto addFlag = [&variables](const char* name, bool value) {
			JsonValue var = JsonValue::MakeObject();
			var.Set("name", JsonValue::MakeString(name));
			var.Set("value", JsonValue::MakeBool(value));
			var.Set("variablesReference", JsonValue::MakeNumber(0.0));
			variables.Push(std::move(var));
		};
		
		addFlag("N", (ps & 0x80) != 0);
		addFlag("V", (ps & 0x40) != 0);
		addFlag("M", (ps & 0x20) != 0);
		addFlag("X", (ps & 0x10) != 0);
		addFlag("D", (ps & 0x08) != 0);
		addFlag("I", (ps & 0x04) != 0);
		addFlag("Z", (ps & 0x02) != 0);
		addFlag("C", (ps & 0x01) != 0);
	}
	
	return variables;
}

JsonValue CpuStateSerializer::SerializeCpuState(CpuType cpuType, BaseState& state) {
	JsonValue obj = JsonValue::MakeObject();
	obj.Set("registers", SerializeRegisters(cpuType, state));
	obj.Set("flags", SerializeFlags(cpuType, state));
	return obj;
}
