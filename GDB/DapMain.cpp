#include <cstdio>
#include <csignal>
#include <atomic>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
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

#include "cli_notification.h"
#include "debugger_cli.h"
#include "batch_runner.h"
#include "console_info.h"

#ifndef __APPLE__
#include "Linux/LinuxKeyManager.h"
#endif

#include "Sdl/SdlRenderer.h"
#include "Sdl/SdlSoundManager.h"

static std::atomic<bool> g_running{true};

static void signalHandler(int)
{
	g_running = false;
}

struct CliArgs {
	std::string romPath;
	bool dapMode = false;
	bool batchMode = false;
	bool jsonOutput = false;
	bool headless = false;
	int timeoutMs = 10000;
	std::vector<uint32_t> breakAddresses;
	std::vector<BatchAssertion> assertions;
	std::vector<MemoryDump> dumps;
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
		"  --dap                   DAP mode: speak DAP JSON on stdin/stdout\n"
		"\n"
		"Options:\n"
		"  --dap                   DAP mode (for VSCode integration)\n"
		"  --batch                 Batch mode (non-interactive)\n"
		"  --json                  JSON output (CLI/batch modes)\n"
		"  --headless              No SDL window (max speed)\n"
		"  --break <addr>          Set initial breakpoint (hex, repeatable)\n"
		"  --timeout <ms>          Batch timeout (default 10000)\n"
		"  --check-reg <R>=<V>     Assert register (batch)\n"
		"  --check-mem <A>=<V>     Assert memory byte (batch)\n"
		"  --check-mem16 <A>=<V>   Assert memory word (batch)\n"
		"  --dump <type> <file>    Dump memory region (batch)\n"
		"  --help                  Show this help\n"
		"\n"
		"Address formats: $1234, 0x1234, 1234, 00:8000\n"
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
		} else if(arg == "--break" && i + 1 < argc) {
			args.breakAddresses.push_back(ParseAddr(argv[++i]));
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
			a.address = ParseAddr(s.substr(0, eq));
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

// --- DAP mode stub ---
static int RunDapMode(CliArgs& args)
{
	// TODO: DAP server implementation (Phase 1 of plan-phase1.md)
	// For now, print a placeholder message to stderr
	fprintf(stderr, "[DAP] DAP mode not yet implemented. Waiting for Phase 1.\n");

	// If a ROM was given, set it up for when DAP launch request comes
	// For now just exit
	return 0;
}

// --- CLI/Batch mode ---
static int RunCliMode(CliArgs& args)
{
	// 0. Set home folder (Mesen needs this for save states, settings, etc.)
	{
		const char* home = getenv("MESEN_HOME");
		if(!home) home = getenv("HOME");
		std::string mesenHome = std::string(home ? home : "/tmp") + "/.mesen-dap";
		FolderUtilities::SetHomeFolder(mesenHome);
	}

	// 1. Create and initialize emulator
	std::unique_ptr<Emulator> emu(new Emulator());
	emu->Initialize(false);  // no shortcut keys
	KeyManager::SetSettings(emu->GetSettings());

	// 2. Set up SDL if not headless
	SdlRenderer* renderer = nullptr;
	SdlSoundManager* soundManager = nullptr;
#ifndef __APPLE__
	LinuxKeyManager* keyManager = nullptr;
#endif

	if(!args.headless) {
		if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
			fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
			emu->Release();
			return 2;
		}

		renderer = new SdlRenderer(emu.get(), nullptr);
		soundManager = new SdlSoundManager(emu.get());
#ifndef __APPLE__
		keyManager = new LinuxKeyManager(emu.get());
		KeyManager::RegisterKeyManager(keyManager);
#endif
	} else {
		// Headless: max speed
		emu->GetSettings()->SetFlag(EmulationFlags::MaximumSpeed);
	}

	// 3. Register notification listener
	auto listener = std::make_shared<CliNotificationListener>();
	emu->GetNotificationManager()->RegisterNotificationListener(listener);

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

	// 5. Load ROM (debugger auto-created inside, breaks on first instruction)
	if(!emu->LoadRom((VirtualFile)args.romPath, VirtualFile())) {
		fprintf(stderr, "Failed to load ROM: %s\n", args.romPath.c_str());
		emu->Release();
		delete renderer;
		delete soundManager;
#ifndef __APPLE__
		delete keyManager;
#endif
		if(!args.headless) SDL_Quit();
		return 2;
	}

	// 6. Wait for the initial break (fired by the internal Step inside LoadRom)
	listener->WaitForBreak(5000);

	// 7. Detect console type and primary CPU
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

	// 9. Set initial breakpoints (now we're in a known-stopped state)
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

	// 10. Dispatch to batch or interactive mode
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
		exitCode = runner.Run();
	} else {
		DebuggerCli cli(emu.get(), listener, primaryCpu, consoleType, args.jsonOutput);
		for(auto addr : args.breakAddresses) {
			cli.AddInitialBreakpoint(addr);
		}
		cli.Run();
	}

	// 11. Teardown
	emu->Stop(false);
	emu->Release();

	delete renderer;
	delete soundManager;
#ifndef __APPLE__
	delete keyManager;
#endif

	if(!args.headless) {
		SDL_Quit();
	}

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

	MessageManager::SetOptions(false, true);

	// Mode detection:
	// 1. --dap flag → DAP mode
	// 2. Everything else → CLI mode (default, requires ROM)

	if(args.dapMode) {
		return RunDapMode(args);
	}

	if(args.romPath.empty()) {
		fprintf(stderr, "Error: ROM path is required.\n");
		PrintUsage(argv[0]);
		return 2;
	}
	return RunCliMode(args);
}
