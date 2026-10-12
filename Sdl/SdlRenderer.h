#pragma once
#include "SDL.h"
#include "Core/Shared/Interfaces/IRenderingDevice.h"
#include "Utilities/SimpleLock.h"
#include "Core/Shared/Video/VideoRenderer.h"
#include "Core/Shared/RenderedFrame.h"
#include <functional>
#include <vector>

class Emulator;

struct HudRenderInfo
{
	SDL_Texture* Texture = nullptr;
	uint32_t Width = 0;
	uint32_t Height = 0;
};

// A HUD surface as the core's render thread handed it over, for Present().
struct HudCopy
{
	std::vector<uint32_t> Buffer;
	uint32_t Width = 0;
	uint32_t Height = 0;
	bool Dirty = true;   // changed since Present() last uploaded it
};

// SdlRenderer - the game window. The core calls UpdateFrame/ClearFrame (its
// emulation thread) and Render (its render thread), which only copy what to
// draw; every SDL call is in Present(), on the main thread, which macOS
// requires and which keeps one window for the session on every platform.
class SdlRenderer : public IRenderingDevice
{
private:
	Emulator* _emu;

	void* _windowHandle;
	bool _ownsWindow = false;
	SDL_Window* _sdlWindow = nullptr;
	SDL_Renderer *_sdlRenderer = nullptr;
	SDL_Texture* _sdlTexture = nullptr;

	HudRenderInfo _emuHud = {};
	HudRenderInfo _scriptHud = {};
	
	bool _useBilinearInterpolation = false;

	static SimpleLock _frameLock;
	uint32_t* _frameBuffer = nullptr;

	const uint32_t _bytesPerPixel = 4;
	uint32_t _screenBufferSize = 0;

	bool _frameChanged = true;

	uint32_t _screenWidth = 0;
	uint32_t _screenHeight = 0;

	uint32_t _requiredHeight = 0;
	uint32_t _requiredWidth = 0;

	uint32_t _frameHeight = 0;
	uint32_t _frameWidth = 0;
	uint32_t _newFrameBufferSize = 0;

	bool _vsyncEnabled = false;

	SimpleLock _hudLock;   // the copies below and _presentPending
	HudCopy _emuHudCopy;
	HudCopy _scriptHudCopy;
	bool _presentPending = false;
	std::function<void()> _onFrameReady;

	bool Init();
	bool InitTexture();
	void Cleanup();
	void LogSdlError(const char* msg);
	void SetScreenSize(uint32_t width, uint32_t height);
	
	bool UpdateHudSize(HudRenderInfo& hud, uint32_t width, uint32_t height);
	void UpdateHudTexture(HudRenderInfo& hud, uint32_t* src);
	static void CopyHud(HudCopy& dst, RenderSurfaceInfo& src);

public:
	SdlRenderer(Emulator* emu, void* windowHandle);
	virtual ~SdlRenderer();

	void ClearFrame() override;
	void UpdateFrame(RenderedFrame& frame) override;
	void Render(RenderSurfaceInfo& emuHud, RenderSurfaceInfo& scriptHud) override;
	void Reset() override;

	void SetExclusiveFullscreenMode(bool fullscreen, void* windowHandle) override;

	// Main thread: draw what Render last handed over, if anything; creates
	// the window on first use.
	void Present();
	// Called by Render, on the core's render thread, when there is something
	// for Present() to draw.
	void SetFrameReadyCallback(std::function<void()> cb) { _onFrameReady = std::move(cb); }
};
