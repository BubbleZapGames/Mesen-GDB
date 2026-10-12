#pragma once
#include "SDL.h"
#include "Core/Shared/Interfaces/IKeyManager.h"
#include <array>
#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

// SdlKeyManager - the keyboard and gamepads, on every platform, from SDL.
// HandleEvent runs on the main thread, which pumps SDL's events; the
// emulator reads the state from its own thread. Keys use Mesen's shared key
// codes (KeyDefinitions.h), by physical position (SDL scancodes), so the
// default layout is the same on any keyboard layout. Gamepad buttons are
// "PadN <button>" codes from BaseGamepadIndex up, N in connection order.
class SdlKeyManager : public IKeyManager
{
public:
	static constexpr int MaxPads = 8;

	SdlKeyManager();
	~SdlKeyManager();

	void HandleEvent(const SDL_Event& evt);

	void RefreshState() override {}
	void UpdateDevices() override {}
	bool IsMouseButtonPressed(MouseButton button) override { return false; }
	bool IsKeyPressed(uint16_t keyCode) override;
	std::vector<uint16_t> GetPressedKeys() override;
	std::string GetKeyName(uint16_t keyCode) override;
	uint16_t GetKeyCode(std::string keyName) override;

	bool SetKeyState(uint16_t scanCode, bool state) override;
	void ResetKeyState() override;
	void SetDisabled(bool disabled) override { _disabled = disabled; }

private:
	static constexpr int KeyCount = 0x200;   // below BaseMouseButtonIndex
	static constexpr int PadButtonCount = 24;  // see PadButtonNames

	struct Pad
	{
		SDL_GameController* Controller = nullptr;   // main thread only
		SDL_JoystickID Id = -1;                      // main thread only
		std::atomic<bool> Connected{false};
		std::array<std::atomic<bool>, SDL_CONTROLLER_BUTTON_MAX> Buttons{};
		std::array<std::atomic<int16_t>, SDL_CONTROLLER_AXIS_MAX> Axes{};
	};

	std::array<std::atomic<bool>, KeyCount> _keys{};
	std::array<Pad, MaxPads> _pads;
	std::atomic<bool> _disabled{false};
	uint16_t _scancodeKeys[SDL_NUM_SCANCODES] = {};
	std::unordered_map<uint16_t, std::string> _keyNames;
	std::unordered_map<std::string, uint16_t> _keyCodes;

	void OpenPad(int deviceIndex);
	void ClosePad(Pad& pad);
	Pad* FindPad(SDL_JoystickID id);
	bool IsPadButtonPressed(const Pad& pad, int button);
};
