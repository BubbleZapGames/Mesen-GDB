#include "DapNotificationListener.h"
#include "DapServer.h"
#include "DapTypes.h"
#include "Core/Debugger/DebugTypes.h"
#include "Core/Shared/CpuType.h"

DapNotificationListener::DapNotificationListener(DapServer* server) : _server(server) {}

void DapNotificationListener::ProcessNotification(ConsoleNotificationType type, void* parameter)
{
	if(!_server || !_server->IsConfigDone()) {
		// Suppress all events before configurationDone (e.g., the initial
		// break from LoadRom fires before VSCode is ready).
		return;
	}

	if(type == ConsoleNotificationType::CodeBreak) {
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
