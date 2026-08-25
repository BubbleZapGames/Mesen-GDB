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
	// Breakpoints per source path — DAP sends setBreakpoints once per file,
	// so keeping one flat list would drop every other file's breakpoints.
	std::map<std::string, std::vector<Breakpoint>> _breakpointsBySource;

	// The original requests, kept so they can be resolved again once symbols
	// are available. VSCode sends setBreakpoints as soon as it sees the
	// initialized event, which is before launch has loaded the .dbg.
	struct SourceBreakpointRequest {
		uint32_t dapId;
		int line;
		std::string condition;
	};
	std::map<std::string, std::vector<SourceBreakpointRequest>> _requestsBySource;

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
	void SyncBreakpoints();
	// Rebuild _breakpointsBySource from _requestsBySource using current symbols
	void ResolveBreakpoints();
	bool ResolveOne(const std::string& sourcePath, const SourceBreakpointRequest& req, Breakpoint& out) const;

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
	/** Where the CPU is now: mapped source line (0 if none) and stack depth. */
	void CurrentLocation(CpuType cpu, std::string& file, int& line, uint32_t& depth);
	/** Shared body of stepIn/next: walk instructions until the line changes. */
	void StepSourceLine(const JsonValue& request, bool intoCalls);

	// Request handlers — Phase 2
	void HandleStepIn(const JsonValue& request);
	void HandleNext(const JsonValue& request);
	void HandleStepOut(const JsonValue& request);
	void HandleSetBreakpoints(const JsonValue& request);
	void HandleDisassemble(const JsonValue& request);

	// Request handlers — Phase 4
	void HandleEvaluate(const JsonValue& request);
	void HandleReadMemory(const JsonValue& request);
	void HandleWriteMemory(const JsonValue& request);

public:
	DapServer(Emulator* emu, FILE* dapOutput = stdout);
	~DapServer();

	void Run();
	void SendStoppedEvent(const char* reason, CpuType cpu);
	bool IsStepping() const { return _stepping; }
	void SendTerminatedEvent();

	bool IsConfigDone() const { return _configDone; }
};
