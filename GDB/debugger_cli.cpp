#include "pch.h"
#include "debugger_cli.h"
#include "cli_notification.h"
#include "formatter.h"
#include "console_info.h"
#include "Shared/Emulator.h"
#include "Shared/DebuggerRequest.h"
#include "Debugger/Debugger.h"
#include "Debugger/Breakpoint.h"
#include "Debugger/MemoryDumper.h"
#include "Debugger/Disassembler.h"
#include "Debugger/CallstackManager.h"
#include "Debugger/TraceLogFileSaver.h"
#include "Debugger/DebugUtilities.h"
#include "Shared/MemoryType.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <algorithm>

// We need to populate Breakpoint's private fields directly.
// This struct mirrors the exact memory layout of Breakpoint.
struct BreakpointData {
	uint32_t id;
	CpuType cpuType;           // uint8_t
	uint8_t _pad1[3];          // padding to align MemoryType
	MemoryType memoryType;     // int (4 bytes)
	BreakpointTypeFlags type;  // int (4 bytes)
	int32_t startAddr;
	int32_t endAddr;
	bool enabled;
	bool markEvent;
	bool ignoreDummyOperations;
	char _pad2;                // padding
	char condition[1000];
};

DebuggerCli::DebuggerCli(Emulator* emu, std::shared_ptr<CliNotificationListener> listener,
                         CpuType primaryCpu, ConsoleType consoleType, bool jsonOutput)
	: _emu(emu), _listener(listener), _primaryCpu(primaryCpu),
	  _consoleType(consoleType), _jsonOutput(jsonOutput)
{
}

void DebuggerCli::AddInitialBreakpoint(uint32_t addr)
{
	CliBreakpoint bp;
	bp.id = _nextBreakpointId++;
	bp.address = addr;
	bp.isWatch = false;
	bp.enabled = true;
	_breakpoints.push_back(bp);
}

std::vector<std::string> DebuggerCli::Tokenize(const std::string& line)
{
	std::vector<std::string> tokens;
	std::istringstream iss(line);
	std::string token;
	while(iss >> token) {
		tokens.push_back(token);
	}
	return tokens;
}

uint32_t DebuggerCli::ParseAddress(const std::string& str)
{
	// Accept formats: 0x1234, $1234, 1234 (hex), 00:8000 (bank:addr)
	std::string s = str;

	// bank:addr format
	size_t colon = s.find(':');
	if(colon != std::string::npos) {
		uint32_t bank = std::stoul(s.substr(0, colon), nullptr, 16);
		uint32_t addr = std::stoul(s.substr(colon + 1), nullptr, 16);
		return (bank << 16) | (addr & 0xFFFF);
	}

	// Strip prefix
	if(s.size() > 1 && s[0] == '$') s = s.substr(1);
	else if(s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);

	return std::stoul(s, nullptr, 16);
}

void DebuggerCli::SyncBreakpoints()
{
	MemoryType cpuMemType = ConsoleInfo::GetCpuMemoryType(_primaryCpu);

	// Build array of Breakpoint objects from our tracked breakpoints
	std::vector<Breakpoint> bps;
	for(auto& cbp : _breakpoints) {
		if(!cbp.enabled) continue;

		Breakpoint bp;
		static_assert(sizeof(BreakpointData) == sizeof(Breakpoint),
			"BreakpointData layout must match Breakpoint");
		BreakpointData* data = reinterpret_cast<BreakpointData*>(&bp);

		memset(data, 0, sizeof(BreakpointData));
		data->id = cbp.id;
		data->cpuType = _primaryCpu;
		data->memoryType = cpuMemType;
		data->type = cbp.isWatch ? BreakpointTypeFlags::Write : BreakpointTypeFlags::Execute;
		data->startAddr = (int32_t)cbp.address;
		data->endAddr = (int32_t)cbp.address;
		data->enabled = true;
		data->markEvent = false;
		data->ignoreDummyOperations = false;

		bps.push_back(bp);
	}

	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(dbg) {
		dbg->SetBreakpoints(bps.data(), (uint32_t)bps.size());
	}
}

