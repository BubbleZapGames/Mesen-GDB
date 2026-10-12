#include "SdlKeyManager.h"
#include "Core/Shared/KeyDefinitions.h"
#include "Core/Shared/MessageManager.h"

// Pad button names, by index in a "PadN <name>" code. Triggers and stick
// directions count as pressed past half way.
static const char* PadButtonNames[] = {
	"A", "B", "X", "Y", "L1", "R1", "Start", "Select",
	"Up", "Down", "Left", "Right", "L2", "R2", "L3", "R3",
	"X+", "X-", "Y+", "Y-", "X2+", "X2-", "Y2+", "Y2-"
};

// SDL's controller button for each of the first 16 names; -1 = an axis.
static const int PadButtonSdl[16] = {
	SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
	SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
	SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK,
	SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
	SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
	-1, -1, SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK
};

// Each scancode's key, by its name in KeyDefinitions.h.
static const std::pair<SDL_Scancode, const char*> ScancodeNames[] = {
	{ SDL_SCANCODE_A, "A" }, { SDL_SCANCODE_B, "B" }, { SDL_SCANCODE_C, "C" }, { SDL_SCANCODE_D, "D" },
	{ SDL_SCANCODE_E, "E" }, { SDL_SCANCODE_F, "F" }, { SDL_SCANCODE_G, "G" }, { SDL_SCANCODE_H, "H" },
	{ SDL_SCANCODE_I, "I" }, { SDL_SCANCODE_J, "J" }, { SDL_SCANCODE_K, "K" }, { SDL_SCANCODE_L, "L" },
	{ SDL_SCANCODE_M, "M" }, { SDL_SCANCODE_N, "N" }, { SDL_SCANCODE_O, "O" }, { SDL_SCANCODE_P, "P" },
	{ SDL_SCANCODE_Q, "Q" }, { SDL_SCANCODE_R, "R" }, { SDL_SCANCODE_S, "S" }, { SDL_SCANCODE_T, "T" },
	{ SDL_SCANCODE_U, "U" }, { SDL_SCANCODE_V, "V" }, { SDL_SCANCODE_W, "W" }, { SDL_SCANCODE_X, "X" },
	{ SDL_SCANCODE_Y, "Y" }, { SDL_SCANCODE_Z, "Z" },
	{ SDL_SCANCODE_1, "1" }, { SDL_SCANCODE_2, "2" }, { SDL_SCANCODE_3, "3" }, { SDL_SCANCODE_4, "4" },
	{ SDL_SCANCODE_5, "5" }, { SDL_SCANCODE_6, "6" }, { SDL_SCANCODE_7, "7" }, { SDL_SCANCODE_8, "8" },
	{ SDL_SCANCODE_9, "9" }, { SDL_SCANCODE_0, "0" },
	{ SDL_SCANCODE_RETURN, "Enter" }, { SDL_SCANCODE_ESCAPE, "Esc" }, { SDL_SCANCODE_BACKSPACE, "Backspace" },
	{ SDL_SCANCODE_TAB, "Tab" }, { SDL_SCANCODE_SPACE, "Space" },
	{ SDL_SCANCODE_MINUS, "-" }, { SDL_SCANCODE_EQUALS, "=" }, { SDL_SCANCODE_LEFTBRACKET, "[" },
	{ SDL_SCANCODE_RIGHTBRACKET, "]" }, { SDL_SCANCODE_BACKSLASH, "\\" }, { SDL_SCANCODE_SEMICOLON, ";" },
	{ SDL_SCANCODE_APOSTROPHE, "'" }, { SDL_SCANCODE_GRAVE, "`" }, { SDL_SCANCODE_COMMA, "," },
	{ SDL_SCANCODE_PERIOD, "." }, { SDL_SCANCODE_SLASH, "/" }, { SDL_SCANCODE_CAPSLOCK, "Caps Lock" },
	{ SDL_SCANCODE_F1, "F1" }, { SDL_SCANCODE_F2, "F2" }, { SDL_SCANCODE_F3, "F3" }, { SDL_SCANCODE_F4, "F4" },
	{ SDL_SCANCODE_F5, "F5" }, { SDL_SCANCODE_F6, "F6" }, { SDL_SCANCODE_F7, "F7" }, { SDL_SCANCODE_F8, "F8" },
	{ SDL_SCANCODE_F9, "F9" }, { SDL_SCANCODE_F10, "F10" }, { SDL_SCANCODE_F11, "F11" }, { SDL_SCANCODE_F12, "F12" },
	{ SDL_SCANCODE_PRINTSCREEN, "Print Screen" }, { SDL_SCANCODE_SCROLLLOCK, "Scroll Lock" },
	{ SDL_SCANCODE_PAUSE, "Pause" }, { SDL_SCANCODE_INSERT, "Insert" }, { SDL_SCANCODE_HOME, "Home" },
	{ SDL_SCANCODE_PAGEUP, "Page Up" }, { SDL_SCANCODE_DELETE, "Delete" }, { SDL_SCANCODE_END, "End" },
	{ SDL_SCANCODE_PAGEDOWN, "Page Down" },
	{ SDL_SCANCODE_RIGHT, "Right Arrow" }, { SDL_SCANCODE_LEFT, "Left Arrow" },
	{ SDL_SCANCODE_DOWN, "Down Arrow" }, { SDL_SCANCODE_UP, "Up Arrow" },
	{ SDL_SCANCODE_NUMLOCKCLEAR, "Num Lock" }, { SDL_SCANCODE_KP_DIVIDE, "Numpad /" },
	{ SDL_SCANCODE_KP_MULTIPLY, "Numpad *" }, { SDL_SCANCODE_KP_MINUS, "Numpad -" },
	{ SDL_SCANCODE_KP_PLUS, "Numpad +" }, { SDL_SCANCODE_KP_ENTER, "Enter" },
	{ SDL_SCANCODE_KP_1, "Numpad 1" }, { SDL_SCANCODE_KP_2, "Numpad 2" }, { SDL_SCANCODE_KP_3, "Numpad 3" },
	{ SDL_SCANCODE_KP_4, "Numpad 4" }, { SDL_SCANCODE_KP_5, "Numpad 5" }, { SDL_SCANCODE_KP_6, "Numpad 6" },
	{ SDL_SCANCODE_KP_7, "Numpad 7" }, { SDL_SCANCODE_KP_8, "Numpad 8" }, { SDL_SCANCODE_KP_9, "Numpad 9" },
	{ SDL_SCANCODE_KP_0, "Numpad 0" }, { SDL_SCANCODE_KP_PERIOD, "Numpad ." },
	{ SDL_SCANCODE_APPLICATION, "Apps" },
	{ SDL_SCANCODE_LCTRL, "Left Ctrl" }, { SDL_SCANCODE_LSHIFT, "Left Shift" }, { SDL_SCANCODE_LALT, "Left Alt" },
	{ SDL_SCANCODE_LGUI, "Left Win" }, { SDL_SCANCODE_RCTRL, "Right Ctrl" }, { SDL_SCANCODE_RSHIFT, "Right Shift" },
	{ SDL_SCANCODE_RALT, "Right Alt" }, { SDL_SCANCODE_RGUI, "Right Win" },
};

