#include "DapServer.h"
#include "DapNotificationListener.h"
#include "CpuStateSerializer.h"
#include "Core/Shared/Emulator.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/SettingTypes.h"
#include "Core/Shared/NotificationManager.h"
#include "Core/Shared/DebuggerRequest.h"
#include "Core/Shared/CpuType.h"
#include "Core/Debugger/Debugger.h"
#include "Core/Debugger/Breakpoint.h"
#include "Core/Debugger/DebugTypes.h"
#include "Core/Debugger/DebugUtilities.h"
#include "Core/Debugger/Disassembler.h"
#include "Core/Debugger/CallstackManager.h"
#include "Core/Debugger/MemoryDumper.h"
#include "Core/Debugger/ExpressionEvaluator.h"
#include "Core/SNES/SnesCpuTypes.h"
#include "Utilities/VirtualFile.h"
#include "Utilities/FolderUtilities.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <chrono>
#include <vector>

DapServer::DapServer(Emulator* emu, FILE* dapOutput) : _emu(emu), _writer(dapOutput)
{
	_listener = std::make_shared<DapNotificationListener>(this);
	_emu->GetNotificationManager()->RegisterNotificationListener(_listener);
}

DapServer::~DapServer()
{
	// Detach listener so it doesn't call back into a destroyed server
	_listener->Detach();
}

// ── Main message loop ──────────────────────────────────────────────

void DapServer::Run()
{
	_running = true;
	while(_running) {
		auto msg = _reader.ReadMessage();
		if(!msg) {
			break; // EOF — client disconnected
		}

		const JsonValue& request = *msg;
		std::string command = request["command"].GetString();

		if(command == DapCommand::Initialize) {
			HandleInitialize(request);
		} else if(command == DapCommand::Launch) {
			HandleLaunch(request);
		} else if(command == DapCommand::ConfigurationDone) {
			HandleConfigurationDone(request);
		} else if(command == DapCommand::Threads) {
			HandleThreads(request);
		} else if(command == DapCommand::StackTrace) {
			HandleStackTrace(request);
		} else if(command == DapCommand::Scopes) {
			HandleScopes(request);
		} else if(command == DapCommand::Variables) {
			HandleVariables(request);
		} else if(command == DapCommand::Continue) {
			HandleContinue(request);
		} else if(command == DapCommand::Pause) {
			HandlePause(request);
		} else if(command == DapCommand::Disconnect) {
			HandleDisconnect(request);
		} else if(command == DapCommand::StepIn) {
			HandleStepIn(request);
		} else if(command == DapCommand::Next) {
			HandleNext(request);
		} else if(command == DapCommand::StepOut) {
			HandleStepOut(request);
		} else if(command == DapCommand::SetBreakpoints) {
			HandleSetBreakpoints(request);
		} else if(command == DapCommand::Disassemble) {
			HandleDisassemble(request);
		} else if(command == DapCommand::Evaluate) {
			HandleEvaluate(request);
		} else if(command == DapCommand::ReadMemory) {
			HandleReadMemory(request);
		} else if(command == DapCommand::WriteMemory) {
			HandleWriteMemory(request);
		} else {
			// Unknown command — send error response
			auto resp = MakeResponse(request, false);
			resp.Set("message", JsonValue::MakeString("Unsupported command: " + command));
			SendResponse(std::move(resp));
		}
	}
}

// ── Response / event helpers ───────────────────────────────────────

JsonValue DapServer::MakeResponse(const JsonValue& request, bool success)
{
	auto resp = JsonValue::MakeObject();
	resp.Set("seq", JsonValue::MakeNumber(_seq++));
	resp.Set("type", JsonValue::MakeString("response"));
	resp.Set("request_seq", JsonValue::MakeNumber(request["seq"].GetNumber()));
	resp.Set("command", JsonValue::MakeString(request["command"].GetString()));
	resp.Set("success", JsonValue::MakeBool(success));
	return resp;
}

JsonValue DapServer::MakeEvent(const char* event)
{
	auto evt = JsonValue::MakeObject();
	evt.Set("seq", JsonValue::MakeNumber(_seq++));
	evt.Set("type", JsonValue::MakeString("event"));
	evt.Set("event", JsonValue::MakeString(event));
	return evt;
}

void DapServer::SendResponse(JsonValue&& response)
{
	_writer.SendMessage(std::move(response));
}

