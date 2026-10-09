#include "pch.h"
#include "debugger_cli.h"
#include "cli_notification.h"
#include "cli_input.h"
#include "formatter.h"
#include "console_info.h"
#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"
#include "Shared/DebuggerRequest.h"
#include "Debugger/Debugger.h"
#include "Debugger/Breakpoint.h"
#include "Debugger/MemoryDumper.h"
#include "Debugger/Disassembler.h"
#include "Debugger/ITraceLogger.h"
#include "Debugger/CallstackManager.h"
#include "Debugger/TraceLogFileSaver.h"
#include "Debugger/DebugUtilities.h"
#include "Shared/MemoryType.h"
#include "Shared/Video/VideoDecoder.h"
#include "Utilities/PNGHelper.h"
#include "Utilities/VirtualFile.h"
#include "Shared/SaveStateManager.h"
#include "Shared/Movies/MovieManager.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <poll.h>
#include "SNES/SnesCpuTypes.h"
#include <unistd.h>
#include <cerrno>
#include <thread>
#include <chrono>

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

void DebuggerCli::SyncBreakpoints(int64_t extraAddr)
{
	MemoryType cpuMemType = ConsoleInfo::GetCpuMemoryType(_primaryCpu);

	// Build array of Breakpoint objects from our tracked breakpoints, plus
	// an optional temporary execute breakpoint (CmdUntil's target).
	std::vector<CliBreakpoint> all = _breakpoints;
	if(extraAddr >= 0) {
		CliBreakpoint tmp;
		tmp.id = 0xFFFF;
		tmp.address = (uint32_t)extraAddr;
		tmp.isWatch = false;
		tmp.enabled = true;
		all.push_back(tmp);
	}
	std::vector<Breakpoint> bps;
	for(auto& cbp : all) {
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

// ── JSON replies ───────────────────────────────────────────────────

static std::string JsonEscape(const std::string& s)
{
	std::string out;
	for(char c : s) {
		switch(c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if((unsigned char)c < 0x20) {
					char buf[8];
					snprintf(buf, sizeof(buf), "\\u%04x", c);
					out += buf;
				} else {
					out += c;
				}
		}
	}
	return out;
}

static std::string HexBytes(const uint8_t* data, uint32_t len)
{
	static const char* digits = "0123456789abcdef";
	std::string out;
	out.reserve(len * 2);
	for(uint32_t i = 0; i < len; i++) {
		out += digits[data[i] >> 4];
		out += digits[data[i] & 15];
	}
	return out;
}

void DebuggerCli::Reply(const std::string& json)
{
	fputs(json.c_str(), _out);
	fputc('\n', _out);
	fflush(_out);
	_replied = true;
}

// ReplyOk - {"ok":true[,fields]}; *fields* is a pre-built `"k":v,...` list.
void DebuggerCli::ReplyOk(const std::string& fields)
{
	Reply(fields.empty() ? "{\"ok\":true}" : "{\"ok\":true," + fields + "}");
}

void DebuggerCli::ReplyError(const std::string& message)
{
	Reply("{\"ok\":false,\"error\":\"" + JsonEscape(message) + "\"}");
}

// EnsureSpeed - re-assert maximum speed before running: loading a ROM,
// a movie's power cycle or a state load resets the rewind manager, which
// clears the flag and leaves the frame limiter pacing at 60 fps.
void DebuggerCli::EnsureSpeed()
{
	if(_maxSpeed) _emu->GetSettings()->SetFlag(EmulationFlags::MaximumSpeed);
}

// WaitUntilStopped - after a movie's power cycle, wait for the emulation
// thread to reach its debugger break before asking the debugger for
// anything. Until it does it may still be inside Emulator::WaitForLock,
// spinning while a break request is pending - and any debugger call that
// stops the emulator (SetBreakpoints, via DebugBreakHelper) raises one and
// then spins until execution stops: each waits on the other forever.
// IsExecutionStopped() raises no request, so polling it is safe.
bool DebuggerCli::WaitUntilStopped(int timeoutMs)
{
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	while(std::chrono::steady_clock::now() < deadline) {
		{
			DebuggerRequest req = _emu->GetDebugger(false);
			Debugger* dbg = req.GetDebugger();
			if(dbg && dbg->IsExecutionStopped()) return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return false;
}

uint32_t DebuggerCli::ProgramCounter()
{
	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	return dbg ? dbg->GetProgramCounter(_primaryCpu, true) : 0;
}

std::string DebuggerCli::StateJson()
{
	uint8_t stateBuffer[512] = {};
	BaseState& state = *reinterpret_cast<BaseState*>(stateBuffer);
	{
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(!dbg) return "null";
		dbg->GetCpuState(state, _primaryCpu);
	}
	return Formatter::FormatRegistersJson(_primaryCpu, state);
}

// ReplyState - where execution stopped and why: "step", "break" (a
// breakpoint ended it early) or "until" (CmdUntil reached its target).
void DebuggerCli::ReplyState(const char* stopped, const std::string& fields)
{
	char head[96];
	snprintf(head, sizeof(head), "\"stopped\":\"%s\",\"frame\":%u,\"pc\":%u,",
		stopped, _emu->GetFrameCount(), ProgramCounter());
	std::string f = head;
	if(!fields.empty()) f += fields + ",";
	ReplyOk(f + "\"cpu\":" + StateJson());
}

// Usage - a malformed command: the usage text, or a JSON error.
void DebuggerCli::Usage(const char* text)
{
	if(_jsonOutput) {
		ReplyError(text);
	} else {
		std::cout << text << "\n";
	}
}

// WaitOrPause - wait for the emulator to stop; if it has not within
// *timeoutMs*, pause it before returning, so a timed-out command never
// leaves it running under the next one.
bool DebuggerCli::WaitOrPause(int timeoutMs)
{
	if(_listener->WaitForBreak(timeoutMs)) {
		return true;
	}
	{
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(dbg) dbg->Step(_primaryCpu, 1, StepType::Step);
	}
	_listener->WaitForBreak(1000);
	return false;
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
	EnsureSpeed();
	_listener->Reset();
	{
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(dbg) dbg->Step(_primaryCpu, count, StepType::Step);
	}
	// Scale timeout with step count: ~1ms per 100 steps + 10s base
	int timeoutMs = 10000 + count / 100;
	bool done = WaitOrPause(timeoutMs);
	if(_jsonOutput) {
		if(done) ReplyState("step"); else ReplyError(_listener->IsQuitRequested() ? "quit" : "timeout");
		return;
	}
	PrintState();
}

void DebuggerCli::CmdNext()
{
	EnsureSpeed();
	_listener->Reset();
	{
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(dbg) dbg->Step(_primaryCpu, 1, StepType::StepOver);
	}
	bool done = WaitOrPause(5000);
	if(_jsonOutput) {
		if(done) ReplyState("step"); else ReplyError(_listener->IsQuitRequested() ? "quit" : "timeout");
		return;
	}
	PrintState();
}

void DebuggerCli::CmdFinish()
{
	EnsureSpeed();
	_listener->Reset();
	{
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(dbg) dbg->Step(_primaryCpu, 1, StepType::StepOut);
	}
	bool done = WaitOrPause(30000);
	if(_jsonOutput) {
		if(done) ReplyState("step"); else ReplyError(_listener->IsQuitRequested() ? "quit" : "timeout");
		return;
	}
	PrintState();
}

void DebuggerCli::CmdRun()
{
	EnsureSpeed();
	_listener->Reset();
	{
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(dbg) dbg->Run();
	}
	if(!_jsonOutput) std::cout << "Running... (press Ctrl+C to interrupt)" << std::endl;
	bool hit = _listener->WaitForBreak(0);  // wait indefinitely (or until interrupted)
	if(_listener->IsQuitRequested()) {
		_quit = true;
		if(_jsonOutput) ReplyError("quit");
		return;
	}
	if(!hit) {
		// Interrupted by Ctrl+C — pause the emulator
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(dbg) dbg->Step(_primaryCpu, 1, StepType::Step);
		_listener->WaitForBreak(1000);
		if(!_jsonOutput) std::cout << "\nInterrupted.\n";
	}
	if(_jsonOutput) {
		ReplyState(hit ? "break" : "interrupted");
		return;
	}
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
	if(_jsonOutput) { ReplyOk("\"id\":" + std::to_string(bp.id)); return; }
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
	if(_jsonOutput) { ReplyOk("\"id\":" + std::to_string(bp.id)); return; }
	printf("Watchpoint %d at $%06X (write)\n", bp.id, addr);
}

void DebuggerCli::CmdDelete(uint32_t id)
{
	auto it = std::remove_if(_breakpoints.begin(), _breakpoints.end(),
		[id](const CliBreakpoint& bp) { return bp.id == id; });
	if(it == _breakpoints.end()) {
		if(_jsonOutput) { ReplyError("no breakpoint " + std::to_string(id)); return; }
		printf("No breakpoint %d\n", id);
		return;
	}
	_breakpoints.erase(it, _breakpoints.end());
	SyncBreakpoints();
	if(_jsonOutput) { ReplyOk(); return; }
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
	if(_jsonOutput) { ReplyState("paused"); return; }
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
	if(_jsonOutput) {
		ReplyOk("\"addr\":" + std::to_string(addr) + ",\"hex\":\"" + HexBytes(buf.data(), len) + "\"");
		return;
	}
	std::cout << Formatter::FormatMemoryHex(buf.data(), len, addr);
}

void DebuggerCli::CmdMemTyped(uint32_t addr, uint32_t len, int memType, const char* label)
{
	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(!dbg) return;

	uint32_t size = dbg->GetMemoryDumper()->GetMemorySize((MemoryType)memType);
	if(size == 0) {
		if(_jsonOutput) { ReplyError(std::string(label) + " not available"); return; }
		printf("%s not available.\n", label);
		return;
	}
	if(addr >= size) {
		if(_jsonOutput) { ReplyError("address out of range"); return; }
		printf("Address $%04X out of range (size: $%X / %u bytes)\n", addr, size, size);
		return;
	}
	if(addr + len > size) {
		len = size - addr;
	}

	std::vector<uint8_t> buf(len);
	dbg->GetMemoryDumper()->GetMemoryValues((MemoryType)memType, addr, addr + len - 1, buf.data());
	if(_jsonOutput) {
		ReplyOk("\"addr\":" + std::to_string(addr) + ",\"hex\":\"" + HexBytes(buf.data(), len) + "\"");
		return;
	}
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
		if(_jsonOutput) { ReplyError("unknown memory type: " + type); return; }
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
		if(_jsonOutput) { ReplyError(std::string(label) + " not available"); return; }
		printf("%s not available.\n", label);
		return;
	}

	std::vector<uint8_t> buf(size);
	dbg->GetMemoryDumper()->GetMemoryState(memType, buf.data());

	std::ofstream out(filename, std::ios::binary);
	if(!out) {
		if(_jsonOutput) { ReplyError("cannot write " + filename); return; }
		printf("Failed to open %s for writing.\n", filename.c_str());
		return;
	}
	out.write(reinterpret_cast<char*>(buf.data()), size);
	out.close();
	if(_jsonOutput) {
		ReplyOk("\"path\":\"" + JsonEscape(filename) + "\",\"bytes\":" + std::to_string(size));
		return;
	}
	printf("Dumped %u bytes of %s to %s\n", size, label, filename.c_str());
}

void DebuggerCli::CmdScreenshot(const std::string& filename)
{
	VideoDecoder* decoder = _emu->GetVideoDecoder();
	if(!decoder) {
		if(_jsonOutput) { ReplyError("no video decoder"); return; }
		std::cout << "VideoDecoder not available.\n";
		return;
	}
	if(_jsonOutput && filename.empty()) { ReplyError("screenshot needs a file"); return; }

	if(filename.empty()) {
		// Use default filename
		decoder->TakeScreenshot();
		std::cout << "Screenshot saved.\n";
	} else {
		// Save to specified file via stringstream
		std::stringstream ss;
		decoder->TakeScreenshot(ss);
		std::string data = ss.str();
		if(data.empty()) {
			if(_jsonOutput) { ReplyError("no frame rendered yet"); return; }
			std::cout << "No frame data available (emulation may not have rendered a frame yet).\n";
			return;
		}
		std::ofstream out(filename, std::ios::binary);
		if(!out) {
			if(_jsonOutput) { ReplyError("cannot write " + filename); return; }
			std::cout << "Failed to open " << filename << " for writing.\n";
			return;
		}
		out.write(data.data(), data.size());
		out.close();
		if(_jsonOutput) {
			ReplyOk("\"path\":\"" + JsonEscape(filename) + "\",\"bytes\":" + std::to_string(data.size()));
			return;
		}
		std::cout << "Screenshot saved to " << filename << " (" << data.size() << " bytes)\n";
	}
}

void DebuggerCli::CmdSet(uint32_t addr, uint8_t val)
{
	MemoryType cpuMemType = ConsoleInfo::GetCpuMemoryType(_primaryCpu);
	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(!dbg) return;

	dbg->GetMemoryDumper()->SetMemoryValue(cpuMemType, addr, val);
	if(_jsonOutput) { ReplyOk(); return; }
	printf("[$%06X] = $%02X\n", addr, val);
}

// CmdWrite - several bytes from *addr* in one command.
void DebuggerCli::CmdWrite(uint32_t addr, const std::vector<uint8_t>& bytes)
{
	MemoryType cpuMemType = ConsoleInfo::GetCpuMemoryType(_primaryCpu);
	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(!dbg) return;

	for(size_t i = 0; i < bytes.size(); i++) {
		dbg->GetMemoryDumper()->SetMemoryValue(cpuMemType, addr + (uint32_t)i, bytes[i]);
	}
	if(_jsonOutput) { ReplyOk("\"bytes\":" + std::to_string(bytes.size())); return; }
	printf("Wrote %zu bytes at $%06X\n", bytes.size(), addr);
}

// CmdUntil - run until execution reaches *addr*, or *maxFrames* frames
// pass, whichever is first: the run is bounded in emulated time, so an
// address that is never reached costs a known number of frames, not a
// hang. Other breakpoints still stop it.
void DebuggerCli::CmdUntil(uint32_t addr, int maxFrames)
{
	EnsureSpeed();
	SyncBreakpoints((int64_t)addr);
	_listener->Reset();
	uint32_t start = _emu->GetFrameCount();
	{
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(dbg) dbg->Step(_primaryCpu, maxFrames, StepType::PpuFrame);
	}
	bool done = WaitOrPause(maxFrames * 100 + 10000);
	SyncBreakpoints();
	uint32_t pc = ProgramCounter();
	bool hit = (pc & 0xFFFFFF) == (addr & 0xFFFFFF) || (pc & 0xFFFF) == addr;
	char f[64];
	snprintf(f, sizeof(f), "\"hit\":%s,\"frames\":%u", hit ? "true" : "false",
		_emu->GetFrameCount() - start);
	if(_jsonOutput) {
		if(!done) { ReplyError(_listener->IsQuitRequested() ? "quit" : "timeout"); return; }
		ReplyState(hit ? "until" : (_emu->GetFrameCount() - start < (uint32_t)maxFrames ? "break" : "step"), f);
		return;
	}
	printf("%s $%06X\n", hit ? "Reached" : "Did not reach", addr);
	PrintState();
}

void DebuggerCli::CmdSaveState(const std::string& filename)
{
	bool ok = _emu->GetSaveStateManager()->SaveState(filename, false);
	if(_jsonOutput) {
		if(ok) ReplyOk("\"path\":\"" + JsonEscape(filename) + "\",\"frame\":" + std::to_string(_emu->GetFrameCount()));
		else ReplyError("cannot write " + filename);
		return;
	}
	printf(ok ? "State saved to %s\n" : "Failed to save state to %s\n", filename.c_str());
}

void DebuggerCli::CmdLoadState(const std::string& filename)
{
	bool ok = _emu->GetSaveStateManager()->LoadState(filename, false);
	SyncBreakpoints();
	if(_jsonOutput) {
		if(ok) ReplyState("loadstate"); else ReplyError("cannot load " + filename);
		return;
	}
	if(!ok) { printf("Failed to load state from %s\n", filename.c_str()); return; }
	PrintState();
}

// CmdMovie - start playing an .mmo in this session (power cycle, then its
// embedded SaveState.mss when the ROM's SHA1 matches), or `movie stop`.
void DebuggerCli::CmdMovie(const std::string& filename)
{
	MovieManager* mm = _emu->GetMovieManager();
	if(filename == "stop") {
		mm->Stop();
		if(_jsonOutput) { ReplyOk(); return; }
		printf("Movie stopped.\n");
		return;
	}
	VirtualFile file(filename);
	if(!file.IsValid()) {
		if(_jsonOutput) { ReplyError("movie not found: " + filename); return; }
		printf("Movie not found: %s\n", filename.c_str());
		return;
	}
	mm->Play(file, true);
	// The power cycle re-breaks on the reloaded ROM; touching the debugger
	// before it has (see WaitUntilStopped) can deadlock the two threads.
	if(!WaitUntilStopped(5000)) {
		if(_jsonOutput) { ReplyError("movie: emulator did not stop after the power cycle"); return; }
		printf("Movie started but the emulator did not stop.\n");
		return;
	}
	// The power cycle resets the debugger's breakpoints.
	SyncBreakpoints();
	bool playing = mm->Playing();
	if(_jsonOutput) {
		if(playing) ReplyState("movie"); else ReplyError("movie did not start: " + filename);
		return;
	}
	printf(playing ? "Playing %s\n" : "Failed to play %s\n", filename.c_str());
	PrintState();
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
	EnsureSpeed();
	_listener->Reset();
	uint32_t start = _emu->GetFrameCount();
	{
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(dbg) dbg->Step(_primaryCpu, count, StepType::PpuFrame);
	}
	// Each frame ~16.7ms at 60fps, allow generous overhead for debugger
	bool done = WaitOrPause(count * 100 + 10000);
	if(_jsonOutput) {
		if(!done) { ReplyError(_listener->IsQuitRequested() ? "quit" : "timeout"); return; }
		// A breakpoint ends the run early: fewer frames than asked for.
		uint32_t ran = _emu->GetFrameCount() - start;
		char f[48];
		snprintf(f, sizeof(f), "\"frames\":%u", ran);
		ReplyState(ran < (uint32_t)count ? "break" : "step", f);
		return;
	}
	PrintState();
}

void DebuggerCli::CmdReset()
{
	_emu->Reset();
	// Wait a bit for the reset to take effect
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	if(_jsonOutput) { ReplyState("reset"); return; }
	PrintState();
}

void DebuggerCli::CmdTrace(const std::string& filename, bool withRegs)
{
	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(!dbg) return;

	// The file saver only receives rows when the trace logger for the CPU is
	// itself enabled — enabling the saver alone yields an empty file.
	ITraceLogger* logger = dbg->GetTraceLogger(_primaryCpu);

	if(filename == "off") {
		if(logger) {
			TraceLoggerOptions opt = {};
			opt.Enabled = false;
			logger->SetOptions(opt);
		}
		dbg->GetTraceLogFileSaver()->StopLogging();
		std::cout << "Trace logging stopped.\n";
	} else {
		if(!logger) {
			std::cout << "No trace logger for this CPU.\n";
			return;
		}
		TraceLoggerOptions opt = {};
		opt.Enabled = true;
		opt.IndentCode = false;
		opt.UseLabels = false;
		opt.Condition[0] = '\0';
		opt.Format[0] = '\0';
		if(withRegs) {
			// PC, the instruction, and the registers as they were before it ran.
			snprintf(opt.Format, sizeof(opt.Format), "%s",
				"[PC,6h] [Disassembly] A:[A,4h] X:[X,4h] Y:[Y,4h] S:[SP,4h] D:[D,4h] DB:[DB,2h] P:[P,2h]");
		}
		logger->SetOptions(opt);
		dbg->GetTraceLogFileSaver()->StartLogging(filename);
		std::cout << "Tracing to: " << filename << "\n";
	}
}

uint16_t DebuggerCli::GetRegisterValue(const std::string& name, const uint8_t* stateBuffer)
{
	auto iequals = [](const std::string& a, const std::string& b) {
		if(a.size() != b.size()) return false;
		for(size_t i = 0; i < a.size(); i++) {
			if(toupper(a[i]) != toupper(b[i])) return false;
		}
		return true;
	};

	switch(_primaryCpu) {
		case CpuType::Snes:
		case CpuType::Sa1: {
			const SnesCpuState& s = *reinterpret_cast<const SnesCpuState*>(stateBuffer);
			if(iequals(name, "A")) return s.A;
			if(iequals(name, "X")) return s.X;
			if(iequals(name, "Y")) return s.Y;
			if(iequals(name, "SP") || iequals(name, "S")) return s.SP;
			if(iequals(name, "D")) return s.D;
			if(iequals(name, "DBR")) return s.DBR;
			if(iequals(name, "PS")) return s.PS;
			if(iequals(name, "PC")) return s.PC;
			if(iequals(name, "K")) return s.K;
			break;
		}
		// Other CPU types could be added here (NES, GB, etc.)
		default: break;
	}
	return 0;
}

bool DebuggerCli::ParseRegCondition(const std::string& expr, RegCondition& cond)
{
	// Parse expressions like: SP>0x1FF, A=0x42, D!=0, S<0x100
	// Operators: =, ==, !=, <, >, <=, >=
	size_t opPos = std::string::npos;
	size_t opLen = 0;
	RegCondOp op;

	// Try two-char operators first
	for(size_t i = 1; i < expr.size() - 1; i++) {
		if(expr[i] == '=' && expr[i+1] == '=') { opPos = i; opLen = 2; op = RegCondOp::Eq; break; }
		if(expr[i] == '!' && expr[i+1] == '=') { opPos = i; opLen = 2; op = RegCondOp::Ne; break; }
		if(expr[i] == '<' && expr[i+1] == '=') { opPos = i; opLen = 2; op = RegCondOp::Le; break; }
		if(expr[i] == '>' && expr[i+1] == '=') { opPos = i; opLen = 2; op = RegCondOp::Ge; break; }
	}

	// Try single-char operators
	if(opPos == std::string::npos) {
		for(size_t i = 1; i < expr.size(); i++) {
			if(expr[i] == '=') { opPos = i; opLen = 1; op = RegCondOp::Eq; break; }
			if(expr[i] == '<') { opPos = i; opLen = 1; op = RegCondOp::Lt; break; }
			if(expr[i] == '>') { opPos = i; opLen = 1; op = RegCondOp::Gt; break; }
		}
	}

	if(opPos == std::string::npos) return false;

	cond.regName = expr.substr(0, opPos);
	cond.op = op;

	std::string valStr = expr.substr(opPos + opLen);
	// Strip $ or 0x prefix
	if(valStr.size() > 1 && valStr[0] == '$') valStr = valStr.substr(1);
	else if(valStr.size() > 2 && valStr[0] == '0' && (valStr[1] == 'x' || valStr[1] == 'X')) valStr = valStr.substr(2);

	cond.value = (uint16_t)std::stoul(valStr, nullptr, 16);
	return true;
}

bool DebuggerCli::EvalRegCondition(const RegCondition& cond, const uint8_t* stateBuffer)
{
	uint16_t val = GetRegisterValue(cond.regName, stateBuffer);
	switch(cond.op) {
		case RegCondOp::Eq: return val == cond.value;
		case RegCondOp::Ne: return val != cond.value;
		case RegCondOp::Lt: return val < cond.value;
		case RegCondOp::Gt: return val > cond.value;
		case RegCondOp::Le: return val <= cond.value;
		case RegCondOp::Ge: return val >= cond.value;
	}
	return false;
}

void DebuggerCli::CmdRunUntil(const RegCondition& cond)
{
	EnsureSpeed();
	static const char* opStr[] = { "==", "!=", "<", ">", "<=", ">=" };
	printf("Searching for %s%s$%04X... (Ctrl+C to interrupt)\n",
		cond.regName.c_str(), opStr[(int)cond.op], cond.value);

	uint8_t stateBuffer[512] = {};
	BaseState& state = *reinterpret_cast<BaseState*>(stateBuffer);
	uint64_t totalSteps = 0;

	// Step in small batches, checking after each
	int batchSize = 1;
	while(!_listener->IsInterrupted() && !_listener->IsQuitRequested()) {
		_listener->Reset();
		{
			DebuggerRequest req = _emu->GetDebugger(false);
			Debugger* dbg = req.GetDebugger();
			if(!dbg) return;
			dbg->Step(_primaryCpu, batchSize, StepType::Step);
		}
		int timeoutMs = 10000 + batchSize / 100;
		bool hit = _listener->WaitForBreak(timeoutMs);
		if(!hit || _listener->IsInterrupted()) {
			// Interrupted — pause emulation and report
			DebuggerRequest req = _emu->GetDebugger(false);
			Debugger* dbg = req.GetDebugger();
			if(dbg) dbg->Step(_primaryCpu, 1, StepType::Step);
			_listener->WaitForBreak(1000);
			printf("\nInterrupted after %llu steps.\n", (unsigned long long)totalSteps);
			PrintState();
			return;
		}
		totalSteps += batchSize;

		// Check condition
		{
			DebuggerRequest req = _emu->GetDebugger(false);
			Debugger* dbg = req.GetDebugger();
			if(!dbg) return;
			dbg->GetCpuState(state, _primaryCpu);
		}
		if(EvalRegCondition(cond, stateBuffer)) {
			// Found! But we overshot by up to batchSize steps.
			// Binary search to find the exact instruction.
			if(batchSize > 1) {
				printf("Condition hit after ~%llu steps. Narrowing...\n",
					(unsigned long long)totalSteps);

				// Rewind: we need to re-run from a known-good state.
				// Since we can't rewind, we'll just report what we found.
				// The user can set a breakpoint and step from there.
			}
			printf("Condition met: %s=%04X\n",
				cond.regName.c_str(), GetRegisterValue(cond.regName, stateBuffer));
			PrintState();
			return;
		}

		// Keep batch size at 1 for precise detection

		// Progress report every 10K steps
		if(totalSteps % 10000 < (uint64_t)batchSize) {
			printf("  ...%llu steps\r", (unsigned long long)totalSteps);
			fflush(stdout);
		}
	}
	printf("\nAborted.\n");
}

// CmdInput - hold *buttons* (a comma list, or "none") on controller *port*
// (1 = the first) until the next `input`: live input, polled once a frame.
void DebuggerCli::CmdInput(int port, const std::string& buttons)
{
	if(!_input) { Usage("input: no input provider"); return; }
	if(_consoleType != ConsoleType::Snes) { Usage("input: SNES controllers only"); return; }
	if(port < 1 || port > 8) { Usage("input: port is 1-8"); return; }

	// SnesController's key names, in its text-state order.
	static const char* names[12] = { "a", "b", "x", "y", "l", "r", "select", "start",
	                                 "up", "down", "left", "right" };
	static const char letters[] = "ABXYLRSTUDLR";
	std::string text(12, '.');
	if(buttons != "none") {
		std::stringstream ss(buttons);
		std::string name;
		while(std::getline(ss, name, ',')) {
			int i = 0;
			while(i < 12 && name != names[i]) i++;
			if(i == 12) { Usage(("input: unknown button " + name).c_str()); return; }
			text[i] = letters[i];
		}
	}
	_input->SetPort((uint8_t)(port - 1), text);
	if(_jsonOutput) { ReplyOk(); return; }
	printf("Port %d: %s\n", port, text.c_str());
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
		"  screenshot [file] Capture frame to PNG\n"
		"  ss [file]         Alias for screenshot\n"
		"  set <addr> <val>  Set CPU memory byte\n"
		"  disasm [addr] [n] Disassemble (default: at PC, 10 lines)\n"
		"  d [addr] [n]      Alias for disasm\n"
		"  bt                Show callstack\n"
		"  frames <N>        Run N PPU frames\n"
		"  until <addr> <N>  Run to <addr>, at most N frames\n"
		"  write <addr> <hex> Write bytes (e.g. write 7E0010 0a0b0c)\n"
		"  savestate <file>  Save a state (.mss)\n"
		"  loadstate <file>  Load a state (.mss)\n"
		"  movie <file>|stop Play a movie (.mmo) in this session, or stop it\n"
		"  input <port> <b,..> Hold buttons on a controller (1 = first) until changed;\n"
		"                    a b x y l r select start up down left right, or none\n"
		"  reset             Reset emulator\n"
		"  rwatch <cond>     Run until register condition is true\n"
		"                    Examples: SP>$1FF, D!=0, A=$42, S<$100\n"
		"  trace <file|off> [regs]  Start/stop trace logging (regs: add disassembly and registers)\n"
		"  help              Show this help\n"
		"  quit              Exit debugger\n"
		"\n"
		"Address formats: $1234, 0x1234, 1234, 00:8000\n";
}

// ReadLine - the next line of stdin, read from the descriptor directly.
// Going through std::cin would let its buffer swallow lines that poll()
// then cannot see, stalling a client that keeps the pipe open and sends
// one command at a time. Returns false on EOF or a quit request.
bool DebuggerCli::ReadLine(std::string& line)
{
	while(true) {
		size_t nl = _inBuf.find('\n');
		if(nl != std::string::npos) {
			line = _inBuf.substr(0, nl);
			if(!line.empty() && line.back() == '\r') line.pop_back();
			_inBuf.erase(0, nl + 1);
			return true;
		}
		struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };
		int ret = poll(&pfd, 1, 100);
		if(_listener->IsQuitRequested()) return false;
		if(ret < 0) {
			if(errno == EINTR) continue;
			return false;
		}
		if(ret == 0) continue;
		char buf[4096];
		ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
		if(n < 0) {
			if(errno == EINTR) continue;
			return false;
		}
		if(n == 0) {
			// EOF: a last line without its newline still counts.
			if(_inBuf.empty()) return false;
			line = _inBuf;
			_inBuf.clear();
			return true;
		}
		_inBuf.append(buf, (size_t)n);
	}
}

void DebuggerCli::Run()
{
	SyncBreakpoints();

	if(_jsonOutput) {
		// Keep the real stdout for replies; everything else that writes to
		// fd 1 - Mesen's log, the text-only commands - goes to stderr.
		if(!_out) {
			fflush(stdout);
			int fd = dup(fileno(stdout));
			dup2(fileno(stderr), fileno(stdout));
			_out = fdopen(fd, "w");
		}
		ReplyState("ready");
	} else {
		printf("Mesen CLI Debugger [%s / %s]. Type 'help' for commands.\n",
			ConsoleInfo::GetConsoleName(_consoleType),
			ConsoleInfo::GetCpuName(_primaryCpu));
		PrintState();
	}

	// Build a set of memory region short names for dynamic command dispatch
	auto regions = ConsoleInfo::GetMemoryRegions(_consoleType);

	std::string line;
	while(!_quit) {
		if(_listener->IsQuitRequested()) {
			break;
		}
		if(!_jsonOutput) std::cout << "(mesen) " << std::flush;

		if(!ReadLine(line)) break;

		if(line.empty()) {
			// Repeating the last command is for a person at the prompt; a
			// client that sends an empty line gets nothing run.
			if(_jsonOutput || _lastCommand.empty()) continue;
			line = _lastCommand;
		}

		auto tokens = Tokenize(line);
		if(tokens.empty()) continue;

		_lastCommand = line;
		_replied = false;

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
				if(tokens.size() < 2) { Usage("Usage: break <addr>"); continue; }
				CmdBreak(ParseAddress(tokens[1]));
			} else if(cmd == "watch") {
				if(tokens.size() < 2) { Usage("Usage: watch <addr>"); continue; }
				CmdWatch(ParseAddress(tokens[1]));
			} else if(cmd == "delete" || cmd == "del") {
				if(tokens.size() < 2) { Usage("Usage: delete <id>"); continue; }
				CmdDelete(std::stoul(tokens[1]));
			} else if(cmd == "info") {
				if(tokens.size() > 1 && tokens[1] == "break") {
					CmdInfoBreak();
				} else if(tokens.size() > 1 && tokens[1] == "regions") {
					CmdInfoRegions();
				} else if(tokens.size() > 1 && tokens[1] == "cpu") {
					CmdInfoCpu();
				} else {
					Usage("Usage: info break|regions|cpu");
				}
			} else if(cmd == "regs" || cmd == "r") {
				CmdRegs();
			} else if(cmd == "mem" || cmd == "x") {
				if(tokens.size() < 2) { Usage("Usage: mem <addr> [len]"); continue; }
				uint32_t addr = ParseAddress(tokens[1]);
				uint32_t len = (tokens.size() > 2) ? std::stoul(tokens[2]) : 256;
				CmdMem(addr, len);
			} else if(cmd == "dump") {
				if(tokens.size() < 3) {
					if(_jsonOutput) { Usage("Usage: dump <type> <file>"); continue; }
					std::cout << "Usage: dump <type> <file>\nTypes:";
					for(auto& r : regions) std::cout << " " << r.shortName;
					std::cout << "\n";
					continue;
				}
				CmdDump(tokens[1], tokens[2]);
			} else if(cmd == "screenshot" || cmd == "ss") {
				std::string fname = (tokens.size() > 1) ? tokens[1] : "";
				CmdScreenshot(fname);
			} else if(cmd == "set") {
				if(tokens.size() < 3) { Usage("Usage: set <addr> <val>"); continue; }
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
				if(tokens.size() < 2) { Usage("Usage: frames <N>"); continue; }
				CmdFrames(std::stoi(tokens[1]));
			} else if(cmd == "reset") {
				CmdReset();
			} else if(cmd == "rwatch" || cmd == "rw") {
				if(tokens.size() < 2) { Usage("Usage: rwatch <cond>  (e.g. SP>$1FF, D!=0)"); continue; }
				RegCondition cond;
				if(!ParseRegCondition(tokens[1], cond)) {
					Usage("Invalid condition. Use: REG<op>VALUE (e.g. SP>$1FF, A=$42)");
					continue;
				}
				CmdRunUntil(cond);
			} else if(cmd == "trace") {
				if(tokens.size() < 2) { Usage("Usage: trace <file|off> [regs]"); continue; }
				CmdTrace(tokens[1], tokens.size() > 2 && tokens[2] == "regs");
			} else if(cmd == "write") {
				if(tokens.size() < 3) { Usage("Usage: write <addr> <hexbytes>"); continue; }
				const std::string& hex = tokens[2];
				if(hex.size() % 2 != 0) { Usage("Usage: write <addr> <hexbytes> (an even number of hex digits)"); continue; }
				std::vector<uint8_t> bytes;
				for(size_t i = 0; i < hex.size(); i += 2) {
					bytes.push_back((uint8_t)std::stoul(hex.substr(i, 2), nullptr, 16));
				}
				CmdWrite(ParseAddress(tokens[1]), bytes);
			} else if(cmd == "until") {
				if(tokens.size() < 3) { Usage("Usage: until <addr> <max_frames>"); continue; }
				CmdUntil(ParseAddress(tokens[1]), std::stoi(tokens[2]));
			} else if(cmd == "savestate") {
				if(tokens.size() < 2) { Usage("Usage: savestate <file>"); continue; }
				CmdSaveState(tokens[1]);
			} else if(cmd == "loadstate") {
				if(tokens.size() < 2) { Usage("Usage: loadstate <file>"); continue; }
				CmdLoadState(tokens[1]);
			} else if(cmd == "input") {
				if(tokens.size() < 3) { Usage("Usage: input <port> <button,...|none>"); continue; }
				CmdInput(std::stoi(tokens[1]), tokens[2]);
			} else if(cmd == "movie") {
				if(tokens.size() < 2) { Usage("Usage: movie <file.mmo>|stop"); continue; }
				CmdMovie(tokens[1]);
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
					if(_jsonOutput) ReplyError("unknown command: " + cmd);
					else std::cout << "Unknown command: " << cmd << ". Type 'help' for commands.\n";
				}
			}
		} catch(const std::exception& e) {
			if(_jsonOutput) {
				if(!_replied) ReplyError(std::string("bad argument: ") + e.what());
			} else {
				std::cerr << "Error: " << e.what() << "\n";
			}
		}
		// The text-only commands (disasm, bt, info, trace, help) wrote to
		// stderr; their reply is a bare acknowledgement.
		if(_jsonOutput && !_replied) ReplyOk();
	}
	if(_out) fflush(_out);
}
