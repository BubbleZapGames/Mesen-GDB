#pragma once
#include <string>
#include <cstdint>
#include <memory>
#include "DapTypes.h"
#include "DapJson.h"

class INotificationListener;
class Debugger;
class Emulator;
enum class CpuType : uint8_t;

class DapServer {
private:
	Debugger* _debugger;
	Emulator* _emulator;
	INotificationListener* _notificationListener;
	bool _isRunning;
	
public:
	DapServer(Debugger* debugger, Emulator* emulator, INotificationListener* notificationListener);
	~DapServer();
	
	void Run();
	void Stop();
	
private:
	void HandleInitializationRequest(const JsonValue& request, DapMessageWriter& writer);
	void HandleLaunchRequest(const JsonValue& request, DapMessageWriter& writer);
	void HandleConfigurationDoneRequest(const JsonValue& request, DapMessageWriter& writer);
	void HandleThreadsRequest(const JsonValue& request, DapMessageWriter& writer);
	void HandleStackTraceRequest(const JsonValue& request, DapMessageWriter& writer);
	void HandleScopesRequest(const JsonValue& request, DapMessageWriter& writer);
	void HandleVariablesRequest(const JsonValue& request, DapMessageWriter& writer);
	void HandleContinueRequest(const JsonValue& request, DapMessageWriter& writer);
	void HandlePauseRequest(const JsonValue& request, DapMessageWriter& writer);
	void HandleDisconnectRequest(const JsonValue& request, DapMessageWriter& writer);
	void HandleStepInRequest(const JsonValue& request, DapMessageWriter& writer);
	void HandleNextRequest(const JsonValue& request, DapMessageWriter& writer);
	void HandleStepOutRequest(const JsonValue& request, DapMessageWriter& writer);
	void HandleSetBreakpointsRequest(const JsonValue& request, DapMessageWriter& writer);
	void HandleDisassembleRequest(const JsonValue& request, DapMessageWriter& writer);
	
	void SendInitializedEvent(DapMessageWriter& writer);
};