SdlKeyManager::SdlKeyManager()
{
	for(KeyDefinition& def : KeyDefinition::GetSharedKeyDefinitions()) {
		if(def.keyCode < KeyCount && !def.name.empty() && !_keyNames.count((uint16_t)def.keyCode)) {
			_keyNames[(uint16_t)def.keyCode] = def.name;
			_keyCodes[def.name] = (uint16_t)def.keyCode;
		}
	}
	for(auto& [scancode, name] : ScancodeNames) {
		auto code = _keyCodes.find(name);
		if(code != _keyCodes.end()) _scancodeKeys[scancode] = code->second;
	}
	for(int pad = 0; pad < MaxPads; pad++) {
		for(int button = 0; button < PadButtonCount; button++) {
			uint16_t code = (uint16_t)(BaseGamepadIndex + pad * 0x100 + button);
			std::string name = "Pad" + std::to_string(pad + 1) + " " + PadButtonNames[button];
			_keyNames[code] = name;
			_keyCodes[name] = code;
		}
	}

	// Gamepads already connected: SDL announced them when it started, before
	// this existed to hear it.
	for(int i = 0; i < SDL_NumJoysticks(); i++) {
		OpenPad(i);
	}
}

SdlKeyManager::~SdlKeyManager()
{
	for(Pad& pad : _pads) ClosePad(pad);
}

void SdlKeyManager::HandleEvent(const SDL_Event& evt)
{
	switch(evt.type) {
		case SDL_KEYDOWN:
		case SDL_KEYUP: {
			uint16_t code = _scancodeKeys[evt.key.keysym.scancode];
			if(code) _keys[code] = evt.type == SDL_KEYDOWN;
			break;
		}

		case SDL_WINDOWEVENT:
			// Keys released while the window was in the background never
			// arrive: forget them, rather than hold them down.
			if(evt.window.event == SDL_WINDOWEVENT_FOCUS_LOST) ResetKeyState();
			break;

		case SDL_CONTROLLERDEVICEADDED:
			OpenPad(evt.cdevice.which);
			break;

		case SDL_CONTROLLERDEVICEREMOVED:
			if(Pad* pad = FindPad(evt.cdevice.which)) ClosePad(*pad);
			break;

		case SDL_CONTROLLERBUTTONDOWN:
		case SDL_CONTROLLERBUTTONUP:
			if(Pad* pad = FindPad(evt.cbutton.which)) {
				if(evt.cbutton.button < SDL_CONTROLLER_BUTTON_MAX) {
					pad->Buttons[evt.cbutton.button] = evt.type == SDL_CONTROLLERBUTTONDOWN;
				}
			}
			break;

		case SDL_CONTROLLERAXISMOTION:
			if(Pad* pad = FindPad(evt.caxis.which)) {
				if(evt.caxis.axis < SDL_CONTROLLER_AXIS_MAX) pad->Axes[evt.caxis.axis] = evt.caxis.value;
			}
			break;
	}
}

