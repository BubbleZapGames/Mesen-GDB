#pragma once
#include <string>
#include <cstdint>
#include <atomic>
#include <memory>
#include <vector>
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
	std::vector<Breakpoint> _breakpoints;
	SourceMapper _sourceMapper;

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
	void SendTerminatedEvent();

	bool IsConfigDone() const { return _configDone; }
};
