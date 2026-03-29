#pragma once
#include <cstdint>
#include "Shared/Interfaces/INotificationListener.h"

class DapServer;

class DapNotificationListener : public INotificationListener {
private:
	DapServer* _server;
	
public:
	DapNotificationListener(DapServer* server);
	
	void ProcessNotification(ConsoleNotificationType type, void* parameter) override;
};
