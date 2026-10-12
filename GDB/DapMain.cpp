#include <cstdio>
#include <csignal>
#include <atomic>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
#include <functional>
#include <cstring>
#include <cstdlib>

#include "Core/Shared/Emulator.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/SettingTypes.h"
#include "Core/Shared/NotificationManager.h"
#include "Core/Shared/DebuggerRequest.h"
#include "Core/Shared/MessageManager.h"
#include "Core/Shared/KeyManager.h"
#include "Core/Shared/CpuType.h"
#include "Core/Debugger/Debugger.h"
#include "Core/Debugger/Breakpoint.h"
#include "Core/Debugger/DebugTypes.h"
#include "Core/Debugger/DebugUtilities.h"
#include "Utilities/VirtualFile.h"
#include "Utilities/FolderUtilities.h"
#include "Core/Shared/Movies/MovieManager.h"

#include "cli_notification.h"
#include "cli_input.h"
#include "debugger_cli.h"
#include "cli_symbols.h"
#include "batch_runner.h"
#include "console_info.h"
#include "bus_logging.h"
#include "cli_platform.h"

#include "Core/Debugger/DAP/DapServer.h"
#include "Core/Debugger/DAP/DapNotificationListener.h"

#ifndef MESEN_HEADLESS
#ifdef __linux__
#include "Linux/LinuxKeyManager.h"
#endif
#include "Sdl/SdlRenderer.h"
#include "Sdl/SdlSoundManager.h"
#endif

static std::atomic<bool> g_running{true};
static std::shared_ptr<CliNotificationListener> g_listener;

// SIGINT interrupts the command in progress (a person at the prompt);
// SIGTERM ends the session, so a client can always stop the process.
static void signalHandler(int sig)
{
	g_running = false;
	auto listener = g_listener;
	if(listener) {
		if(sig == SIGTERM) listener->RequestQuit();
		else listener->Interrupt();
	}
}

// Frontend - the game window, sound and keyboard of a session that is not
// headless. A HEADLESS=true build has none of them, and no SDL.
class Frontend
{
public:
#ifdef MESEN_HEADLESS
	static constexpr bool Available = false;
	bool Start(Emulator*) { return false; }
	const char* Error() { return "built with HEADLESS=true"; }
	void OnWindowClosed(std::function<void()>) {}
	void Stop() {}
#else
	static constexpr bool Available = true;

	~Frontend() { Stop(); }

	// False when SDL cannot start (no display); Error() says why.
	bool Start(Emulator* emu)
	{
		// mesen-gdb has its own main(), not SDL's (SDL_MAIN_HANDLED on Windows).
		SDL_SetMainReady();
		if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) return false;
		_sdl = true;
		_renderer.reset(new SdlRenderer(emu, nullptr));
		_sound.reset(new SdlSoundManager(emu));
#ifdef __linux__
		_keys.reset(new LinuxKeyManager(emu));
		KeyManager::RegisterKeyManager(_keys.get());
#endif
		return true;
	}

	const char* Error() { return SDL_GetError(); }

	void OnWindowClosed(std::function<void()> callback)
	{
		if(_renderer) _renderer->SetQuitCallback(std::move(callback));
	}

	// After the emulator is released.
	void Stop()
	{
		_renderer.reset();
		_sound.reset();
#ifdef __linux__
		_keys.reset();
#endif
		if(_sdl) SDL_Quit();
		_sdl = false;
	}

private:
	bool _sdl = false;
	std::unique_ptr<SdlRenderer> _renderer;
	std::unique_ptr<SdlSoundManager> _sound;
#ifdef __linux__
	std::unique_ptr<LinuxKeyManager> _keys;
#endif
#endif
};

struct CliArgs {
	std::string romPath;
	bool dapMode = false;
	bool batchMode = false;
	bool jsonOutput = false;
	bool headless = false;
	int timeoutMs = 10000;
	std::vector<std::string> breakSpecs;      // addresses or symbols, resolved after loading symbols
	std::vector<uint32_t> breakAddresses;
	std::string scriptPath;   // --script: debugger commands from a file, as a test
	std::string symPath;   // --sym / --dbg; otherwise found beside the ROM
	std::string dbgPath;
	std::vector<BatchAssertion> assertions;
	std::vector<MemoryDump> dumps;
	std::string screenshotFile;
	std::string moviePath;
	bool logBus = false;
	bool logVram = false;
	bool logHdma = false;
	bool turbo = false;
	std::string ramInit;   // zeros / ones / random: power-on RAM, pinned for reproducible runs
};