void DapServer::SendEvent(JsonValue&& event)
{
	_writer.SendMessage(std::move(event));
}

// ── Thread / CPU mapping ───────────────────────────────────────────

int DapServer::CpuTypeToThreadId(CpuType cpu)
{
	return (int)cpu + 1;
}

CpuType DapServer::ThreadIdToCpuType(int threadId)
{
	return (CpuType)(threadId - 1);
}

const char* DapServer::CpuTypeName(CpuType cpu)
{
	switch(cpu) {
		case CpuType::Snes:    return "SNES 65816";
		case CpuType::Spc:     return "SNES SPC700";
		case CpuType::NecDsp:  return "SNES NEC DSP";
		case CpuType::Sa1:     return "SNES SA-1";
		case CpuType::Gsu:     return "SNES GSU";
		case CpuType::Cx4:     return "SNES CX4";
		case CpuType::Gameboy: return "Game Boy LR35902";
		case CpuType::Nes:     return "NES 6502";
		case CpuType::Pce:     return "PCE HuC6280";
		case CpuType::Sms:     return "SMS Z80";
		case CpuType::Gba:     return "GBA ARM7";
		case CpuType::Ws:      return "WonderSwan V30MZ";
		default:               return "Unknown CPU";
	}
}

// ── Stopped / terminated events (called from notification listener) ──

void DapServer::SendStoppedEvent(const char* reason, CpuType cpu)
{
	auto evt = MakeEvent(DapEvent::Stopped);
	auto body = JsonValue::MakeObject();
	body.Set("reason", JsonValue::MakeString(reason));
	body.Set("threadId", JsonValue::MakeNumber(CpuTypeToThreadId(cpu)));
	body.Set("allThreadsStopped", JsonValue::MakeBool(true));
	evt.Set("body", std::move(body));
	SendEvent(std::move(evt));
}

void DapServer::SendTerminatedEvent()
{
	auto evt = MakeEvent(DapEvent::Terminated);
	SendEvent(std::move(evt));
}

// ── Request handlers ───────────────────────────────────────────────

void DapServer::HandleInitialize(const JsonValue& request)
{
	auto resp = MakeResponse(request, true);
	auto body = JsonValue::MakeObject();
	body.Set("supportsConfigurationDoneRequest", JsonValue::MakeBool(true));
	body.Set("supportsSingleThreadExecutionRequests", JsonValue::MakeBool(true));
	body.Set("supportsDisassembleRequest", JsonValue::MakeBool(true));
	body.Set("supportsSteppingGranularity", JsonValue::MakeBool(true));
	body.Set("supportsInstructionBreakpoints", JsonValue::MakeBool(true));
	body.Set("supportsEvaluateForHovers", JsonValue::MakeBool(true));
	body.Set("supportsReadMemoryRequest", JsonValue::MakeBool(true));
	body.Set("supportsWriteMemoryRequest", JsonValue::MakeBool(true));
	body.Set("supportsConditionalBreakpoints", JsonValue::MakeBool(true));
	resp.Set("body", std::move(body));
	SendResponse(std::move(resp));

	// Send initialized event — tells client to send breakpoint configs
	auto evt = MakeEvent(DapEvent::Initialized);
	SendEvent(std::move(evt));
}

