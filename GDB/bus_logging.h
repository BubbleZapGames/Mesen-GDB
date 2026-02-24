#pragma once
#include <atomic>
#include <cstdint>

namespace BusLogging {
	extern std::atomic<bool> logBus;
	extern std::atomic<bool> logVram;
}