static uint32_t ParseAddr(const std::string& s)
{
	std::string str = s;
	// bank:addr
	size_t colon = str.find(':');
	if(colon != std::string::npos) {
		uint32_t bank = std::stoul(str.substr(0, colon), nullptr, 16);
		uint32_t addr = std::stoul(str.substr(colon + 1), nullptr, 16);
		return (bank << 16) | (addr & 0xFFFF);
	}
	if(str.size() > 1 && str[0] == '$') str = str.substr(1);
	else if(str.size() > 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) str = str.substr(2);
	return std::stoul(str, nullptr, 16);
}

static void PrintUsage(const char* prog)
{
	fprintf(stderr,
		"Usage: %s [options] <rom_path>\n"
		"\n"
		"Modes:\n"
		"  <rom_path>              CLI interactive mode (default)\n"
		"  <rom_path> --batch      CLI batch mode\n"
		"  <rom_path> --script <f> Run a file of debugger commands as a test: exit 0 if\n"
		"                          every expect passed, 1 if one failed, 2 on an error\n"
		"  --dap                   DAP mode: speak DAP JSON on stdin/stdout\n"
		"                          (shows the game window; add --headless to suppress)\n"
		"\n"
		"Options:\n"
		"  --dap                   DAP mode (for VSCode integration)\n"
		"  --batch                 Batch mode (non-interactive)\n"
		"  --json                  JSON output: batch registers, or in the CLI one\n"
		"                          JSON reply line per command (see CLAUDE.md)\n"
		"  --headless              No SDL window or audio (max speed)\n"
		"  --break <addr>          Set initial breakpoint (hex or symbol, repeatable)\n"
		"  --timeout <ms>          Batch timeout, or a script's limit for run and rwatch\n"
		"                          (default 10000)\n"
		"  --check-reg <R>=<V>     Assert register (batch)\n"
		"  --check-mem <A>=<V>     Assert memory byte (batch)\n"
		"  --check-mem16 <A>=<V>   Assert memory word (batch)\n"
		"  --dump <type> <file>    Dump memory region (batch)\n"
		"  --screenshot <file>     Capture frame to PNG (batch)\n"
		"  --movie <file.mmo>      Play a Mesen movie file (.mmo)\n"
		"  --turbo                 Run at maximum speed (no frame limiter)\n"
		"  --ram-init <s>          Power-on RAM: zeros, ones or random (SNES)\n"
		"  --sym <file>            WLA-DX symbols (default: the ROM's name with .sym)\n"
		"  --dbg <file>            cc65 .dbg symbols (default: the ROM's name with\n"
		"                          .dbg, or main.dbg beside it)\n"
		"  --log-bus               Log SNES bus writes to stdout\n"
		"  --log-vram              Log SNES VRAM writes to stdout\n"
		"  --log-hdma              Log SNES HDMA transfers to stdout\n"
		"  --help                  Show this help\n"
		"\n"
		"Address formats: $1234, 0x1234, 1234, 00:8000, or a symbol (name, name+off)\n"
		"\n"
		"Examples:\n"
		"  %s game.nes                          Interactive NES debugger\n"
		"  %s game.sfc --batch --break $8100    Run SNES to address, print state\n"
		"  %s --dap                             Start DAP server for VSCode\n",
		prog, prog, prog, prog);
}

