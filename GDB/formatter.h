#pragma once
#include <string>
#include <cstdint>
#include "Shared/CpuType.h"
#include "Shared/BaseState.h"
#include "Debugger/DebugTypes.h"

namespace Formatter {
	std::string FormatRegisters(CpuType cpu, const BaseState& state);
	std::string FormatRegistersJson(CpuType cpu, const BaseState& state);
	std::string FormatMemoryHex(const uint8_t* data, uint32_t len, uint32_t startAddr);
	std::string FormatDisassembly(CodeLineData* lines, uint32_t count);
	std::string FormatCallstack(StackFrameInfo* frames, uint32_t count);
}
