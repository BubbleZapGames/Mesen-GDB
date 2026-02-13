#pragma once
#include "pch.h"
#include <thread>
#include "Shared/SettingTypes.h"
#include "Shared/RenderedFrame.h"
#include "Shared/Interfaces/IRenderingDevice.h"
#include "Utilities/AutoResetEvent.h"
#include "Utilities/SimpleLock.h"
#include "Utilities/safe_ptr.h"

class IRenderingDevice;
class Emulator;
class SystemHud;
class DebugHud;
class InputHud;

class VideoRenderer
{
private:
	Emulator* _emu;

	AutoResetEvent _waitForRender;
	unique_ptr<std::thread> _renderThread;
	IRenderingDevice* _renderer = nullptr;
	atomic<bool> _stopFlag;
	SimpleLock _stopStartLock;

	uint32_t _rendererWidth = 512;
	uint32_t _rendererHeight = 480;

	unique_ptr<DebugHud> _rendererHud;
	unique_ptr<SystemHud> _systemHud;
	unique_ptr<InputHud> _inputHud;
	SimpleLock _hudLock;

	RenderSurfaceInfo _emuHudSurface = {};
	RenderSurfaceInfo _scriptHudSurface = {};
	bool _needScriptHudClear = false;
	uint32_t _scriptHudScale = 2;
	uint32_t _lastScriptHudFrameNumber = 0;
	bool _needRedraw = true;

	RenderedFrame _lastFrame;
	SimpleLock _frameLock;

	void RenderThread();
	bool DrawScriptHud(RenderedFrame& frame);
	
	FrameInfo GetEmuHudSize(FrameInfo baseFrameSize);

public:
	VideoRenderer(Emulator* emu);
	~VideoRenderer();

	FrameInfo GetRendererSize();
	void SetRendererSize(uint32_t width, uint32_t height);
	
	void SetScriptHudScale(uint32_t scale) { _scriptHudScale = scale; }
	std::pair<FrameInfo, OverscanDimensions> GetScriptHudSize();

	void StartThread();
	void StopThread();

	void UpdateFrame(RenderedFrame& frame);
	void ClearFrame();
	void RegisterRenderingDevice(IRenderingDevice *renderer);
	void UnregisterRenderingDevice(IRenderingDevice *renderer);

};