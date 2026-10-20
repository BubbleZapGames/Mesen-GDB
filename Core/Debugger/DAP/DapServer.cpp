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
	Close();
}

void DapServer::Close()
{
	// Detach listener so it doesn't call back into a closed or destroyed server
	_listener->Detach();
	_writer.Close();
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
		} else if(command == DapCommand::SetInstructionBreakpoints) {
			HandleSetInstructionBreakpoints(request);
		} else if(command == DapCommand::SetFunctionBreakpoints) {
			HandleSetFunctionBreakpoints(request);
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
	body.Set("supportsFunctionBreakpoints", JsonValue::MakeBool(true));
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

// CallstackManager keeps at most this many calls, and copies them all out
static const uint32_t MAX_CALLSTACK = 512;

// The bytes an iNES file holds before PRG-ROM (the header, and a trainer if
// it has one), or 0 for any other ROM
static uint32_t InesHeaderSize(const std::string& romPath)
{
	std::ifstream rom(romPath, std::ios::binary);
	uint8_t header[16] = {};
	if(!rom.read((char*)header, sizeof(header)) || memcmp(header, "NES\x1A", 4) != 0) {
		return 0;
	}
	return (header[6] & 0x04) ? 16 + 512 : 16;
}

// A WLA-DX symbol file rather than a .dbg
static bool IsSymFile(const std::string& path)
{
	if(path.size() < 4) return false;
	std::string ext = path.substr(path.size() - 4);
	for(char& c : ext) c = (char)tolower((unsigned char)c);
	return ext == ".sym";
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

	// A .dbg counts an iNES header in its ROM offsets; Mesen's don't
	uint32_t inesHeader = InesHeaderSize(romPath);
	if(inesHeader > 0) {
		_sourceMapper.SetNesRom(inesHeader);
	}

	// Symbols: the dbgPath given (a .dbg or a WLA-DX .sym), else the ROM's
	// name with .dbg, then with .sym, then main.dbg beside it
	std::string dbgPath = args["dbgPath"].GetString();
	if(dbgPath.empty()) {
		size_t dot = romPath.find_last_of('.');
		size_t slash = romPath.find_last_of("/\\");
		std::vector<std::string> candidates;
		if(dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
			candidates.push_back(romPath.substr(0, dot) + ".dbg");
			candidates.push_back(romPath.substr(0, dot) + ".sym");
		}
		candidates.push_back(romPath.substr(0, slash == std::string::npos ? 0 : slash + 1) + "main.dbg");
		for(const std::string& candidate : candidates) {
			if(std::ifstream(candidate).good()) {
				dbgPath = candidate;
				break;
			}
		}
	}
	if(!dbgPath.empty()) {
		bool loaded;
		if(IsSymFile(dbgPath)) {
			// A .sym records labels as CPU addresses; the debugger knows
			// where in the ROM each one is
			DebuggerRequest req = _emu->GetDebugger(false);
			Debugger* dbg = req.GetDebugger();
			auto cpuTypes = _emu->GetCpuTypes();
			loaded = _sourceMapper.LoadSymFile(dbgPath, [&](uint32_t cpuAddr) -> uint32_t {
				if(!dbg || cpuTypes.empty()) return SourceMapper::NoRomOffset;
				AddressInfo relAddr { (int32_t)cpuAddr, DebugUtilities::GetCpuMemoryType(cpuTypes[0]) };
				AddressInfo absAddr = dbg->GetAbsoluteAddress(relAddr);
				bool inRom = absAddr.Address >= 0 && DebugUtilities::IsRom(absAddr.Type);
				return inRom ? (uint32_t)absAddr.Address : SourceMapper::NoRomOffset;
			});
		} else {
			loaded = _sourceMapper.LoadDbgFile(dbgPath);
		}
		if(loaded) {
			fprintf(stderr, "[DAP] Loaded symbols from: %s\n", dbgPath.c_str());
		} else {
			fprintf(stderr, "[DAP] Could not read symbols from: %s\n", dbgPath.c_str());
		}
	}

	// LoadRom creates a fresh debugger and symbols only exist now, so anything
	// set before launch (VSCode sends setBreakpoints as soon as it sees the
	// initialized event) has to be resolved and re-applied here.
	ResolveBreakpoints();

	auto resp = MakeResponse(request, true);
	SendResponse(std::move(resp));

	// Breakpoints set before launch were pending; tell the client where each
	// one landed now that symbols are loaded
	SendBreakpointChanges();
}

void DapServer::HandleConfigurationDone(const JsonValue& request)
{
	_configDone = true;

	// Last chance to install breakpoints before execution resumes
	ResolveBreakpoints();
	SendBreakpointChanges();

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

			// Resolve the symbol and the source line through the absolute ROM
			// offset: a FastROM/HiROM game executes the same bytes through
			// several bank mirrors, and only the absolute address lines up
			// with what the .dbg recorded.
			AddressInfo relAddr { (int32_t)addr, DebugUtilities::GetCpuMemoryType(cpu) };
			AddressInfo absAddr = dbg->GetAbsoluteAddress(relAddr);
			bool haveRom = absAddr.Address >= 0 && DebugUtilities::IsRom(absAddr.Type);

			std::string symName;
			if(haveRom) {
				symName = _sourceMapper.GetSymbolNameByRomOffset((uint32_t)absAddr.Address);
			}
			if(symName.empty()) {
				symName = _sourceMapper.GetSymbolName(addr);
			}
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

			SourceLocation loc;
			if(haveRom) {
				loc = _sourceMapper.GetSourceLocationByRomOffset((uint32_t)absAddr.Address);
			}
			if(!loc.valid) {
				loc = _sourceMapper.GetSourceLocation(addr);
			}
			if(loc.valid && !loc.file.empty() && loc.fileFound) {
				frame.Set("line", JsonValue::MakeNumber(loc.line));
				frame.Set("column", JsonValue::MakeNumber(1));
				auto source = JsonValue::MakeObject();
				source.Set("name", JsonValue::MakeString(loc.file));
				source.Set("path", JsonValue::MakeString(loc.file));
				frame.Set("source", std::move(source));
			} else {
				// No line mapping for this address, or none to a file on this
				// disk (a library built elsewhere). Emit no source at all: a
				// DAP source is only loadable through a path or a non-zero
				// sourceReference, and one carrying neither makes the client
				// ask for its content with a "source" request we do not serve.
				// Subdue the frame instead — the address is already its name,
				// and the disassembly view covers reading the code.
				frame.Set("line", JsonValue::MakeNumber(0));
				frame.Set("column", JsonValue::MakeNumber(0));
				frame.Set("presentationHint", JsonValue::MakeString("subtle"));
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
			StackFrameInfo csFrames[MAX_CALLSTACK];
			uint32_t csCount = 0;
			csm->GetCallstack(csFrames, csCount);

			// CallstackManager lists calls oldest first; the client wants the
			// caller of the current frame next, then its caller, and so on
			for(uint32_t n = 0; n < csCount && frameCount < 64; n++) {
				const StackFrameInfo& call = csFrames[csCount - 1 - n];
				const char* annotation = nullptr;
				if(call.Flags == StackFrameFlags::Nmi) annotation = "NMI";
				else if(call.Flags == StackFrameFlags::Irq) annotation = "IRQ";
				frames.Push(makeFrame(frameCount, call.Source, annotation));
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

// A source line is many instructions, so a single-instruction step mostly
// leaves the client sitting on the same line — and makes stepIn and next look
// identical, since they only diverge on the one instruction that is a call.
// Walk instructions until the reported position is somewhere the user would
// recognise as a new place.
static const int STEP_BREAK_TIMEOUT_MS = 5000;
// Enough to cross any reasonable statement; a step that runs into a long
// unmapped routine stops where it got to rather than hanging the session.
static const uint32_t MAX_STEP_INSTRUCTIONS = 200000;
// Each step costs a ~10ms round trip to the emulation thread, so a line that
// spins — a macro like wait_autojoy!, or any hardware poll — takes minutes one
// instruction at a time. Stay exact for as long as a straight-line statement
// could plausibly run, then escalate: past that point we are going round a loop
// that lives entirely on one line, and only getting out of it matters.
static const uint32_t EXACT_STEP_INSTRUCTIONS = 32;
// StepOver ignores a count > 1, so batching has to use plain Step and lean on
// the depth handling below to keep step-over semantics.
static const uint32_t MAX_STEP_BATCH = 64;
// Hard ceiling on one step request. A wait on something the user has not done
// yet (a button press) would otherwise never return; stopping inside it at
// least hands control back and shows where it is stuck.
static const int STEP_TIME_BUDGET_MS = 5000;
// A callee's first instructions often carry no line record (the label itself
// gets none), so give a new frame this long to produce one before writing it
// off as code we have no source for.
static const uint32_t CALLEE_PROBE_INSTRUCTIONS = 64;

void DapServer::CurrentLocation(CpuType cpu, std::string& file, int& line, uint32_t& depth, uint32_t* pcOut, bool* cLineOut)
{
	file.clear();
	line = 0;
	depth = 0;
	if(cLineOut) {
		*cLineOut = false;
	}

	DebuggerRequest req = _emu->GetDebugger(false);
	Debugger* dbg = req.GetDebugger();
	if(!dbg) {
		return;
	}

	uint32_t pc = dbg->GetProgramCounter(cpu, true);
	if(pcOut) {
		*pcOut = pc;
	}

	// Resolve through the ROM offset for the same reason the stack trace does:
	// a FastROM/HiROM game runs the same bytes at several addresses.
	SourceLocation loc;
	AddressInfo relAddr { (int32_t)pc, DebugUtilities::GetCpuMemoryType(cpu) };
	AddressInfo absAddr = dbg->GetAbsoluteAddress(relAddr);
	if(absAddr.Address >= 0 && DebugUtilities::IsRom(absAddr.Type)) {
		loc = _sourceMapper.GetSourceLocationByRomOffset((uint32_t)absAddr.Address);
	}
	if(!loc.valid) {
		loc = _sourceMapper.GetSourceLocation(pc);
	}
	if(loc.valid) {
		file = loc.file;
		line = loc.line;
		if(cLineOut) {
			*cLineOut = loc.cLine;
		}
	}

	CallstackManager* csm = dbg->GetCallstackManager(cpu);
	if(csm) {
		StackFrameInfo frames[MAX_CALLSTACK];
		uint32_t count = 0;
		csm->GetCallstack(frames, count);
		depth = count;
	}
}

void DapServer::StepSourceLine(const JsonValue& request, bool intoCalls)
{
	const JsonValue& args = request["arguments"];
	int threadId = (int)args["threadId"].GetNumber();
	CpuType cpu = ThreadIdToCpuType(threadId);
	bool byInstruction = args["granularity"].GetString() == "instruction";

	auto resp = MakeResponse(request, true);
	SendResponse(std::move(resp));

	StepType overCalls = intoCalls ? StepType::Step : StepType::StepOver;

	// The disassembly view asks for one instruction and means it.
	if(byInstruction) {
		DebuggerRequest req = _emu->GetDebugger(false);
		if(Debugger* dbg = req.GetDebugger()) {
			dbg->Step(cpu, 1, overCalls, BreakSource::CpuStep);
		}
		return;
	}

	std::string startFile;
	int startLine = 0;
	uint32_t startDepth = 0;
	uint32_t lastPc = 0;
	bool startInC = false;
	CurrentLocation(cpu, startFile, startLine, startDepth, &lastPc, &startInC);
	// Stepping C, the assembly under it is no place to stop: not cc65's
	// runtime helpers a statement calls, nor the generated code of a line
	// A loop that lives on one line (`loop { wait_nmi!(); }`) never reaches
	// another line; coming back round to where the step began ends it instead
	uint32_t startPc = lastPc;
	bool leftStart = false;
	// Set when a step left the PC where it was: the CPU is halted (WAI), and
	// each further plain step would only advance one idle cycle
	bool halted = false;

	_stepping = true;
	uint32_t probed = 0;
	uint32_t onSameLine = 0;
	uint32_t batch = 1;
	auto deadline = std::chrono::steady_clock::now()
		+ std::chrono::milliseconds(STEP_TIME_BUDGET_MS);

	for(uint32_t stepped = 0; stepped < MAX_STEP_INSTRUCTIONS; ) {
		std::string file;
		int line = 0;
		uint32_t depth = 0;
		StepType type = overCalls;

		if(std::chrono::steady_clock::now() >= deadline) {
			// Out of time — stop here rather than leaving the client hanging.
			break;
		}

		if(halted) {
			// Wait out the halt in one step over: the debugger runs to the
			// instruction after it, through whatever interrupt ends it
			type = StepType::StepOver;
			batch = 1;
		} else if(onSameLine >= EXACT_STEP_INSTRUCTIONS && batch < MAX_STEP_BATCH) {
			// Double each time rather than jumping to the cap, so a loop that
			// ends just after the exact phase is not overshot by much.
			batch *= 2;
		}
		stepped += batch;

		_listener->ResetBreak();
		{
			DebuggerRequest req = _emu->GetDebugger(false);
			Debugger* dbg = req.GetDebugger();
			if(!dbg) {
				break;
			}
			dbg->Step(cpu, (int)batch, batch > 1 && !halted ? StepType::Step : type,
				BreakSource::CpuStep);
		}
		if(!_listener->WaitForBreak(STEP_BREAK_TIMEOUT_MS)) {
			break;
		}

		if(_listener->LastBreakWasBreakpoint()) {
			// A breakpoint outranks the step that was in progress.
			_stepping = false;
			SendStoppedEvent(DapStoppedReason::Breakpoint, cpu);
			return;
		}

		uint32_t pc = 0;
		bool inC = false;
		CurrentLocation(cpu, file, line, depth, &pc, &inC);
		bool atSource = line != 0 && (!startInC || inC);
		halted = pc == lastPc;
		lastPc = pc;
		if(pc != startPc) {
			leftStart = true;
		} else if(leftStart && depth == startDepth) {
			break;
		}

		if(depth < startDepth) {
			// Returned to the caller — that is a new place either way.
			break;
		}
		if(depth > startDepth) {
			if(intoCalls && atSource) {
				// Stepped into something we have source for.
				break;
			}
			if(intoCalls && ++probed < CALLEE_PROBE_INSTRUCTIONS) {
				// Still in the prologue, most likely — keep looking.
				continue;
			}
			// Genuinely no source for this callee: nothing to show, so leave
			// it in one operation rather than single-stepping the whole body.
			probed = 0;
			{
				DebuggerRequest req = _emu->GetDebugger(false);
				if(Debugger* dbg = req.GetDebugger()) {
					_listener->ResetBreak();
					dbg->Step(cpu, 1, StepType::StepOut, BreakSource::CpuStep);
				}
			}
			if(_listener->WaitForBreak(STEP_BREAK_TIMEOUT_MS) && _listener->LastBreakWasBreakpoint()) {
				// The callee — or an interrupt that fired mid-step, like the
				// NMI handler — holds a breakpoint: stepping over code must
				// not step over the breakpoints in it
				_stepping = false;
				SendStoppedEvent(DapStoppedReason::Breakpoint, cpu);
				return;
			}
			continue;
		}
		probed = 0;
		if(atSource && (line != startLine || file != startFile)) {
			break;
		}
		onSameLine++;
	}
	_stepping = false;

	SendStoppedEvent(DapStoppedReason::Step, cpu);
}

void DapServer::HandleStepIn(const JsonValue& request)
{
	StepSourceLine(request, true);
}

void DapServer::HandleNext(const JsonValue& request)
{
	StepSourceLine(request, false);
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

// Absolute (ROM) memory type for a CPU, used so execution breakpoints match
// regardless of which bank mirror the code is fetched through (FastROM, HiROM).
static MemoryType GetPrgRomMemoryType(CpuType cpuType)
{
	switch(cpuType) {
		case CpuType::Snes:
		case CpuType::Sa1:
		case CpuType::Gsu:
		case CpuType::Cx4:
		case CpuType::St018:
		case CpuType::NecDsp: return MemoryType::SnesPrgRom;
		case CpuType::Spc: return MemoryType::SpcRam;
		case CpuType::Gameboy: return MemoryType::GbPrgRom;
		case CpuType::Nes: return MemoryType::NesPrgRom;
		case CpuType::Pce: return MemoryType::PcePrgRom;
		case CpuType::Sms: return MemoryType::SmsPrgRom;
		case CpuType::Gba: return MemoryType::GbaPrgRom;
		case CpuType::Ws: return MemoryType::WsPrgRom;
	}
	return MemoryType::SnesPrgRom;
}

CpuType DapServer::PrimaryCpu() const
{
	auto cpuTypes = _emu->GetCpuTypes();
	return cpuTypes.empty() ? CpuType::Snes : cpuTypes[0];
}

static bool IsSourcePath(const std::string& path)
{
	// The disassembly view's address breakpoints arrive as "0x..." or "$..."
	return !path.empty() && path[0] != '0' && path[0] != '$';
}

// Shown on a breakpoint set before the program is loaded
static const char* PENDING_MESSAGE = "Placed once the program is loaded";

std::string DapServer::CheckCondition(Debugger* dbg, const std::string& condition) const
{
	if(condition.empty() || !dbg) {
		// Nothing to check, or no debugger to check it with until launch
		return "";
	}
	// An expression that does not parse compiles to nothing and silently
	// never fires, so catch it here where the client can show why
	EvalResultType type = EvalResultType::Invalid;
	dbg->EvaluateExpression(condition, PrimaryCpu(), type, false);
	return type == EvalResultType::Invalid ? "Invalid condition: " + condition : "";
}

DapServer::BreakpointStatus DapServer::ResolveSource(const std::string& sourcePath, const BreakpointRequest& req, Breakpoint& out)
{
	BreakpointStatus status;
	status.line = req.line;
	CpuType cpu = PrimaryCpu();
	int32_t addr = -1;
	bool useRomOffset = false;

	if(IsSourcePath(sourcePath)) {
		if(!_sourceMapper.IsLoaded()) {
			status.message = PENDING_MESSAGE;
			return status;
		}
		// The .dbg records ROM file offsets. Matching on the absolute address
		// makes the breakpoint fire whichever bank mirror executes the code —
		// a FastROM game runs the same bytes at $80:xxxx as at $00:xxxx.
		int line = req.line;
		uint32_t romOffset = _sourceMapper.GetRomOffset(sourcePath, req.line, &line);
		if(romOffset != 0xFFFFFFFF) {
			addr = (int32_t)romOffset;
			useRomOffset = true;
		} else {
			uint32_t resolved = _sourceMapper.GetAddress(sourcePath, req.line, &line);
			if(resolved != 0xFFFFFFFF) {
				addr = (int32_t)resolved;
			}
		}
		if(addr < 0) {
			status.message = "No code at or near this line";
			return status;
		}
		status.line = line;
	} else {
		// Disassembly view: the "line" is the address, or the path holds it
		if(req.line > 0) {
			addr = (int32_t)req.line;
		}
		if(addr < 0 && sourcePath.size() > 2 && sourcePath[0] == '0' && (sourcePath[1] == 'x' || sourcePath[1] == 'X')) {
			try { addr = (int32_t)std::stoul(sourcePath.substr(2), nullptr, 16); } catch(...) {}
		} else if(addr < 0 && !sourcePath.empty() && sourcePath[0] == '$') {
			try { addr = (int32_t)std::stoul(sourcePath.substr(1), nullptr, 16); } catch(...) {}
		}
		if(addr < 0) {
			status.message = "Not an address";
			return status;
		}
	}

	DebuggerRequest dbgReq = _emu->GetDebugger(false);
	status.message = CheckCondition(dbgReq.GetDebugger(), req.condition);
	if(!status.message.empty()) {
		return status;
	}

	MemoryType memoryType = useRomOffset ? GetPrgRomMemoryType(cpu) : DebugUtilities::GetCpuMemoryType(cpu);
	out.Init(req.dapId, cpu, memoryType, BreakpointTypeFlags::Execute, addr, addr, req.condition);
	status.verified = true;
	return status;
}

DapServer::BreakpointStatus DapServer::ResolveInstruction(const BreakpointRequest& req, Breakpoint& out)
{
	BreakpointStatus status;
	if(!req.validAddress) {
		status.message = "Not an instruction reference";
		return status;
	}

	DebuggerRequest dbgReq = _emu->GetDebugger(false);
	Debugger* dbg = dbgReq.GetDebugger();
	if(!dbg) {
		status.message = PENDING_MESSAGE;
		return status;
	}
	status.message = CheckCondition(dbg, req.condition);
	if(!status.message.empty()) {
		return status;
	}

	// Install on the ROM offset when the address is in ROM, for the same
	// reason as source breakpoints: it then fires through any bank mirror
	CpuType cpu = PrimaryCpu();
	AddressInfo relAddr { (int32_t)req.address, DebugUtilities::GetCpuMemoryType(cpu) };
	AddressInfo absAddr = dbg->GetAbsoluteAddress(relAddr);
	if(absAddr.Address >= 0 && DebugUtilities::IsRom(absAddr.Type)) {
		out.Init(req.dapId, cpu, absAddr.Type, BreakpointTypeFlags::Execute, absAddr.Address, absAddr.Address, req.condition);
	} else {
		out.Init(req.dapId, cpu, relAddr.Type, BreakpointTypeFlags::Execute, relAddr.Address, relAddr.Address, req.condition);
	}
	status.verified = true;
	return status;
}

DapServer::BreakpointStatus DapServer::ResolveFunction(const BreakpointRequest& req, Breakpoint& out)
{
	BreakpointStatus status;
	if(!_sourceMapper.IsLoaded()) {
		status.message = PENDING_MESSAGE;
		return status;
	}
	uint32_t romOffset = SourceMapper::NoRomOffset;
	uint32_t cpuAddr = 0;
	if(!_sourceMapper.FindFunction(req.name, romOffset, cpuAddr)) {
		status.message = "No function or label named " + req.name;
		return status;
	}

	DebuggerRequest dbgReq = _emu->GetDebugger(false);
	status.message = CheckCondition(dbgReq.GetDebugger(), req.condition);
	if(!status.message.empty()) {
		return status;
	}

	// On the ROM offset where there is one, so it fires through any bank mirror
	CpuType cpu = PrimaryCpu();
	if(romOffset != SourceMapper::NoRomOffset) {
		out.Init(req.dapId, cpu, GetPrgRomMemoryType(cpu), BreakpointTypeFlags::Execute, romOffset, romOffset, req.condition);
	} else {
		MemoryType memoryType = DebugUtilities::GetCpuMemoryType(cpu);
		out.Init(req.dapId, cpu, memoryType, BreakpointTypeFlags::Execute, cpuAddr, cpuAddr, req.condition);
	}
	status.verified = true;
	return status;
}

void DapServer::ResolveBreakpoints()
{
	_installed.clear();
	for(auto& entry : _requestsBySource) {
		if(IsSourcePath(entry.first)) {
			// Report locations in this file with the client's own path
			_sourceMapper.NoteClientPath(entry.first);
		}
		for(BreakpointRequest& req : entry.second) {
			Breakpoint bp;
			req.status = ResolveSource(entry.first, req, bp);
			if(req.status.verified) {
				_installed.push_back(bp);
			}
		}
	}
	for(BreakpointRequest& req : _instructionRequests) {
		Breakpoint bp;
		req.status = ResolveInstruction(req, bp);
		if(req.status.verified) {
			_installed.push_back(bp);
		}
	}
	for(BreakpointRequest& req : _functionRequests) {
		Breakpoint bp;
		req.status = ResolveFunction(req, bp);
		if(req.status.verified) {
			_installed.push_back(bp);
		}
	}

	DebuggerRequest dbgReq = _emu->GetDebugger(false);
	if(Debugger* dbg = dbgReq.GetDebugger()) {
		dbg->SetBreakpoints(_installed.data(), (uint32_t)_installed.size());
	}
	// No ROM loaded yet: launch resolves and installs them again
}

JsonValue DapServer::BreakpointJson(BreakpointRequest& req, BreakpointKind kind)
{
	auto bp = JsonValue::MakeObject();
	bp.Set("id", JsonValue::MakeNumber(req.dapId));
	bp.Set("verified", JsonValue::MakeBool(req.status.verified));
	if(kind == BreakpointKind::Instruction) {
		if(req.validAddress) {
			char ref[32];
			snprintf(ref, sizeof(ref), "0x%X", req.address);
			bp.Set("instructionReference", JsonValue::MakeString(ref));
		}
	} else if(kind == BreakpointKind::Source) {
		// VSCode moves the breakpoint marker to the line reported here
		bp.Set("line", JsonValue::MakeNumber(req.status.line));
	}
	if(!req.status.message.empty()) {
		bp.Set("message", JsonValue::MakeString(req.status.message));
	}
	req.reported = req.status;
	req.hasReported = true;
	return bp;
}

void DapServer::SendBreakpointChanges()
{
	auto send = [this](BreakpointRequest& req, BreakpointKind kind) {
		if(req.hasReported && req.reported == req.status) {
			return;
		}
		auto evt = MakeEvent(DapEvent::Breakpoint);
		auto body = JsonValue::MakeObject();
		body.Set("reason", JsonValue::MakeString("changed"));
		body.Set("breakpoint", BreakpointJson(req, kind));
		evt.Set("body", std::move(body));
		SendEvent(std::move(evt));
	};
	for(auto& entry : _requestsBySource) {
		for(BreakpointRequest& req : entry.second) {
			send(req, BreakpointKind::Source);
		}
	}
	for(BreakpointRequest& req : _instructionRequests) {
		send(req, BreakpointKind::Instruction);
	}
	for(BreakpointRequest& req : _functionRequests) {
		send(req, BreakpointKind::Function);
	}
}

void DapServer::HandleSetBreakpoints(const JsonValue& request)
{
	const JsonValue& args = request["arguments"];
	const JsonValue& source = args["source"];

	std::string sourcePath = source["path"].GetString();
	std::string sourceName = source["name"].GetString();
	std::string sourceKey = !sourcePath.empty() ? sourcePath : sourceName;

	std::vector<BreakpointRequest> requests;
	const auto& breakpointsList = args["breakpoints"].GetArray();
	for(size_t i = 0; i < breakpointsList.size(); i++) {
		const JsonValue& bp = breakpointsList[i];
		BreakpointRequest req;
		req.dapId = _nextBreakpointId++;
		req.line = bp["line"].GetType() == JsonValue::Type::Number ? (int)bp["line"].GetNumber() : 0;
		req.condition = bp["condition"].GetString();
		requests.push_back(std::move(req));
	}

	if(requests.empty()) {
		_requestsBySource.erase(sourceKey);
	} else {
		_requestsBySource[sourceKey] = std::move(requests);
	}

	ResolveBreakpoints();

	auto results = JsonValue::MakeArray();
	auto it = _requestsBySource.find(sourceKey);
	if(it != _requestsBySource.end()) {
		for(BreakpointRequest& req : it->second) {
			results.Push(BreakpointJson(req, BreakpointKind::Source));
		}
	}

	auto resp = MakeResponse(request, true);
	auto body = JsonValue::MakeObject();
	body.Set("breakpoints", std::move(results));
	resp.Set("body", std::move(body));
	SendResponse(std::move(resp));
}

void DapServer::HandleSetInstructionBreakpoints(const JsonValue& request)
{
	const JsonValue& args = request["arguments"];

	_instructionRequests.clear();
	const auto& breakpointsList = args["breakpoints"].GetArray();
	for(size_t i = 0; i < breakpointsList.size(); i++) {
		const JsonValue& bp = breakpointsList[i];
		BreakpointRequest req;
		req.dapId = _nextBreakpointId++;
		req.condition = bp["condition"].GetString();

		// The reference is what stackTrace and disassemble hand out: "0x%X"
		std::string ref = bp["instructionReference"].GetString();
		int offset = bp["offset"].GetType() == JsonValue::Type::Number ? (int)bp["offset"].GetNumber() : 0;
		try {
			size_t used = 0;
			int base = 10;
			std::string digits = ref;
			if(ref.size() > 2 && ref[0] == '0' && (ref[1] == 'x' || ref[1] == 'X')) {
				digits = ref.substr(2);
				base = 16;
			} else if(!ref.empty() && ref[0] == '$') {
				digits = ref.substr(1);
				base = 16;
			}
			unsigned long value = std::stoul(digits, &used, base);
			if(used == digits.size()) {
				req.address = (uint32_t)((long long)value + offset);
				req.validAddress = true;
			}
		} catch(...) {
		}
		_instructionRequests.push_back(std::move(req));
	}

	ResolveBreakpoints();

	auto results = JsonValue::MakeArray();
	for(BreakpointRequest& req : _instructionRequests) {
		results.Push(BreakpointJson(req, BreakpointKind::Instruction));
	}

	auto resp = MakeResponse(request, true);
	auto body = JsonValue::MakeObject();
	body.Set("breakpoints", std::move(results));
	resp.Set("body", std::move(body));
	SendResponse(std::move(resp));
}

void DapServer::HandleSetFunctionBreakpoints(const JsonValue& request)
{
	const JsonValue& args = request["arguments"];

	_functionRequests.clear();
	const auto& breakpointsList = args["breakpoints"].GetArray();
	for(size_t i = 0; i < breakpointsList.size(); i++) {
		const JsonValue& bp = breakpointsList[i];
		BreakpointRequest req;
		req.dapId = _nextBreakpointId++;
		req.name = bp["name"].GetString();
		req.condition = bp["condition"].GetString();
		_functionRequests.push_back(std::move(req));
	}

	ResolveBreakpoints();

	auto results = JsonValue::MakeArray();
	for(BreakpointRequest& req : _functionRequests) {
		results.Push(BreakpointJson(req, BreakpointKind::Function));
	}

	auto resp = MakeResponse(request, true);
	auto body = JsonValue::MakeObject();
	body.Set("breakpoints", std::move(results));
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

			// The label at this instruction, if the symbols name one
			AddressInfo relAddr { line.Address, DebugUtilities::GetCpuMemoryType(primaryCpu) };
			AddressInfo absAddr = dbg->GetAbsoluteAddress(relAddr);
			if(absAddr.Address >= 0 && DebugUtilities::IsRom(absAddr.Type)) {
				std::string label = _sourceMapper.GetLabelAtRomOffset((uint32_t)absAddr.Address);
				if(!label.empty()) {
					instr.Set("symbol", JsonValue::MakeString(label));
				}
			}

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
