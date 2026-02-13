#include "pch.h"
#include "batch_runner.h"
#include "cli_notification.h"
#include "formatter.h"
#include "console_info.h"
#include "Shared/Emulator.h"
#include "Shared/DebuggerRequest.h"
#include "Debugger/Debugger.h"
#include "Debugger/MemoryDumper.h"
#include "NES/NesTypes.h"
#include "SNES/SnesCpuTypes.h"
#include "Gameboy/GbTypes.h"
#include "GBA/GbaTypes.h"
#include "PCE/PceTypes.h"
#include "SMS/SmsTypes.h"
#include "WS/WsTypes.h"
#include "Shared/MemoryType.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>

BatchRunner::BatchRunner(Emulator* emu, std::shared_ptr<CliNotificationListener> listener,
                         CpuType primaryCpu, ConsoleType consoleType,
                         bool jsonOutput, int timeoutMs)
	: _emu(emu), _listener(listener), _primaryCpu(primaryCpu),
	  _consoleType(consoleType), _jsonOutput(jsonOutput), _timeoutMs(timeoutMs)
{
}

void BatchRunner::AddAssertion(const BatchAssertion& assertion)
{
	_assertions.push_back(assertion);
}

void BatchRunner::AddDump(int memType, const std::string& filename)
{
	_dumps.push_back({memType, filename});
}

static bool iequals(const std::string& a, const std::string& b)
{
	if(a.size() != b.size()) return false;
	for(size_t i = 0; i < a.size(); i++) {
		if(tolower(a[i]) != tolower(b[i])) return false;
	}
	return true;
}

uint16_t BatchRunner::GetRegisterValue(const std::string& name, const uint8_t* stateBuffer, bool& found)
{
	found = true;

	switch(_primaryCpu) {
		case CpuType::Nes: {
			const NesCpuState& s = *reinterpret_cast<const NesCpuState*>(stateBuffer);
			if(iequals(name, "A")) return s.A;
			if(iequals(name, "X")) return s.X;
			if(iequals(name, "Y")) return s.Y;
			if(iequals(name, "SP")) return s.SP;
			if(iequals(name, "PC")) return s.PC;
			if(iequals(name, "PS")) return s.PS;
			break;
		}
		case CpuType::Snes:
		case CpuType::Sa1: {
			const SnesCpuState& s = *reinterpret_cast<const SnesCpuState*>(stateBuffer);
			if(iequals(name, "A")) return s.A;
			if(iequals(name, "X")) return s.X;
			if(iequals(name, "Y")) return s.Y;
			if(iequals(name, "SP")) return s.SP;
			if(iequals(name, "D")) return s.D;
			if(iequals(name, "DBR")) return s.DBR;
			if(iequals(name, "PS")) return s.PS;
			if(iequals(name, "PC")) return s.PC;
			if(iequals(name, "K")) return s.K;
			break;
		}
		case CpuType::Gameboy: {
			const GbCpuState& s = *reinterpret_cast<const GbCpuState*>(stateBuffer);
			if(iequals(name, "A")) return s.A;
			if(iequals(name, "F")) return s.Flags;
			if(iequals(name, "B")) return s.B;
			if(iequals(name, "C")) return s.C;
			if(iequals(name, "D")) return s.D;
			if(iequals(name, "E")) return s.E;
			if(iequals(name, "H")) return s.H;
			if(iequals(name, "L")) return s.L;
			if(iequals(name, "SP")) return s.SP;
			if(iequals(name, "PC")) return s.PC;
			break;
		}
		case CpuType::Gba: {
			const GbaCpuState& s = *reinterpret_cast<const GbaCpuState*>(stateBuffer);
			// R0-R15
			if(name.size() >= 2 && (name[0] == 'R' || name[0] == 'r')) {
				int idx = std::stoi(name.substr(1));
				if(idx >= 0 && idx <= 15) return (uint16_t)s.R[idx];
			}
			if(iequals(name, "SP")) return (uint16_t)s.R[13];
			if(iequals(name, "LR")) return (uint16_t)s.R[14];
			if(iequals(name, "PC")) return (uint16_t)s.R[15];
			break;
		}
		case CpuType::Pce: {
			const PceCpuState& s = *reinterpret_cast<const PceCpuState*>(stateBuffer);
			if(iequals(name, "A")) return s.A;
			if(iequals(name, "X")) return s.X;
			if(iequals(name, "Y")) return s.Y;
			if(iequals(name, "SP")) return s.SP;
			if(iequals(name, "PC")) return s.PC;
			if(iequals(name, "PS")) return s.PS;
			break;
		}
		case CpuType::Sms: {
			const SmsCpuState& s = *reinterpret_cast<const SmsCpuState*>(stateBuffer);
			if(iequals(name, "A")) return s.A;
			if(iequals(name, "F")) return s.Flags;
			if(iequals(name, "B")) return s.B;
			if(iequals(name, "C")) return s.C;
			if(iequals(name, "D")) return s.D;
			if(iequals(name, "E")) return s.E;
			if(iequals(name, "H")) return s.H;
			if(iequals(name, "L")) return s.L;
			if(iequals(name, "SP")) return s.SP;
			if(iequals(name, "PC")) return s.PC;
			if(iequals(name, "IX")) return (s.IXH << 8) | s.IXL;
			if(iequals(name, "IY")) return (s.IYH << 8) | s.IYL;
			break;
		}
		case CpuType::Ws: {
			const WsCpuState& s = *reinterpret_cast<const WsCpuState*>(stateBuffer);
			if(iequals(name, "AX")) return s.AX;
			if(iequals(name, "BX")) return s.BX;
			if(iequals(name, "CX")) return s.CX;
			if(iequals(name, "DX")) return s.DX;
			if(iequals(name, "SP")) return s.SP;
			if(iequals(name, "BP")) return s.BP;
			if(iequals(name, "SI")) return s.SI;
			if(iequals(name, "DI")) return s.DI;
			if(iequals(name, "CS")) return s.CS;
			if(iequals(name, "IP")) return s.IP;
			if(iequals(name, "DS")) return s.DS;
			if(iequals(name, "ES")) return s.ES;
			if(iequals(name, "SS")) return s.SS;
			break;
		}
		default:
			break;
	}

	found = false;
	return 0;
}