void DebuggerCli::PrintState()
{
	// Allocate a buffer large enough for any CPU state
	uint8_t stateBuffer[512] = {};
	BaseState& state = *reinterpret_cast<BaseState*>(stateBuffer);
	{
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(!dbg) return;
		dbg->GetCpuState(state, _primaryCpu);
	}

	if(_jsonOutput) {
		std::cout << Formatter::FormatRegistersJson(_primaryCpu, state) << std::endl;
	} else {
		std::cout << Formatter::FormatRegisters(_primaryCpu, state) << std::endl;
		PrintDisassemblyAtPC(3);
	}
}

void DebuggerCli::PrintDisassemblyAtPC(int lines)
{
	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(!dbg) return;

	uint32_t pc = dbg->GetProgramCounter(_primaryCpu, true);
	std::vector<CodeLineData> output(lines);
	uint32_t count = dbg->GetDisassembler()->GetDisassemblyOutput(
		_primaryCpu, pc, output.data(), lines);
	std::cout << Formatter::FormatDisassembly(output.data(), count);
}

void DebuggerCli::CmdStep(int count)
{
	_listener->Reset();
	{
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(dbg) dbg->Step(_primaryCpu, count, StepType::Step);
	}
	_listener->WaitForBreak(5000);
	PrintState();
}

void DebuggerCli::CmdNext()
{
	_listener->Reset();
	{
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(dbg) dbg->Step(_primaryCpu, 1, StepType::StepOver);
	}
	_listener->WaitForBreak(5000);
	PrintState();
}

void DebuggerCli::CmdFinish()
{
	_listener->Reset();
	{
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(dbg) dbg->Step(_primaryCpu, 1, StepType::StepOut);
	}
	_listener->WaitForBreak(30000);
	PrintState();
}

void DebuggerCli::CmdRun()
{
	_listener->Reset();
	{
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(dbg) dbg->Run();
	}
	std::cout << "Running... (press Ctrl+C to interrupt)" << std::endl;
	_listener->WaitForBreak(0);  // wait indefinitely
	PrintState();
}

void DebuggerCli::CmdBreak(uint32_t addr)
{
	CliBreakpoint bp;
	bp.id = _nextBreakpointId++;
	bp.address = addr;
	bp.isWatch = false;
	bp.enabled = true;
	_breakpoints.push_back(bp);
	SyncBreakpoints();
	printf("Breakpoint %d at $%06X\n", bp.id, addr);
}

void DebuggerCli::CmdWatch(uint32_t addr)
{
	CliBreakpoint bp;
	bp.id = _nextBreakpointId++;
	bp.address = addr;
	bp.isWatch = true;
	bp.enabled = true;
	_breakpoints.push_back(bp);
	SyncBreakpoints();
	printf("Watchpoint %d at $%06X (write)\n", bp.id, addr);
}

void DebuggerCli::CmdDelete(uint32_t id)
{
	auto it = std::remove_if(_breakpoints.begin(), _breakpoints.end(),
		[id](const CliBreakpoint& bp) { return bp.id == id; });
	if(it == _breakpoints.end()) {
		printf("No breakpoint %d\n", id);
		return;
	}
	_breakpoints.erase(it, _breakpoints.end());
	SyncBreakpoints();
	printf("Deleted breakpoint %d\n", id);
}

void DebuggerCli::CmdInfoBreak()
{
	if(_breakpoints.empty()) {
		std::cout << "No breakpoints.\n";
		return;
	}
	printf("%-4s %-6s %-8s %s\n", "ID", "Type", "Address", "Enabled");
	for(auto& bp : _breakpoints) {
		printf("%-4d %-6s $%06X  %s\n", bp.id,
			bp.isWatch ? "watch" : "break",
			bp.address,
			bp.enabled ? "yes" : "no");
	}
}

void DebuggerCli::CmdInfoRegions()
{
	auto regions = ConsoleInfo::GetMemoryRegions(_consoleType);
	printf("Memory regions for %s:\n", ConsoleInfo::GetConsoleName(_consoleType));
	printf("%-12s %-24s %s\n", "Command", "Name", "Type");
	for(auto& r : regions) {
		printf("%-12s %-24s %d\n", r.shortName, r.name, (int)r.type);
	}
}

