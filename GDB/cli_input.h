#pragma once
#include <array>
#include <mutex>
#include <string>
#include "Shared/Emulator.h"
#include "Shared/BaseControlDevice.h"
#include "Shared/SettingTypes.h"
#include "Shared/Interfaces/IInputProvider.h"
#include "Shared/Interfaces/INotificationListener.h"

// CliInputProvider - controller state set by the CLI's `input` command and
// held until changed: live input for a program driving the debugger, where
// otherwise a movie is the only way a headless session gets any.
//
// It drives only the ports `input` has named; any other port (or a movie,
// which registers after it) is left alone. A power cycle builds a new
// console and so a new control manager, which loses its providers, so it
// re-registers on GameLoaded as the rewind manager does.
class CliInputProvider : public IInputProvider, public INotificationListener
{
private:
	Emulator* _emu;
	std::mutex _lock;
	std::array<std::string, 8> _state;   // per port: text state, empty = not driven

public:
	explicit CliInputProvider(Emulator* emu) : _emu(emu) {}

	void Attach() { _emu->RegisterInputProvider(this); }

	// SetPort - hold *text* (the device's key-name string, '.' for released)
	// on *port* (0 = the first controller) from the next poll on.
	void SetPort(uint8_t port, const std::string& text)
	{
		std::lock_guard<std::mutex> guard(_lock);
		if(port < _state.size()) _state[port] = text;
	}

	bool SetInput(BaseControlDevice* device) override
	{
		if(device->GetControllerType() != ControllerType::SnesController) return false;
		uint8_t port = device->GetPort();
		std::string text;
		{
			std::lock_guard<std::mutex> guard(_lock);
			if(port >= _state.size() || _state[port].empty()) return false;
			text = _state[port];
		}
		device->SetTextState(text);
		return true;
	}

	void ProcessNotification(ConsoleNotificationType type, void* parameter) override
	{
		if(type == ConsoleNotificationType::GameLoaded) {
			_emu->RegisterInputProvider(this);
		}
	}
};
