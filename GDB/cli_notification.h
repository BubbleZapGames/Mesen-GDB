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
	bool _interrupted = false;
	bool _quitRequested = false;

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

	// Signal-safe: wake up any thread blocked in WaitForBreak
	void Interrupt() {
		std::lock_guard<std::mutex> lock(_mutex);
		_interrupted = true;
		_cv.notify_all();
	}

	// Returns true if break occurred, false if timed out or interrupted
	bool WaitForBreak(int timeoutMs) {
		std::unique_lock<std::mutex> lock(_mutex);
		if(_breakOccurred) {
			_breakOccurred = false;
			return true;
		}
		auto pred = [this]{ return _breakOccurred || _interrupted; };
		if(timeoutMs > 0) {
			_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), pred);
		} else {
			_cv.wait(lock, pred);
		}
		if(_interrupted) {
			_interrupted = false;
			return false;
		}
		bool result = _breakOccurred;
		_breakOccurred = false;
		return result;
	}

	void RequestQuit() {
		std::lock_guard<std::mutex> lock(_mutex);
		_quitRequested = true;
		_interrupted = true;
		_cv.notify_all();
	}

	bool IsQuitRequested() {
		std::lock_guard<std::mutex> lock(_mutex);
		return _quitRequested;
	}

	bool IsInterrupted() {
		std::lock_guard<std::mutex> lock(_mutex);
		return _interrupted;
	}

	bool IsStopped() {
		std::lock_guard<std::mutex> lock(_mutex);
		return _stopped;
	}

	void Reset() {
		std::lock_guard<std::mutex> lock(_mutex);
		_breakOccurred = false;
		_interrupted = false;
	}
};