int BatchRunner::Run()
{
	// Resume execution -- the debugger paused on initial step
	{
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(!dbg) {
			std::cerr << "Error: debugger not initialized\n";
			return 2;
		}
		_listener->Reset();
		dbg->Run();
	}

	// Wait for break (breakpoint hit, WAI, STP, or timeout)
	bool hitBreak = _listener->WaitForBreak(_timeoutMs);

	if(!hitBreak) {
		std::cerr << "Error: timeout after " << _timeoutMs << "ms\n";
		return 2;
	}

	// Read CPU state
	uint8_t stateBuffer[512] = {};
	BaseState& state = *reinterpret_cast<BaseState*>(stateBuffer);
	{
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(!dbg) {
			std::cerr << "Error: debugger lost\n";
			return 2;
		}
		dbg->GetCpuState(state, _primaryCpu);
	}

	// Output state
	if(_jsonOutput) {
		std::cout << Formatter::FormatRegistersJson(_primaryCpu, state) << std::endl;
	} else {
		std::cout << Formatter::FormatRegisters(_primaryCpu, state) << std::endl;
	}

	// Dump memory regions if requested
	for(auto& d : _dumps) {
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(!dbg) continue;

		MemoryType mt = (MemoryType)d.memType;
		uint32_t size = dbg->GetMemoryDumper()->GetMemorySize(mt);
		if(size == 0) {
			std::cerr << "Warning: memory type not available for dump to " << d.filename << "\n";
			continue;
		}

		std::vector<uint8_t> buf(size);
		dbg->GetMemoryDumper()->GetMemoryState(mt, buf.data());

		std::ofstream out(d.filename, std::ios::binary);
		if(!out) {
			std::cerr << "Error: could not open " << d.filename << " for writing\n";
			continue;
		}
		out.write(reinterpret_cast<char*>(buf.data()), size);
		if(!_jsonOutput) {
			fprintf(stderr, "Dumped %u bytes to %s\n", size, d.filename.c_str());
		}
	}

	// Run assertions
	if(_assertions.empty()) {
		return 0;
	}

	MemoryType cpuMemType = ConsoleInfo::GetCpuMemoryType(_primaryCpu);
	bool allPassed = true;
	for(auto& check : _assertions) {
		uint16_t actual = 0;
		std::string label;

		if(check.type == BatchAssertion::Type::Reg) {
			label = check.name;
			bool found = false;
			actual = GetRegisterValue(check.name, stateBuffer, found);
			if(!found) {
				std::cerr << "Unknown register: " << check.name << "\n";
				allPassed = false;
				continue;
			}
		} else {
			char addrBuf[16];
			snprintf(addrBuf, sizeof(addrBuf), "[$%06X]", check.address);
			label = addrBuf;

			DebuggerRequest req = _emu->GetDebugger(false);
			Debugger* dbg = req.GetDebugger();
			if(!dbg) { allPassed = false; continue; }

			if(check.size == 2) {
				actual = dbg->GetMemoryDumper()->GetMemoryValue16(cpuMemType, check.address);
			} else {
				actual = dbg->GetMemoryDumper()->GetMemoryValue(cpuMemType, check.address);
			}
		}

		if(actual != check.expected) {
			if(_jsonOutput) {
				fprintf(stderr, "{\"assertion_failed\":\"%s\",\"expected\":%d,\"actual\":%d}\n",
					label.c_str(), check.expected, actual);
			} else {
				fprintf(stderr, "FAIL: %s = $%04X (expected $%04X)\n",
					label.c_str(), actual, check.expected);
			}
			allPassed = false;
		} else {
			if(!_jsonOutput) {
				fprintf(stdout, "PASS: %s = $%04X\n", label.c_str(), actual);
			}
		}
	}

	return allPassed ? 0 : 1;
}