void DapServer::HandleLaunch(const JsonValue& request)
{
	const JsonValue& args = request["arguments"];
	std::string romPath = args["romPath"].GetString();

	// stopOnEntry defaults to true if not specified
	if(args["stopOnEntry"].GetType() != JsonValue::Type::Null) {
		_stopOnEntry = args["stopOnEntry"].GetBool();
	} else {
		_stopOnEntry = true;
	}

	if(romPath.empty()) {
		auto resp = MakeResponse(request, false);
		resp.Set("message", JsonValue::MakeString("romPath is required"));
		SendResponse(std::move(resp));
		return;
	}

	// Load ROM — emulator was already Pause()d in DapMain, so the emulation
	// thread will hit the initial break and block in SleepUntilResume.
	if(!_emu->LoadRom((VirtualFile)romPath, VirtualFile())) {
		auto resp = MakeResponse(request, false);
		resp.Set("message", JsonValue::MakeString("Failed to load ROM: " + romPath));
		SendResponse(std::move(resp));
		return;
	}

	// Load .dbg symbol file: explicit path or auto-detect from ROM path
	std::string dbgPath = args["dbgPath"].GetString();
	if(dbgPath.empty()) {
		// Auto-detect: try replacing ROM extension with .dbg
		size_t dot = romPath.find_last_of('.');
		if(dot != std::string::npos) {
			dbgPath = romPath.substr(0, dot) + ".dbg";
		}
		// Also try common patterns: same directory, "main.dbg"
		if(!dbgPath.empty()) {
			std::ifstream test(dbgPath);
			if(!test.good()) {
				// Try sibling "main.dbg" in same directory
				size_t slash = romPath.find_last_of("/\\");
				if(slash != std::string::npos) {
					std::string dir = romPath.substr(0, slash + 1);
					dbgPath = dir + "main.dbg";
				}
			}
		}
	}
	if(!dbgPath.empty()) {
		if(_sourceMapper.LoadDbgFile(dbgPath)) {
			fprintf(stderr, "[DAP] Loaded symbols from: %s\n", dbgPath.c_str());
		}
	}

	auto resp = MakeResponse(request, true);
	SendResponse(std::move(resp));
}

void DapServer::HandleConfigurationDone(const JsonValue& request)
{
	_configDone = true;

	auto resp = MakeResponse(request, true);
	SendResponse(std::move(resp));

	if(_stopOnEntry) {
		// Emulation is already paused from the initial break after LoadRom.
		// Send stopped event to tell VSCode we're at the entry point.
		auto cpuTypes = _emu->GetCpuTypes();
		if(!cpuTypes.empty()) {
			SendStoppedEvent(DapStoppedReason::Entry, cpuTypes[0]);
		}
	} else {
		// Resume execution immediately
		DebuggerRequest req = _emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(dbg) {
			dbg->Run();
		}
	}
}

void DapServer::HandleThreads(const JsonValue& request)
{
	auto resp = MakeResponse(request, true);
	auto threads = JsonValue::MakeArray();

	auto cpuTypes = _emu->GetCpuTypes();
	for(CpuType cpu : cpuTypes) {
		auto thread = JsonValue::MakeObject();
		thread.Set("id", JsonValue::MakeNumber(CpuTypeToThreadId(cpu)));
		thread.Set("name", JsonValue::MakeString(CpuTypeName(cpu)));
		threads.Push(std::move(thread));
	}

	auto body = JsonValue::MakeObject();
	body.Set("threads", std::move(threads));
	resp.Set("body", std::move(body));
	SendResponse(std::move(resp));
}

void DapServer::HandleStackTrace(const JsonValue& request)
{
	const JsonValue& args = request["arguments"];
	int threadId = (int)args["threadId"].GetNumber();
	CpuType cpu = ThreadIdToCpuType(threadId);

	auto resp = MakeResponse(request, true);
	auto frames = JsonValue::MakeArray();
	int frameCount = 0;

	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(dbg) {
		uint32_t pc = dbg->GetProgramCounter(cpu, true);

		// Helper to build a single DAP StackFrame
		auto makeFrame = [&](int frameId, uint32_t addr, const char* annotation) {
			auto frame = JsonValue::MakeObject();
			// Encode threadId into upper bits of frameId for scopes
			frame.Set("id", JsonValue::MakeNumber((threadId << 16) | frameId));

			std::string symName = _sourceMapper.GetSymbolName(addr);
			char addrStr[16];
			snprintf(addrStr, sizeof(addrStr), "$%04X", addr & 0xFFFF);
			std::string name;
			if(!symName.empty()) {
				name = symName;
			} else {
				name = addrStr;
			}
			if(annotation && annotation[0]) {
				name += std::string(" [") + annotation + "]";
			}
			frame.Set("name", JsonValue::MakeString(name));

			SourceLocation loc = _sourceMapper.GetSourceLocation(addr);
			if(loc.valid && !loc.file.empty()) {
				frame.Set("line", JsonValue::MakeNumber(loc.line));
				frame.Set("column", JsonValue::MakeNumber(1));
				auto source = JsonValue::MakeObject();
				source.Set("name", JsonValue::MakeString(loc.file));
				source.Set("path", JsonValue::MakeString(loc.file));
				frame.Set("source", std::move(source));
			} else {
				frame.Set("line", JsonValue::MakeNumber(0));
				frame.Set("column", JsonValue::MakeNumber(0));
				auto source = JsonValue::MakeObject();
				source.Set("name", JsonValue::MakeString(CpuTypeName(cpu)));
				source.Set("presentationHint", JsonValue::MakeString("deemphasize"));
				frame.Set("source", std::move(source));
			}

			char ipr[32];
			snprintf(ipr, sizeof(ipr), "0x%X", addr);
			frame.Set("instructionPointerReference", JsonValue::MakeString(ipr));
			return frame;
		};

		// Frame 0: current PC
		frames.Push(makeFrame(0, pc, nullptr));
		frameCount = 1;

		// Additional frames from CallstackManager
		CallstackManager* csm = dbg->GetCallstackManager(cpu);
		if(csm) {
			StackFrameInfo csFrames[64];
			uint32_t csCount = 0;
			csm->GetCallstack(csFrames, csCount);

			// CallstackManager returns frames from most recent to oldest
			for(uint32_t i = 0; i < csCount && frameCount < 64; i++) {
				const char* annotation = nullptr;
				if(csFrames[i].Flags == StackFrameFlags::Nmi) annotation = "NMI";
				else if(csFrames[i].Flags == StackFrameFlags::Irq) annotation = "IRQ";
				frames.Push(makeFrame(frameCount, csFrames[i].Source, annotation));
				frameCount++;
			}
		}
	}

	auto body = JsonValue::MakeObject();
	body.Set("stackFrames", std::move(frames));
	body.Set("totalFrames", JsonValue::MakeNumber(frameCount));
	resp.Set("body", std::move(body));
	SendResponse(std::move(resp));
}

