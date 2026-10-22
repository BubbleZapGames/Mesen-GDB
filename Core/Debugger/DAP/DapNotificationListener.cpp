#include "DapNotificationListener.h"
#include "DapServer.h"
#include "DapTypes.h"
#include "Core/Debugger/DebugTypes.h"
#include "Core/Shared/CpuType.h"
#include <chrono>

DapNotificationListener::DapNotificationListener(DapServer* server) : _server(server) {}

void DapNotificationListener::ResetBreak()
{
	std::lock_guard<std::mutex> lock(_mutex);
	_breakOccurred = false;
	_breakWasBreakpoint = false;
}

bool DapNotificationListener::LastBreakWasBreakpoint()
{
	std::lock_guard<std::mutex> lock(_mutex);
	return _breakWasBreakpoint;
}

bool DapNotificationListener::WaitForBreak(int timeoutMs)
{
	std::unique_lock<std::mutex> lock(_mutex);
	if(_breakOccurred) {
		_breakOccurred = false;
		return true;
	}
	bool arrived = _cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
		[this]{ return _breakOccurred; });
	_breakOccurred = false;
	return arrived;
}

void DapNotificationListener::ProcessNotification(ConsoleNotificationType type, void* parameter)
{
	if(!_server || !_server->IsConfigDone()) {
		// Suppress all events before configurationDone (e.g., the initial
		// break from LoadRom fires before VSCode is ready).
		return;
	}

	if(type == ConsoleNotificationType::CodeBreak) {
		if(_server->IsNoDebug()) {
			// Run Without Debugging has no way to continue, so a break (BRK,
			// a debugger exception) would freeze the game: carry on instead
			_server->Resume();
			return;
		}
		{
			// Wake a step loop waiting on this break before anything else.
			std::lock_guard<std::mutex> lock(_mutex);
			_breakOccurred = true;
			_breakWasBreakpoint =
				((BreakEvent*)parameter)->Source == BreakSource::Breakpoint;
			_cv.notify_all();
		}

		if(_server->IsStepping()) {
			// Mid-step: the loop decides whether this address is somewhere the
			// user should see, and sends the stopped event once it settles.
			return;
		}

		BreakEvent* breakEvent = (BreakEvent*)parameter;
		const char* reason;

		switch(breakEvent->Source) {
			case BreakSource::Breakpoint:
				reason = DapStoppedReason::Breakpoint;
				break;
			case BreakSource::CpuStep:
				reason = DapStoppedReason::Step;
				break;
			case BreakSource::Pause:
				reason = DapStoppedReason::Pause;
				break;
			default:
				reason = DapStoppedReason::Pause;
				break;
		}

		_server->SendStoppedEvent(reason, breakEvent->SourceCpu);
	} else if(type == ConsoleNotificationType::EmulationStopped) {
		_server->SendTerminatedEvent();
	}
}