void DebuggerCli::CmdInfoCpu()
{
	printf("Console: %s\n", ConsoleInfo::GetConsoleName(_consoleType));
	printf("Primary CPU: %s\n", ConsoleInfo::GetCpuName(_primaryCpu));

	auto cpuTypes = _emu->GetCpuTypes();
	if(cpuTypes.size() > 1) {
		printf("Active CPUs:\n");
		for(auto cpu : cpuTypes) {
			printf("  %s%s\n", ConsoleInfo::GetCpuName(cpu),
				cpu == _primaryCpu ? " (primary)" : "");
		}
	}
}

void DebuggerCli::CmdRegs()
{
	PrintState();
}

void DebuggerCli::CmdMem(uint32_t addr, uint32_t len)
{
	MemoryType cpuMemType = ConsoleInfo::GetCpuMemoryType(_primaryCpu);
	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(!dbg) return;

	std::vector<uint8_t> buf(len);
	dbg->GetMemoryDumper()->GetMemoryValues(cpuMemType, addr, addr + len - 1, buf.data());
	std::cout << Formatter::FormatMemoryHex(buf.data(), len, addr);
}

void DebuggerCli::CmdMemTyped(uint32_t addr, uint32_t len, int memType, const char* label)
{
	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(!dbg) return;

	uint32_t size = dbg->GetMemoryDumper()->GetMemorySize((MemoryType)memType);
	if(size == 0) {
		printf("%s not available.\n", label);
		return;
	}
	if(addr >= size) {
		printf("Address $%04X out of range (size: $%X / %u bytes)\n", addr, size, size);
		return;
	}
	if(addr + len > size) {
		len = size - addr;
	}

	std::vector<uint8_t> buf(len);
	dbg->GetMemoryDumper()->GetMemoryValues((MemoryType)memType, addr, addr + len - 1, buf.data());
	std::cout << Formatter::FormatMemoryHex(buf.data(), len, addr);
}

void DebuggerCli::CmdDump(const std::string& type, const std::string& filename)
{
	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(!dbg) return;

	// Look up type in console-specific memory regions
	auto regions = ConsoleInfo::GetMemoryRegions(_consoleType);
	MemoryType memType = MemoryType::None;
	const char* label = nullptr;
	for(auto& r : regions) {
		if(type == r.shortName) {
			memType = r.type;
			label = r.name;
			break;
		}
	}

	if(memType == MemoryType::None) {
		printf("Unknown memory type: %s\nValid types for %s:", type.c_str(),
			ConsoleInfo::GetConsoleName(_consoleType));
		for(auto& r : regions) {
			printf(" %s", r.shortName);
		}
		printf("\n");
		return;
	}

	uint32_t size = dbg->GetMemoryDumper()->GetMemorySize(memType);
	if(size == 0) {
		printf("%s not available.\n", label);
		return;
	}

	std::vector<uint8_t> buf(size);
	dbg->GetMemoryDumper()->GetMemoryState(memType, buf.data());

	std::ofstream out(filename, std::ios::binary);
	if(!out) {
		printf("Failed to open %s for writing.\n", filename.c_str());
		return;
	}
	out.write(reinterpret_cast<char*>(buf.data()), size);
	out.close();
	printf("Dumped %u bytes of %s to %s\n", size, label, filename.c_str());
}

void DebuggerCli::CmdSet(uint32_t addr, uint8_t val)
{
	MemoryType cpuMemType = ConsoleInfo::GetCpuMemoryType(_primaryCpu);
	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(!dbg) return;

	dbg->GetMemoryDumper()->SetMemoryValue(cpuMemType, addr, val);
	printf("[$%06X] = $%02X\n", addr, val);
}

void DebuggerCli::CmdDisasm(uint32_t addr, int count)
{
	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(!dbg) return;

	std::vector<CodeLineData> output(count);
	uint32_t actual = dbg->GetDisassembler()->GetDisassemblyOutput(
		_primaryCpu, addr, output.data(), count);
	std::cout << Formatter::FormatDisassembly(output.data(), actual);
}