void DapServer::HandleScopes(const JsonValue& request)
{
	const JsonValue& args = request["arguments"];
	int frameId = (int)args["frameId"].GetNumber();
	// frameId encodes threadId in upper 16 bits (set in HandleStackTrace)
	int threadId = (frameId >> 16) & 0xFFFF;
	if(threadId == 0) threadId = frameId; // backward compat: old single-frame format

	auto resp = MakeResponse(request, true);
	auto scopes = JsonValue::MakeArray();

	auto regScope = JsonValue::MakeObject();
	regScope.Set("name", JsonValue::MakeString("Registers"));
	regScope.Set("variablesReference", JsonValue::MakeNumber((threadId << 16) | (int)ScopeType::Registers));
	regScope.Set("expensive", JsonValue::MakeBool(false));
	scopes.Push(std::move(regScope));

	auto flagScope = JsonValue::MakeObject();
	flagScope.Set("name", JsonValue::MakeString("Flags"));
	flagScope.Set("variablesReference", JsonValue::MakeNumber((threadId << 16) | (int)ScopeType::Flags));
	flagScope.Set("expensive", JsonValue::MakeBool(false));
	scopes.Push(std::move(flagScope));

	auto body = JsonValue::MakeObject();
	body.Set("scopes", std::move(scopes));
	resp.Set("body", std::move(body));
	SendResponse(std::move(resp));
}

void DapServer::HandleVariables(const JsonValue& request)
{
	const JsonValue& args = request["arguments"];
	int varRef = (int)args["variablesReference"].GetNumber();

	int threadId = (varRef >> 16) & 0xFFFF;
	ScopeType scopeType = (ScopeType)(varRef & 0xFFFF);
	CpuType cpu = ThreadIdToCpuType(threadId);

	auto resp = MakeResponse(request, true);
	auto variables = JsonValue::MakeArray();

	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(dbg) {
		// Raw buffer large enough for any CPU state struct
		uint8_t stateBuffer[512];
		memset(stateBuffer, 0, sizeof(stateBuffer));
		dbg->GetCpuState(*(BaseState*)stateBuffer, cpu);

		if(scopeType == ScopeType::Registers) {
			variables = CpuStateSerializer::SerializeRegisters(cpu, *(BaseState*)stateBuffer);
		} else if(scopeType == ScopeType::Flags) {
			variables = CpuStateSerializer::SerializeFlags(cpu, *(BaseState*)stateBuffer);
		}
	}

	auto body = JsonValue::MakeObject();
	body.Set("variables", std::move(variables));
	resp.Set("body", std::move(body));
	SendResponse(std::move(resp));
}