void SdlKeyManager::OpenPad(int deviceIndex)
{
	if(!SDL_IsGameController(deviceIndex)) return;
	SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(deviceIndex);
	if(FindPad(id)) return;   // already open (announced and enumerated both)
	for(int i = 0; i < MaxPads; i++) {
		Pad& pad = _pads[i];
		if(pad.Controller) continue;
		pad.Controller = SDL_GameControllerOpen(deviceIndex);
		if(!pad.Controller) return;
		pad.Id = id;
		pad.Connected = true;
		const char* name = SDL_GameControllerName(pad.Controller);
		MessageManager::Log("[Input Connected] Pad" + std::to_string(i + 1) + ": " + (name ? name : "?"));
		return;
	}
}

void SdlKeyManager::ClosePad(Pad& pad)
{
	if(!pad.Controller) return;
	pad.Connected = false;
	SDL_GameControllerClose(pad.Controller);
	pad.Controller = nullptr;
	pad.Id = -1;
	for(auto& button : pad.Buttons) button = false;
	for(auto& axis : pad.Axes) axis = 0;
}

SdlKeyManager::Pad* SdlKeyManager::FindPad(SDL_JoystickID id)
{
	for(Pad& pad : _pads) {
		if(pad.Controller && pad.Id == id) return &pad;
	}
	return nullptr;
}

bool SdlKeyManager::IsPadButtonPressed(const Pad& pad, int button)
{
	constexpr int half = 16384;
	if(button < 16 && PadButtonSdl[button] >= 0) return pad.Buttons[PadButtonSdl[button]];
	switch(button) {
		case 12: return pad.Axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT] > half;
		case 13: return pad.Axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] > half;
		case 16: return pad.Axes[SDL_CONTROLLER_AXIS_LEFTX] > half;
		case 17: return pad.Axes[SDL_CONTROLLER_AXIS_LEFTX] < -half;
		case 18: return pad.Axes[SDL_CONTROLLER_AXIS_LEFTY] > half;    // down
		case 19: return pad.Axes[SDL_CONTROLLER_AXIS_LEFTY] < -half;
		case 20: return pad.Axes[SDL_CONTROLLER_AXIS_RIGHTX] > half;
		case 21: return pad.Axes[SDL_CONTROLLER_AXIS_RIGHTX] < -half;
		case 22: return pad.Axes[SDL_CONTROLLER_AXIS_RIGHTY] > half;
		case 23: return pad.Axes[SDL_CONTROLLER_AXIS_RIGHTY] < -half;
	}
	return false;
}

bool SdlKeyManager::IsKeyPressed(uint16_t keyCode)
{
	if(_disabled || keyCode == 0) return false;
	if(keyCode < KeyCount) return _keys[keyCode];
	if(keyCode >= BaseGamepadIndex) {
		int pad = (keyCode - BaseGamepadIndex) / 0x100;
		int button = (keyCode - BaseGamepadIndex) % 0x100;
		if(pad < MaxPads && button < PadButtonCount && _pads[pad].Connected) {
			return IsPadButtonPressed(_pads[pad], button);
		}
	}
	return false;
}

std::vector<uint16_t> SdlKeyManager::GetPressedKeys()
{
	std::vector<uint16_t> keys;
	for(int code = 1; code < KeyCount; code++) {
		if(_keys[code]) keys.push_back((uint16_t)code);
	}
	for(int pad = 0; pad < MaxPads; pad++) {
		if(!_pads[pad].Connected) continue;
		for(int button = 0; button < PadButtonCount; button++) {
			if(IsPadButtonPressed(_pads[pad], button)) keys.push_back((uint16_t)(BaseGamepadIndex + pad * 0x100 + button));
		}
	}
	return keys;
}

std::string SdlKeyManager::GetKeyName(uint16_t keyCode)
{
	auto name = _keyNames.find(keyCode);
	return name != _keyNames.end() ? name->second : "";
}

uint16_t SdlKeyManager::GetKeyCode(std::string keyName)
{
	auto code = _keyCodes.find(keyName);
	return code != _keyCodes.end() ? code->second : 0;
}

bool SdlKeyManager::SetKeyState(uint16_t scanCode, bool state)
{
	if(scanCode >= KeyCount || _keys[scanCode] == state) return false;
	_keys[scanCode] = state;
	return true;
}

void SdlKeyManager::ResetKeyState()
{
	for(auto& key : _keys) key = false;
}
