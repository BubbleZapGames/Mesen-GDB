#include "DapNotificationListener.h"
#include "DapServer.h"
#include "DapTypes.h"

DapNotificationListener::DapNotificationListener(DapServer* server) : _server(server) {}

void DapNotificationListener::ProcessNotification(ConsoleNotificationType type, void* parameter) {
	if(type == ConsoleNotificationType::CodeBreak) {
		// Process break notification
		// For Phase 1, just notify that we stopped
	}
}
