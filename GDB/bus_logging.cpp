#include "bus_logging.h"

namespace BusLogging {
	std::atomic<bool> logBus{false};
	std::atomic<bool> logVram{false};
}
