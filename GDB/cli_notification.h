#pragma once
#include <mutex>
#include <condition_variable>
#include <memory>
#include "Shared/Interfaces/INotificationListener.h"

class CliNotificationListener : public INotificationListener,
                                 public std::enable_shared_from_this<CliNotificationListener> {
private:
	std::mutex _mutex;
	std::condition_variable _cv;
	bool _breakOccurred = false;
	bool _stopped = false;

public:
	virtual ~CliNotificationListener() = default;

	void ProcessNotification(ConsoleNotificationType type, void* parameter) override {
		if(type == ConsoleNotificationType::CodeBreak) {
			std::lock_guard<std::mutex> lock(_mutex);
			_breakOccurred = true;
			_cv.notify_all();
		} else if(type == ConsoleNotificationType::EmulationStopped) {
			std::lock_guard<std::mutex> lock(_mutex);
			_stopped = true;
			_breakOccurred = true;
			_cv.notify_all();
		}
	}

	// Returns true if break occurred, false if timed out
	bool WaitForBreak(int timeoutMs) {
		std::unique_lock<std::mutex> lock(_mutex);
		if(_breakOccurred) {
			_breakOccurred = false;
			return true;
		}
		if(timeoutMs > 0) {
			_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this]{ return _breakOccurred; });
		} else {
			_cv.wait(lock, [this]{ return _breakOccurred; });
		}
		bool result = _breakOccurred;
		_breakOccurred = false;
		return result;
	}

	bool IsStopped() {
		std::lock_guard<std::mutex> lock(_mutex);
		return _stopped;
	}

	void Reset() {
		std::lock_guard<std::mutex> lock(_mutex);
		_breakOccurred = false;
	}
};