void DebuggerCli::CmdBacktrace()
{
	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(!dbg) return;

	CallstackManager* csm = dbg->GetCallstackManager(_primaryCpu);
	if(!csm) {
		std::cout << "Callstack not available.\n";
		return;
	}

	StackFrameInfo frames[64];
	uint32_t count = 0;
	csm->GetCallstack(frames, count);
	if(count == 0) {
		std::cout << "Empty callstack.\n";
		return;
	}
	std::cout << Formatter::FormatCallstack(frames, count);
}

void DebuggerCli::CmdFrames(int count)
{
	_listener->Reset();
	{
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(dbg) dbg->Step(_primaryCpu, count, StepType::PpuFrame);
	}
	_listener->WaitForBreak(count * 1000 + 5000);
	PrintState();
}

void DebuggerCli::CmdReset()
{
	_emu->Reset();
	// Wait a bit for the reset to take effect
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	PrintState();
}

void DebuggerCli::CmdTrace(const std::string& filename)
{
	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(!dbg) return;

	if(filename == "off") {
		dbg->GetTraceLogFileSaver()->StopLogging();
		std::cout << "Trace logging stopped.\n";
	} else {
		dbg->GetTraceLogFileSaver()->StartLogging(filename);
		std::cout << "Tracing to: " << filename << "\n";
	}
}

void DebuggerCli::CmdHelp()
{
	std::cout <<
		"Commands:\n"
		"  step [N]          Step N instructions (default 1)\n"
		"  s [N]             Alias for step\n"
		"  next              Step over (skip calls)\n"
		"  n                 Alias for next\n"
		"  finish            Step out (run to return)\n"
		"  run               Resume execution until breakpoint\n"
		"  c                 Alias for run (continue)\n"
		"  break <addr>      Set execution breakpoint\n"
		"  b <addr>          Alias for break\n"
		"  watch <addr>      Set write watchpoint\n"
		"  delete <id>       Delete breakpoint by ID\n"
		"  info break        List all breakpoints\n"
		"  info regions      List available memory regions\n"
		"  info cpu          Show console/CPU info\n"
		"  regs              Show CPU registers\n"
		"  r                 Alias for regs\n"
		"  mem <addr> [len]  Show CPU memory (default 256 bytes)\n"
		"  x <addr> [len]    Alias for mem\n";

	// Show console-specific memory region commands
	auto regions = ConsoleInfo::GetMemoryRegions(_consoleType);
	for(auto& r : regions) {
		printf("  %-8s <addr> [len] Show %s\n", r.shortName, r.name);
	}

	std::cout <<
		"  dump <type> <file> Dump memory to file (types: ";
	for(size_t i = 0; i < regions.size(); i++) {
		if(i > 0) std::cout << ", ";
		std::cout << regions[i].shortName;
	}
	std::cout << ")\n"
		"  set <addr> <val>  Set CPU memory byte\n"
		"  disasm [addr] [n] Disassemble (default: at PC, 10 lines)\n"
		"  d [addr] [n]      Alias for disasm\n"
		"  bt                Show callstack\n"
		"  frames <N>        Run N PPU frames\n"
		"  reset             Reset emulator\n"
		"  trace <file|off>  Start/stop trace logging\n"
		"  help              Show this help\n"
		"  quit              Exit debugger\n"
		"\n"
		"Address formats: $1234, 0x1234, 1234, 00:8000\n";
}

