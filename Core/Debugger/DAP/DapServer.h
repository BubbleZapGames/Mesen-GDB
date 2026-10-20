#pragma once
#include <string>
#include <cstdint>
#include <atomic>
#include <memory>
#include <vector>
#include <map>
#include "DapTypes.h"
#include "DapJson.h"
#include "DapMessageReader.h"
#include "DapMessageWriter.h"
#include "SourceMapper.h"

class Debugger;
class Emulator;
class Breakpoint;
class DapNotificationListener;
enum class CpuType : uint8_t;

class DapServer {
private:
	Emulator* _emu;
	DapMessageReader _reader;
	DapMessageWriter _writer;
	std::shared_ptr<DapNotificationListener> _listener;
	std::atomic<int> _seq{1};
	bool _running = false;
	bool _configDone = false;
	bool _stopOnEntry = true;
	uint32_t _nextBreakpointId = 1;

	// Where a breakpoint stands, as the client is told it
	struct BreakpointStatus {
		bool verified = false;
		// Source breakpoints: the line it landed on, which can differ from
		// the one asked for when that line has no code
		int line = 0;
		// Why it is not verified
		std::string message;
		bool operator==(const BreakpointStatus& o) const {
			return verified == o.verified && line == o.line && message == o.message;
		}
	};

	// A breakpoint as the client asked for it, kept so it can be resolved
	// again once symbols are available: VSCode sends setBreakpoints as soon as
	// it sees the initialized event, which is before launch has loaded the .dbg.
	struct BreakpointRequest {
		uint32_t dapId = 0;
		int line = 0;               // source breakpoints: the line asked for
		uint32_t address = 0;       // instruction breakpoints: the CPU address
		bool validAddress = false;
		std::string condition;
		BreakpointStatus status;
		// What the client was last told, so changes go out as events
		BreakpointStatus reported;
		bool hasReported = false;
	};
	// Per source path — DAP sends setBreakpoints once per file, so one flat
	// list would drop every other file's breakpoints
	std::map<std::string, std::vector<BreakpointRequest>> _requestsBySource;
	// From the disassembly view; each setInstructionBreakpoints replaces them all
	std::vector<BreakpointRequest> _instructionRequests;
	// What is installed in the debugger: every request that resolved
	std::vector<Breakpoint> _installed;

	SourceMapper _sourceMapper;

	// Set while a source-line step is walking instructions. The notification
	// listener checks it so intermediate breaks stay invisible to the client.
	std::atomic<bool> _stepping{false};

	// Response/event helpers
	JsonValue MakeResponse(const JsonValue& request, bool success);
	JsonValue MakeEvent(const char* event);
	void SendResponse(JsonValue&& response);
	void SendEvent(JsonValue&& event);

	// Thread/CPU mapping
	static int CpuTypeToThreadId(CpuType cpu);
	static CpuType ThreadIdToCpuType(int threadId);
	static const char* CpuTypeName(CpuType cpu);

	// Breakpoint helpers
	CpuType PrimaryCpu() const;
	// Resolve every request against the current symbols and install the ones
	// that resolved
	void ResolveBreakpoints();
	BreakpointStatus ResolveSource(const std::string& sourcePath, const BreakpointRequest& req, Breakpoint& out);
	BreakpointStatus ResolveInstruction(const BreakpointRequest& req, Breakpoint& out);
	// Empty if the condition is usable (or cannot be checked yet), else why not
	std::string CheckCondition(Debugger* dbg, const std::string& condition) const;
	JsonValue BreakpointJson(BreakpointRequest& req, bool instruction);
	// Tell the client about breakpoints whose status changed since it last heard
	void SendBreakpointChanges();

	// Request handlers — Phase 1
	void HandleInitialize(const JsonValue& request);
	void HandleLaunch(const JsonValue& request);
	void HandleConfigurationDone(const JsonValue& request);
	void HandleThreads(const JsonValue& request);
	void HandleStackTrace(const JsonValue& request);
	void HandleScopes(const JsonValue& request);
	void HandleVariables(const JsonValue& request);
	void HandleContinue(const JsonValue& request);
	void HandlePause(const JsonValue& request);
	void HandleDisconnect(const JsonValue& request);

	// Stepping helpers
	/** Where the CPU is now: mapped source line (0 if none), stack depth, PC, and whether the line is C. */
	void CurrentLocation(CpuType cpu, std::string& file, int& line, uint32_t& depth, uint32_t* pcOut = nullptr, bool* cLineOut = nullptr);
	/** Shared body of stepIn/next: walk instructions until the line changes. */
	void StepSourceLine(const JsonValue& request, bool intoCalls);

	// Request handlers — Phase 2
	void HandleStepIn(const JsonValue& request);
	void HandleNext(const JsonValue& request);
	void HandleStepOut(const JsonValue& request);
	void HandleSetBreakpoints(const JsonValue& request);
	void HandleSetInstructionBreakpoints(const JsonValue& request);
	void HandleDisassemble(const JsonValue& request);

	// Request handlers — Phase 4
	void HandleEvaluate(const JsonValue& request);
	void HandleReadMemory(const JsonValue& request);
	void HandleWriteMemory(const JsonValue& request);

public:
	DapServer(Emulator* emu, FILE* dapOutput = stdout);
	~DapServer();

	void Run();
	// End of session: no more notifications, and close the output. Call after
	// stopping the emulator, whose shutdown still reports to the client.
	void Close();
	void SendStoppedEvent(const char* reason, CpuType cpu);
	bool IsStepping() const { return _stepping; }
	void SendTerminatedEvent();

	bool IsConfigDone() const { return _configDone; }
};
