#pragma once
#include <cstdint>

namespace DapCommand {
	constexpr const char* Initialize = "initialize";
	constexpr const char* Launch = "launch";
	constexpr const char* ConfigurationDone = "configurationDone";
	constexpr const char* Threads = "threads";
	constexpr const char* StackTrace = "stackTrace";
	constexpr const char* Scopes = "scopes";
	constexpr const char* Variables = "variables";
	constexpr const char* Continue = "continue";
	constexpr const char* Pause = "pause";
	constexpr const char* Disconnect = "disconnect";
}

namespace DapEvent {
	constexpr const char* Initialized = "initialized";
	constexpr const char* Stopped = "stopped";
	constexpr const char* Continued = "continued";
	constexpr const char* Terminated = "terminated";
	constexpr const char* Exited = "exited";
}

namespace DapStoppedReason {
	constexpr const char* Entry = "entry";
	constexpr const char* Breakpoint = "breakpoint";
	constexpr const char* Step = "step";
	constexpr const char* Pause = "pause";
}

enum class ScopeType : uint16_t {
	Registers = 1,
	Flags = 2
};