void DapServer::HandleContinue(const JsonValue& request)
{
	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(dbg) {
		dbg->Run();
	}

	auto resp = MakeResponse(request, true);
	auto body = JsonValue::MakeObject();
	body.Set("allThreadsContinued", JsonValue::MakeBool(true));
	resp.Set("body", std::move(body));
	SendResponse(std::move(resp));
}

void DapServer::HandlePause(const JsonValue& request)
{
	// Use Emulator::Pause() which coordinates properly with the emulation
	// thread via DebugBreakHelper + Step(1, Step, Pause). This is non-blocking
	// from our perspective — it briefly waits for the current instruction to
	// finish, sets up a break-after-1-instruction, then resumes. The emulation
	// thread will break and send a CodeBreak notification → stopped event.
	_emu->Pause();

	auto resp = MakeResponse(request, true);
	SendResponse(std::move(resp));
}

void DapServer::HandleDisconnect(const JsonValue& request)
{
	auto resp = MakeResponse(request, true);
	SendResponse(std::move(resp));

	_emu->Stop(true);
	_running = false;
}

// ── Phase 2: Stepping ──────────────────────────────────────────────

void DapServer::HandleStepIn(const JsonValue& request)
{
	const JsonValue& args = request["arguments"];
	int threadId = (int)args["threadId"].GetNumber();
	CpuType cpu = ThreadIdToCpuType(threadId);

	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(dbg) {
		dbg->Step(cpu, 1, StepType::Step, BreakSource::CpuStep);
	}

	auto resp = MakeResponse(request, true);
	SendResponse(std::move(resp));
}

void DapServer::HandleNext(const JsonValue& request)
{
	const JsonValue& args = request["arguments"];
	int threadId = (int)args["threadId"].GetNumber();
	CpuType cpu = ThreadIdToCpuType(threadId);

	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(dbg) {
		dbg->Step(cpu, 1, StepType::StepOver, BreakSource::CpuStep);
	}

	auto resp = MakeResponse(request, true);
	SendResponse(std::move(resp));
}

void DapServer::HandleStepOut(const JsonValue& request)
{
	const JsonValue& args = request["arguments"];
	int threadId = (int)args["threadId"].GetNumber();
	CpuType cpu = ThreadIdToCpuType(threadId);

	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(dbg) {
		dbg->Step(cpu, 1, StepType::StepOut, BreakSource::CpuStep);
	}

	auto resp = MakeResponse(request, true);
	SendResponse(std::move(resp));
}

// ── Phase 2: Breakpoints ──────────────────────────────────────────

// BreakpointData overlays Breakpoint's private fields (same pattern as debugger_cli.cpp)
struct BreakpointData {
	uint32_t id;
	CpuType cpuType;
	uint8_t _pad1[3];
	MemoryType memoryType;
	BreakpointTypeFlags type;
	int32_t startAddr;
	int32_t endAddr;
	bool enabled;
	bool markEvent;
	bool ignoreDummyOperations;
	char _pad2;
	char condition[1000];
};

void DapServer::SyncBreakpoints()
{
	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(dbg) {
		dbg->SetBreakpoints(_breakpoints.data(), (uint32_t)_breakpoints.size());
	}
}

