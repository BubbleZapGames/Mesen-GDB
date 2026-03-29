#pragma once
#include <cstdint>
#include "Shared/Interfaces/INotificationListener.h"

class DapServer;

class DapNotificationListener : public INotificationListener {
private:
	DapServer* _server;
	
public:
	DapNotificationListener(DapServer* server);
	virtual ~DapNotificationListener() = default;

	void ProcessNotification(ConsoleNotificationType type, void* parameter) override;
	void Detach() { _server = nullptr; }
};