static bool ParseArgs(int argc, char* argv[], CliArgs& args)
{
	for(int i = 1; i < argc; i++) {
		std::string arg = argv[i];

		if(arg == "--help" || arg == "-h") {
			PrintUsage(argv[0]);
			return false;
		} else if(arg == "--dap") {
			args.dapMode = true;
		} else if(arg == "--batch") {
			args.batchMode = true;
		} else if(arg == "--json") {
			args.jsonOutput = true;
		} else if(arg == "--headless") {
			args.headless = true;
		} else if(arg == "--movie" && i + 1 < argc) {
			args.moviePath = argv[++i];
		} else if(arg == "--log-bus") {
			args.logBus = true;
		} else if(arg == "--log-vram") {
			args.logVram = true;
		} else if(arg == "--log-hdma") {
			args.logHdma = true;
		} else if(arg == "--break" && i + 1 < argc) {
			args.breakSpecs.push_back(argv[++i]);
		} else if(arg == "--timeout" && i + 1 < argc) {
			args.timeoutMs = std::stoi(argv[++i]);
		} else if(arg == "--check-reg" && i + 1 < argc) {
			std::string s = argv[++i];
			auto eq = s.find('=');
			if(eq == std::string::npos) {
				fprintf(stderr, "Invalid --check-reg format: %s (expected REG=VALUE)\n", s.c_str());
				return false;
			}
			BatchAssertion a;
			a.type = BatchAssertion::Type::Reg;
			a.name = s.substr(0, eq);
			a.expected = (uint16_t)ParseAddr(s.substr(eq + 1));
			a.size = 2;
			args.assertions.push_back(a);
		} else if((arg == "--check-mem" || arg == "--check-mem16") && i + 1 < argc) {
			std::string s = argv[++i];
			auto eq = s.find('=');
			if(eq == std::string::npos) {
				fprintf(stderr, "Invalid %s format: %s (expected ADDR=VALUE)\n", arg.c_str(), s.c_str());
				return false;
			}
			BatchAssertion a;
			a.type = BatchAssertion::Type::Mem;
			a.name = s.substr(0, eq);   // resolved after loading symbols
			a.expected = (uint16_t)ParseAddr(s.substr(eq + 1));
			a.size = (arg == "--check-mem16") ? (uint8_t)2 : (uint8_t)1;
			args.assertions.push_back(a);
		} else if(arg == "--dump" && i + 2 < argc) {
			// --dump <type> <file> — type is resolved after ROM load when console is known
			std::string type = argv[++i];
			std::string file = argv[++i];
			// Store type name as string; resolve to MemoryType after console detection
			// For now store -1 as a sentinel; the type name goes in filename temporarily
			args.dumps.push_back({-1, type + "\t" + file});
		} else if(arg == "--screenshot" && i + 1 < argc) {
			args.screenshotFile = argv[++i];
		} else if(arg == "--ram-init" && i + 1 < argc) {
			args.ramInit = argv[++i];
			if(args.ramInit != "zeros" && args.ramInit != "ones" && args.ramInit != "random") {
				fprintf(stderr, "Invalid --ram-init %s (zeros, ones or random)\n", args.ramInit.c_str());
				return false;
			}
		} else if(arg == "--script" && i + 1 < argc) {
			args.scriptPath = argv[++i];
		} else if(arg == "--sym" && i + 1 < argc) {
			args.symPath = argv[++i];
		} else if(arg == "--dbg" && i + 1 < argc) {
			args.dbgPath = argv[++i];
		} else if(arg == "--turbo") {
			args.turbo = true;
		} else if(arg[0] == '-') {
			fprintf(stderr, "Unknown option: %s\n", arg.c_str());
			PrintUsage(argv[0]);
			return false;
		} else {
			if(args.romPath.empty()) {
				args.romPath = arg;
			} else {
				fprintf(stderr, "Multiple ROM paths specified\n");
				return false;
			}
		}
	}

	return true;
}

// --- DAP mode ---
static int RunDapMode(CliArgs& args)
{
	// Redirect stdout → stderr before anything else runs, so emulator and SDL
	// chatter can't corrupt the DAP stream. The real stdout goes to DapServer.
	FILE* dapOutput = CliPlatform::TakeStdout();

	FolderUtilities::SetHomeFolder(CliPlatform::MesenHome());

	// Create and initialize emulator
	std::unique_ptr<Emulator> emu(new Emulator());
	emu->Initialize(false);
	KeyManager::SetSettings(emu->GetSettings());

	// Video/audio/input. --dap on its own shows the game window and plays
	// sound while debugging; pass --headless to run without one.
	Frontend frontend;
	if(!args.headless && !frontend.Start(emu.get())) {
		// No display available — keep the debug session usable rather than
		// failing the launch outright.
		fprintf(stderr, "[DAP] SDL_Init failed (%s), continuing without video/audio.\n", frontend.Error());
		args.headless = true;
	}

	if(args.headless) {
		// Max speed, but keep rendering every frame so state reads at a
		// breakpoint reflect the current PPU output.
		emu->GetSettings()->SetFlag(EmulationFlags::MaximumSpeed);
		emu->GetSettings()->GetSnesConfig().DisableFrameSkipping = true;
	}

	// Enable ALL debugger flags before ROM load — only the ones for the loaded
	// console will actually matter. This avoids needing to know console type upfront.
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::SnesDebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::SpcDebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::Sa1DebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::GsuDebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::NecDspDebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::Cx4DebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::GbDebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::NesDebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::PceDebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::SmsDebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::GbaDebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::WsDebuggerEnabled, true);
	emu->GetSettings()->SetFlag(EmulationFlags::ConsoleMode);
	emu->Pause();

	// DapServer creates and registers its own DapNotificationListener.
	// ROM loading is handled by the DAP launch request.
	DapServer server(emu.get(), dapOutput);

	// Closing the game window ends the debug session. VSCode replies to
	// the terminated event with a disconnect, which unblocks the reader.
	frontend.OnWindowClosed([&server]() {
		fprintf(stderr, "[DAP] Window closed, terminating debug session.\n");
		server.SendTerminatedEvent();
	});

	server.Run();

	fclose(dapOutput);

	// Cleanup
	emu->Stop(true);
	emu->Release();
	frontend.Stop();

	return 0;
}