void DapServer::HandleSetBreakpoints(const JsonValue& request)
{
	const JsonValue& args = request["arguments"];
	const JsonValue& source = args["source"];

	// Get the primary CPU and its memory type
	auto cpuTypes = _emu->GetCpuTypes();
	CpuType primaryCpu = cpuTypes.empty() ? CpuType::Snes : cpuTypes[0];
	MemoryType cpuMemType = DebugUtilities::GetCpuMemoryType(primaryCpu);

	// Clear existing breakpoints (DAP sends the full set each time)
	_breakpoints.clear();
	auto verifiedBps = JsonValue::MakeArray();

	std::string sourcePath = source["path"].GetString();
	std::string sourceName = source["name"].GetString();
	bool isSourceFile = !sourcePath.empty() && sourcePath[0] != '0' && sourcePath[0] != '$';

	const auto& breakpointsList = args["breakpoints"].GetArray();
	for(size_t i = 0; i < breakpointsList.size(); i++) {
		const JsonValue& bp = breakpointsList[i];
		int32_t addr = -1;

		int requestedLine = 0;
		if(bp["line"].GetType() == JsonValue::Type::Number) {
			requestedLine = (int)bp["line"].GetNumber();
		}

		if(isSourceFile && _sourceMapper.IsLoaded()) {
			// Source breakpoint: resolve file+line → address
			uint32_t resolved = _sourceMapper.GetAddress(sourcePath, requestedLine);
			if(resolved != 0xFFFFFFFF) {
				addr = (int32_t)resolved;
			}
		} else if(requestedLine > 0) {
			// Disassembly view: line IS the address
			addr = (int32_t)requestedLine;
		}

		// Also try parsing source.path as hex address
		if(addr < 0 && !sourcePath.empty()) {
			if(sourcePath.size() > 2 && sourcePath[0] == '0' && (sourcePath[1] == 'x' || sourcePath[1] == 'X')) {
				try { addr = (int32_t)std::stoul(sourcePath.substr(2), nullptr, 16); } catch(...) {}
			} else if(sourcePath[0] == '$') {
				try { addr = (int32_t)std::stoul(sourcePath.substr(1), nullptr, 16); } catch(...) {}
			}
		}

		uint32_t bpId = _nextBreakpointId++;
		bool verified = (addr >= 0);

		if(verified) {
			Breakpoint mesenBp;
			static_assert(sizeof(BreakpointData) == sizeof(Breakpoint),
				"BreakpointData layout must match Breakpoint");
			BreakpointData* data = reinterpret_cast<BreakpointData*>(&mesenBp);
			memset(data, 0, sizeof(BreakpointData));
			data->id = bpId;
			data->cpuType = primaryCpu;
			data->memoryType = cpuMemType;
			data->type = BreakpointTypeFlags::Execute;
			data->startAddr = addr;
			data->endAddr = addr;
			data->enabled = true;

			// Condition support
			std::string condition = bp["condition"].GetString();
			if(!condition.empty() && condition.size() < sizeof(data->condition)) {
				strncpy(data->condition, condition.c_str(), sizeof(data->condition) - 1);
			}

			_breakpoints.push_back(mesenBp);
		}

		auto bpResult = JsonValue::MakeObject();
		bpResult.Set("id", JsonValue::MakeNumber(bpId));
		bpResult.Set("verified", JsonValue::MakeBool(verified));
		if(verified) {
			bpResult.Set("line", JsonValue::MakeNumber(addr));
		}
		verifiedBps.Push(std::move(bpResult));
	}

	SyncBreakpoints();

	auto resp = MakeResponse(request, true);
	auto body = JsonValue::MakeObject();
	body.Set("breakpoints", std::move(verifiedBps));
	resp.Set("body", std::move(body));
	SendResponse(std::move(resp));
}

// ── Phase 2: Disassembly ──────────────────────────────────────────

void DapServer::HandleDisassemble(const JsonValue& request)
{
	const JsonValue& args = request["arguments"];
	std::string memRef = args["memoryReference"].GetString();
	int offset = 0;
	int count = 20;

	if(args["offset"].GetType() == JsonValue::Type::Number) {
		offset = (int)args["offset"].GetNumber();
	}
	if(args["instructionOffset"].GetType() == JsonValue::Type::Number) {
		offset += (int)args["instructionOffset"].GetNumber();
	}
	if(args["instructionCount"].GetType() == JsonValue::Type::Number) {
		count = (int)args["instructionCount"].GetNumber();
	}

	// Parse base address from memoryReference (format: "0xABCD")
	uint32_t baseAddr = 0;
	if(memRef.size() > 2 && memRef[0] == '0' && (memRef[1] == 'x' || memRef[1] == 'X')) {
		try { baseAddr = (uint32_t)std::stoul(memRef.substr(2), nullptr, 16); } catch(...) {}
	}
	uint32_t addr = (uint32_t)((int32_t)baseAddr + offset);

	auto cpuTypes = _emu->GetCpuTypes();
	CpuType primaryCpu = cpuTypes.empty() ? CpuType::Snes : cpuTypes[0];

	auto resp = MakeResponse(request, true);
	auto instructions = JsonValue::MakeArray();

	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(dbg && count > 0) {
		std::vector<CodeLineData> output(count);
		uint32_t actual = dbg->GetDisassembler()->GetDisassemblyOutput(
			primaryCpu, addr, output.data(), count);

		for(uint32_t i = 0; i < actual; i++) {
			CodeLineData& line = output[i];

			// Skip empty/data lines
			if(line.Flags & LineFlags::Empty) continue;

			auto instr = JsonValue::MakeObject();

			// Format address as "0xABCD"
			char addrStr[16];
			snprintf(addrStr, sizeof(addrStr), "0x%X", (uint32_t)line.Address);
			instr.Set("address", JsonValue::MakeString(addrStr));

			// Instruction text (trim trailing whitespace)
			std::string text = line.Text;
			size_t end = text.find_last_not_of(" \t\r\n");
			if(end != std::string::npos) text.resize(end + 1);
			instr.Set("instruction", JsonValue::MakeString(text));

			// Byte code
			std::string bytes;
			for(int b = 0; b < line.OpSize; b++) {
				char hex[4];
				snprintf(hex, sizeof(hex), "%02X ", line.ByteCode[b]);
				bytes += hex;
			}
			if(!bytes.empty()) bytes.pop_back(); // trim trailing space
			instr.Set("instructionBytes", JsonValue::MakeString(bytes));

			instructions.Push(std::move(instr));
		}
	}

	auto body = JsonValue::MakeObject();
	body.Set("instructions", std::move(instructions));
	resp.Set("body", std::move(body));
	SendResponse(std::move(resp));
}

