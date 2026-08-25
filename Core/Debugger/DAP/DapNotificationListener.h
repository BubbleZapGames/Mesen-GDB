#pragma once
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include "Shared/Interfaces/INotificationListener.h"

class DapServer;

class DapNotificationListener : public INotificationListener {
private:
	DapServer* _server;

	// Lets a step loop wait for the emulation thread to break, the same way
	// CliNotificationListener does for the interactive CLI.
	std::mutex _mutex;
	std::condition_variable _cv;
	bool _breakOccurred = false;
	bool _breakWasBreakpoint = false;

public:
	DapNotificationListener(DapServer* server);
	virtual ~DapNotificationListener() = default;

	void ProcessNotification(ConsoleNotificationType type, void* parameter) override;
	void Detach() { _server = nullptr; }

	void ResetBreak();
	/** True if a break arrived, false on timeout. */
	bool WaitForBreak(int timeoutMs);
	/** Whether the last break came from a breakpoint rather than a step. */
	bool LastBreakWasBreakpoint();
};