// --- CLI/Batch mode ---
static int RunCliMode(CliArgs& args)
{
	// Symbols, so an address anywhere - these options, the CLI's commands -
	// can be a name.
	CliSymbols symbols;
	std::string symPath = args.symPath.empty() ? CliSymbols::FindSymFile(args.romPath) : args.symPath;
	std::string dbgPath = args.dbgPath.empty() ? CliSymbols::FindDbgFile(args.romPath) : args.dbgPath;
	if(!symbols.Load(symPath, dbgPath)) {
		fprintf(stderr, "Cannot read symbols: %s %s\n", symPath.c_str(), dbgPath.c_str());
		return 2;
	}
	if(symbols.Count() > 0) {
		std::string from = symPath;
		if(!dbgPath.empty()) from += (from.empty() ? "" : ", ") + dbgPath;
		fprintf(stderr, "[Symbols] %zu from %s\n", symbols.Count(), from.c_str());
	}
	try {
		for(auto& spec : args.breakSpecs) {
			args.breakAddresses.push_back(ParseCliAddress(spec, &symbols));
		}
		for(auto& a : args.assertions) {
			if(a.type == BatchAssertion::Type::Mem) a.address = ParseCliAddress(a.name, &symbols);
		}
	} catch(const std::exception& e) {
		fprintf(stderr, "Error: %s\n", e.what());
		return 2;
	}

	// A JSON or script session owns stdout for its replies: point fd 1 at stderr
	// before the emulator exists, so the ROM-load log cannot precede the
	// first reply.
	if(!args.scriptPath.empty()) {
		if(args.batchMode) {
			fprintf(stderr, "Error: --script and --batch are separate modes.\n");
			return 2;
		}
		// The script is the session's stdin; RAM is pinned unless asked
		// otherwise, so a test runs the same every time.
		if(!CliPlatform::StdinFromFile(args.scriptPath)) {
			fprintf(stderr, "Error: cannot read script %s\n", args.scriptPath.c_str());
			return 2;
		}
		if(args.ramInit.empty()) args.ramInit = "zeros";
	}

	FILE* jsonOut = nullptr;
	if((args.jsonOutput || !args.scriptPath.empty()) && !args.batchMode) {
		jsonOut = CliPlatform::TakeStdout();
	}

	// 0. Set home folder (Mesen needs this for save states, settings, etc.)
	FolderUtilities::SetHomeFolder(CliPlatform::MesenHome());

	// 1. Create and initialize emulator
	std::unique_ptr<Emulator> emu(new Emulator());
	emu->Initialize(false);  // no shortcut keys
	KeyManager::SetSettings(emu->GetSettings());

	// 2. Set up SDL if not headless
	Frontend frontend;
	if(!args.headless) {
		if(!frontend.Start(emu.get())) {
			fprintf(stderr, "SDL_Init failed: %s\n", frontend.Error());
			emu->Release();
			return 2;
		}
	} else {
		// Headless: max speed. Keep rendering every frame anyway so
		// breakpoint screenshots reflect the current PPU state instead of
		// whichever frame last beat the 10ms frame-skip timer.
		emu->GetSettings()->SetFlag(EmulationFlags::MaximumSpeed);
		emu->GetSettings()->GetSnesConfig().DisableFrameSkipping = true;
	}

	// 3. Register notification listener
	auto listener = std::make_shared<CliNotificationListener>();
	g_listener = listener;
	emu->GetNotificationManager()->RegisterNotificationListener(listener);

	frontend.OnWindowClosed([listener]() {
		fprintf(stderr, "Window closed, exiting.\n");
		listener->RequestQuit();
	});

	// 4. Enable ALL debugger flags before LoadRom — only the ones for the loaded
	//    console will actually matter. This avoids needing to know console type upfront.
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::SnesDebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::SpcDebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::Sa1DebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::GsuDebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::NecDspDebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::Cx4DebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::St018DebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::GbDebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::NesDebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::PceDebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::SmsDebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::GbaDebuggerEnabled, true);
	emu->GetSettings()->SetDebuggerFlag(DebuggerFlags::WsDebuggerEnabled, true);
	emu->GetSettings()->SetFlag(EmulationFlags::ConsoleMode);
	emu->Pause();

	// A debugger session drives input itself (the `input` command), and with
	// no settings file a headless emulator has nothing plugged in: give empty
	// SNES ports a standard controller. (A movie applies its own settings.)
	if(!args.batchMode) {
		SnesConfig& snes = emu->GetSettings()->GetSnesConfig();
		if(snes.Port1.Type == ControllerType::None) snes.Port1.Type = ControllerType::SnesController;
		if(snes.Port2.Type == ControllerType::None) snes.Port2.Type = ControllerType::SnesController;
	}

	if(!args.ramInit.empty()) {
		emu->GetSettings()->GetSnesConfig().RamPowerOnState =
			args.ramInit == "zeros" ? RamState::AllZeros :
			args.ramInit == "ones" ? RamState::AllOnes : RamState::Random;
	}

	// 5. Load ROM (debugger auto-created inside, breaks on first instruction)
	if(!emu->LoadRom((VirtualFile)args.romPath, VirtualFile())) {
		fprintf(stderr, "Failed to load ROM: %s\n", args.romPath.c_str());
		emu->Release();
		frontend.Stop();
		return 2;
	}

	// 6. Wait for the initial break (fired by the internal Step inside LoadRom)
	listener->WaitForBreak(5000);

	// 7. Enable bus logging flags
	BusLogging::logBus.store(args.logBus, std::memory_order_relaxed);
	BusLogging::logVram.store(args.logVram, std::memory_order_relaxed);
	BusLogging::logHdma.store(args.logHdma, std::memory_order_relaxed);

	// 8. Detect console type and primary CPU
	CpuType primaryCpu = emu->GetCpuTypes()[0];
	ConsoleType consoleType = emu->GetConsoleType();

	// 8. Resolve --dump type names now that we know the console
	for(auto& d : args.dumps) {
		if(d.memType == -1) {
			// Parse "type\tfile" stored during arg parsing
			size_t tab = d.filename.find('\t');
			std::string typeName = d.filename.substr(0, tab);
			std::string fileName = d.filename.substr(tab + 1);
			d.filename = fileName;

			auto regions = ConsoleInfo::GetMemoryRegions(consoleType);
			bool found = false;
			for(auto& r : regions) {
				if(typeName == r.shortName) {
					d.memType = (int)r.type;
					found = true;
					break;
				}
			}
			if(!found) {
				fprintf(stderr, "Unknown dump type '%s' for %s. Valid types:",
					typeName.c_str(), ConsoleInfo::GetConsoleName(consoleType));
				for(auto& r : regions) {
					fprintf(stderr, " %s", r.shortName);
				}
				fprintf(stderr, "\n");
			}
		}
	}

	// 9. Start movie playback if requested (before breakpoints, since PowerCycle resets debugger)

	if(!args.moviePath.empty()) {
		VirtualFile movieFile(args.moviePath);
		if(!movieFile.IsValid()) {
			fprintf(stderr, "Movie file not found: %s\n", args.moviePath.c_str());
		} else {
			fprintf(stderr, "[Movie] Loading: %s\n", args.moviePath.c_str());
			emu->GetMovieManager()->Play(movieFile, true);
			if(emu->GetMovieManager()->Playing()) {
				fprintf(stderr, "[Movie] Playback started\n");
			} else {
				fprintf(stderr, "[Movie] Failed to start playback\n");
			}
			// Wait for the power cycle's re-break before the breakpoints
			// below touch the debugger: until the emulation thread stops it
			// may be in Emulator::WaitForLock, spinning on a break request,
			// and SetBreakpoints raises one and spins until it stops - a
			// deadlock (see DebuggerCli::WaitUntilStopped).
			auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while(std::chrono::steady_clock::now() < deadline) {
				DebuggerRequest req = emu->GetDebugger(false);
				Debugger* dbg = req.GetDebugger();
				if(dbg && dbg->IsExecutionStopped()) break;
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
	}

	// 10. Set initial breakpoints (after movie start, since movie PowerCycle resets debugger)
	if(!args.breakAddresses.empty()) {
		MemoryType cpuMemType = ConsoleInfo::GetCpuMemoryType(primaryCpu);

		struct BreakpointData {
			uint32_t id;
			CpuType cpuType;
			uint8_t _pad1[3];
			MemoryType memoryType;
			BreakpointTypeFlags type;
			int32_t startAddr;
			int32_t endAddr;
			bool enabled;
			bool markEvent;
			bool ignoreDummyOperations;
			char _pad2;
			char condition[1000];
		};

		std::vector<Breakpoint> bps;
		uint32_t bpId = 1;
		for(auto addr : args.breakAddresses) {
			Breakpoint bp;
			static_assert(sizeof(BreakpointData) == sizeof(Breakpoint),
				"BreakpointData layout must match Breakpoint");
			BreakpointData* data = reinterpret_cast<BreakpointData*>(&bp);
			memset(data, 0, sizeof(BreakpointData));
			data->id = bpId++;
			data->cpuType = primaryCpu;
			data->memoryType = cpuMemType;
			data->type = BreakpointTypeFlags::Execute;
			data->startAddr = (int32_t)addr;
			data->endAddr = (int32_t)addr;
			data->enabled = true;
			bps.push_back(bp);
		}

		DebuggerRequest req = emu->GetDebugger(false);
		Debugger* dbg = req.GetDebugger();
		if(dbg) {
			dbg->SetBreakpoints(bps.data(), (uint32_t)bps.size());
		}
	}

	// Apply turbo mode if requested
	if(args.turbo) {
		emu->GetSettings()->SetFlag(EmulationFlags::MaximumSpeed);
	}

	// 11. Dispatch to batch or interactive mode. The CLI's input provider is
	// declared here so it outlives the teardown below: the emulator holds a
	// raw pointer to it until it stops.
	std::shared_ptr<CliInputProvider> input;
	int exitCode = 0;
	if(args.batchMode) {
		BatchRunner runner(emu.get(), listener, primaryCpu, consoleType,
		                   args.jsonOutput, args.timeoutMs);
		for(auto& a : args.assertions) {
			runner.AddAssertion(a);
		}
		for(auto& d : args.dumps) {
			if(d.memType >= 0) {
				runner.AddDump(d.memType, d.filename);
			}
		}
		if(!args.screenshotFile.empty()) {
			runner.SetScreenshotFile(args.screenshotFile);
		}
		exitCode = runner.Run();
	} else {
		DebuggerCli cli(emu.get(), listener, primaryCpu, consoleType, args.jsonOutput);
		if(jsonOut) cli.SetOutput(jsonOut);
		if(!args.scriptPath.empty()) cli.SetScript(args.scriptPath, args.timeoutMs);
		input = std::make_shared<CliInputProvider>(emu.get());
		input->Attach();
		emu->GetNotificationManager()->RegisterNotificationListener(input);
		cli.SetInputProvider(input);
		cli.SetMaximumSpeed(args.headless || args.turbo);
		for(auto addr : args.breakAddresses) {
			cli.AddInitialBreakpoint(addr);
		}
		cli.SetSymbols(&symbols);
		exitCode = cli.Run();
	}

	// 11. Teardown
	g_listener.reset();
	emu->Stop(false);
	emu->Release();
	frontend.Stop();

	return exitCode;
}

int main(int argc, char* argv[])
{
	CliArgs args;
	if(!ParseArgs(argc, argv, args)) {
		return 2;
	}

	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);
	CliPlatform::UseBinaryStdio();

	if(!args.headless && !Frontend::Available) {
		fprintf(stderr, "Built without a game window (HEADLESS=true): running headless.\n");
		args.headless = true;
	}

	MessageManager::SetOptions(false, true);

	// Mode detection:
	// 1. --dap flag → DAP mode
	// 2. Everything else → CLI mode (default, requires ROM)

	if(args.dapMode) {
		// In DAP mode, stdout is reserved for DAP JSON protocol.
		// Disable Mesen's diagnostic output to stdout.
		MessageManager::SetOptions(false, false);
		return RunDapMode(args);
	}

	if(args.romPath.empty()) {
		fprintf(stderr, "Error: ROM path is required.\n");
		PrintUsage(argv[0]);
		return 2;
	}
	return RunCliMode(args);
}