// ── Phase 4: Expression evaluation ────────────────────────────────

void DapServer::HandleEvaluate(const JsonValue& request)
{
	const JsonValue& args = request["arguments"];
	std::string expression = args["expression"].GetString();

	auto cpuTypes = _emu->GetCpuTypes();
	CpuType primaryCpu = cpuTypes.empty() ? CpuType::Snes : cpuTypes[0];

	// If a frameId is provided, extract the threadId for CPU selection
	if(args["frameId"].GetType() == JsonValue::Type::Number) {
		int frameId = (int)args["frameId"].GetNumber();
		int threadId = (frameId >> 16) & 0xFFFF;
		if(threadId > 0) {
			primaryCpu = ThreadIdToCpuType(threadId);
		}
	}

	auto resp = MakeResponse(request, true);

	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(dbg && !expression.empty()) {
		EvalResultType resultType = EvalResultType::Invalid;
		int64_t result = dbg->EvaluateExpression(expression, primaryCpu, resultType, false);

		auto body = JsonValue::MakeObject();
		if(resultType == EvalResultType::Numeric) {
			char buf[32];
			if(result >= 0 && result <= 0xFFFF) {
				snprintf(buf, sizeof(buf), "$%04X (%d)", (int)result, (int)result);
			} else {
				snprintf(buf, sizeof(buf), "$%llX (%lld)", (long long)result, (long long)result);
			}
			body.Set("result", JsonValue::MakeString(buf));
			body.Set("type", JsonValue::MakeString("integer"));
		} else if(resultType == EvalResultType::Boolean) {
			body.Set("result", JsonValue::MakeString(result ? "true" : "false"));
			body.Set("type", JsonValue::MakeString("boolean"));
		} else {
			body.Set("result", JsonValue::MakeString("<error>"));
			body.Set("type", JsonValue::MakeString("error"));
		}
		body.Set("variablesReference", JsonValue::MakeNumber(0));
		resp.Set("body", std::move(body));
	} else {
		resp.Set("success", JsonValue::MakeBool(false));
		resp.Set("message", JsonValue::MakeString("No debugger active"));
	}

	SendResponse(std::move(resp));
}

// ── Phase 4: Memory read/write ────────────────────────────────────

