#pragma once
#include <string>
#include <cstdint>
#include "DapJson.h"

struct BaseState;
enum class CpuType : uint8_t;

class CpuStateSerializer {
public:
	static JsonValue SerializeRegisters(CpuType cpuType, BaseState& state);
	static JsonValue SerializeFlags(CpuType cpuType, BaseState& state);
	static JsonValue SerializeCpuState(CpuType cpuType, BaseState& state);
};