void DebuggerCli::Run()
{
	SyncBreakpoints();

	printf("Mesen CLI Debugger [%s / %s]. Type 'help' for commands.\n",
		ConsoleInfo::GetConsoleName(_consoleType),
		ConsoleInfo::GetCpuName(_primaryCpu));
	PrintState();

	// Build a set of memory region short names for dynamic command dispatch
	auto regions = ConsoleInfo::GetMemoryRegions(_consoleType);

	std::string line;
	while(!_quit) {
		std::cout << "(mesen) " << std::flush;
		if(!std::getline(std::cin, line)) {
			break;  // EOF
		}

		if(line.empty()) continue;

		auto tokens = Tokenize(line);
		if(tokens.empty()) continue;

		const std::string& cmd = tokens[0];

		try {
			if(cmd == "step" || cmd == "s") {
				int count = (tokens.size() > 1) ? std::stoi(tokens[1]) : 1;
				CmdStep(count);
			} else if(cmd == "next" || cmd == "n") {
				CmdNext();
			} else if(cmd == "finish") {
				CmdFinish();
			} else if(cmd == "run" || cmd == "c" || cmd == "continue") {
				CmdRun();
			} else if(cmd == "break" || cmd == "b") {
				if(tokens.size() < 2) { std::cout << "Usage: break <addr>\n"; continue; }
				CmdBreak(ParseAddress(tokens[1]));
			} else if(cmd == "watch") {
				if(tokens.size() < 2) { std::cout << "Usage: watch <addr>\n"; continue; }
				CmdWatch(ParseAddress(tokens[1]));
			} else if(cmd == "delete" || cmd == "del") {
				if(tokens.size() < 2) { std::cout << "Usage: delete <id>\n"; continue; }
				CmdDelete(std::stoul(tokens[1]));
			} else if(cmd == "info") {
				if(tokens.size() > 1 && tokens[1] == "break") {
					CmdInfoBreak();
				} else if(tokens.size() > 1 && tokens[1] == "regions") {
					CmdInfoRegions();
				} else if(tokens.size() > 1 && tokens[1] == "cpu") {
					CmdInfoCpu();
				} else {
					std::cout << "Usage: info break|regions|cpu\n";
				}
			} else if(cmd == "regs" || cmd == "r") {
				CmdRegs();
			} else if(cmd == "mem" || cmd == "x") {
				if(tokens.size() < 2) { std::cout << "Usage: mem <addr> [len]\n"; continue; }
				uint32_t addr = ParseAddress(tokens[1]);
				uint32_t len = (tokens.size() > 2) ? std::stoul(tokens[2]) : 256;
				CmdMem(addr, len);
			} else if(cmd == "dump") {
				if(tokens.size() < 3) {
					std::cout << "Usage: dump <type> <file>\nTypes:";
					for(auto& r : regions) std::cout << " " << r.shortName;
					std::cout << "\n";
					continue;
				}
				CmdDump(tokens[1], tokens[2]);
			} else if(cmd == "set") {
				if(tokens.size() < 3) { std::cout << "Usage: set <addr> <val>\n"; continue; }
				uint32_t addr = ParseAddress(tokens[1]);
				uint8_t val = (uint8_t)std::stoul(tokens[2], nullptr, 16);
				CmdSet(addr, val);
			} else if(cmd == "disasm" || cmd == "d") {
				uint32_t addr = 0;
				int count = 10;
				bool usePC = true;
				if(tokens.size() > 1) { addr = ParseAddress(tokens[1]); usePC = false; }
				if(tokens.size() > 2) { count = std::stoi(tokens[2]); }
				if(usePC) {
					DebuggerRequest req = _emu->GetDebugger(false);
					Debugger* dbg = req.GetDebugger();
					if(dbg) addr = dbg->GetProgramCounter(_primaryCpu, true);
				}
				CmdDisasm(addr, count);
			} else if(cmd == "bt" || cmd == "backtrace") {
				CmdBacktrace();
			} else if(cmd == "frames") {
				if(tokens.size() < 2) { std::cout << "Usage: frames <N>\n"; continue; }
				CmdFrames(std::stoi(tokens[1]));
			} else if(cmd == "reset") {
				CmdReset();
			} else if(cmd == "trace") {
				if(tokens.size() < 2) { std::cout << "Usage: trace <file|off>\n"; continue; }
				CmdTrace(tokens[1]);
			} else if(cmd == "help" || cmd == "h" || cmd == "?") {
				CmdHelp();
			} else if(cmd == "quit" || cmd == "q" || cmd == "exit") {
				_quit = true;
			} else {
				// Check if it's a memory region short name command
				bool handled = false;
				for(auto& r : regions) {
					if(cmd == r.shortName) {
						uint32_t addr = (tokens.size() > 1) ? ParseAddress(tokens[1]) : 0;
						uint32_t len = (tokens.size() > 2) ? std::stoul(tokens[2]) : 256;
						CmdMemTyped(addr, len, (int)r.type, r.name);
						handled = true;
						break;
					}
				}
				if(!handled) {
					std::cout << "Unknown command: " << cmd << ". Type 'help' for commands.\n";
				}
			}
		} catch(const std::exception& e) {
			std::cerr << "Error: " << e.what() << "\n";
		}
	}
}