// Base64 encoding table
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string Base64Encode(const uint8_t* data, size_t len)
{
	std::string out;
	out.reserve((len + 2) / 3 * 4);
	for(size_t i = 0; i < len; i += 3) {
		uint32_t n = (uint32_t)data[i] << 16;
		if(i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
		if(i + 2 < len) n |= data[i + 2];
		out += b64[(n >> 18) & 0x3F];
		out += b64[(n >> 12) & 0x3F];
		out += (i + 1 < len) ? b64[(n >> 6) & 0x3F] : '=';
		out += (i + 2 < len) ? b64[n & 0x3F] : '=';
	}
	return out;
}

static std::vector<uint8_t> Base64Decode(const std::string& encoded)
{
	static int8_t inv[256];
	static bool init = false;
	if(!init) {
		memset(inv, -1, sizeof(inv));
		for(int i = 0; i < 64; i++) inv[(uint8_t)b64[i]] = i;
		init = true;
	}

	std::vector<uint8_t> out;
	out.reserve(encoded.size() * 3 / 4);
	uint32_t buf = 0;
	int bits = 0;
	for(char c : encoded) {
		if(c == '=' || c == '\n' || c == '\r') continue;
		int8_t val = inv[(uint8_t)c];
		if(val < 0) continue;
		buf = (buf << 6) | val;
		bits += 6;
		if(bits >= 8) {
			bits -= 8;
			out.push_back((buf >> bits) & 0xFF);
		}
	}
	return out;
}

void DapServer::HandleReadMemory(const JsonValue& request)
{
	const JsonValue& args = request["arguments"];
	std::string memRef = args["memoryReference"].GetString();
	int count = (int)args["count"].GetNumber();
	int offset = 0;
	if(args["offset"].GetType() == JsonValue::Type::Number) {
		offset = (int)args["offset"].GetNumber();
	}

	// Parse base address
	uint32_t baseAddr = 0;
	if(memRef.size() > 2 && memRef[0] == '0' && (memRef[1] == 'x' || memRef[1] == 'X')) {
		try { baseAddr = (uint32_t)std::stoul(memRef.substr(2), nullptr, 16); } catch(...) {}
	}
	uint32_t addr = (uint32_t)((int32_t)baseAddr + offset);

	auto cpuTypes = _emu->GetCpuTypes();
	CpuType primaryCpu = cpuTypes.empty() ? CpuType::Snes : cpuTypes[0];
	MemoryType cpuMemType = DebugUtilities::GetCpuMemoryType(primaryCpu);

	auto resp = MakeResponse(request, true);

	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(dbg && count > 0) {
		std::vector<uint8_t> data(count);
		dbg->GetMemoryDumper()->GetMemoryValues(cpuMemType, addr, addr + count - 1, data.data());

		char addrStr[16];
		snprintf(addrStr, sizeof(addrStr), "0x%X", addr);

		auto body = JsonValue::MakeObject();
		body.Set("address", JsonValue::MakeString(addrStr));
		body.Set("data", JsonValue::MakeString(Base64Encode(data.data(), data.size())));
		body.Set("unreadableBytes", JsonValue::MakeNumber(0));
		resp.Set("body", std::move(body));
	} else {
		auto body = JsonValue::MakeObject();
		char addrStr[16];
		snprintf(addrStr, sizeof(addrStr), "0x%X", addr);
		body.Set("address", JsonValue::MakeString(addrStr));
		body.Set("data", JsonValue::MakeString(""));
		body.Set("unreadableBytes", JsonValue::MakeNumber(count));
		resp.Set("body", std::move(body));
	}

	SendResponse(std::move(resp));
}

void DapServer::HandleWriteMemory(const JsonValue& request)
{
	const JsonValue& args = request["arguments"];
	std::string memRef = args["memoryReference"].GetString();
	std::string dataB64 = args["data"].GetString();
	int offset = 0;
	if(args["offset"].GetType() == JsonValue::Type::Number) {
		offset = (int)args["offset"].GetNumber();
	}

	uint32_t baseAddr = 0;
	if(memRef.size() > 2 && memRef[0] == '0' && (memRef[1] == 'x' || memRef[1] == 'X')) {
		try { baseAddr = (uint32_t)std::stoul(memRef.substr(2), nullptr, 16); } catch(...) {}
	}
	uint32_t addr = (uint32_t)((int32_t)baseAddr + offset);

	auto cpuTypes = _emu->GetCpuTypes();
	CpuType primaryCpu = cpuTypes.empty() ? CpuType::Snes : cpuTypes[0];
	MemoryType cpuMemType = DebugUtilities::GetCpuMemoryType(primaryCpu);

	auto resp = MakeResponse(request, true);

	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(dbg && !dataB64.empty()) {
		std::vector<uint8_t> data = Base64Decode(dataB64);
		for(size_t i = 0; i < data.size(); i++) {
			dbg->GetMemoryDumper()->SetMemoryValue(cpuMemType, addr + (uint32_t)i, data[i]);
		}
		auto body = JsonValue::MakeObject();
		body.Set("bytesWritten", JsonValue::MakeNumber(data.size()));
		resp.Set("body", std::move(body));
	}

	SendResponse(std::move(resp));
}
