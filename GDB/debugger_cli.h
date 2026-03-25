#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "Shared/CpuType.h"
#include "Shared/SettingTypes.h"

class Emulator;
class Debugger;
class CliNotificationListener;

struct CliBreakpoint {
	uint32_t id;
	uint32_t address;
	bool isWatch;     // true = write watchpoint, false = execute breakpoint
	bool enabled;
};

enum class RegCondOp { Eq, Ne, Lt, Gt, Le, Ge };

struct RegCondition {
	std::string regName;
	RegCondOp op;
	uint16_t value;
};

class DebuggerCli {
private:
	Emulator* _emu;
	std::shared_ptr<CliNotificationListener> _listener;
	CpuType _primaryCpu;
	ConsoleType _consoleType;
	std::vector<CliBreakpoint> _breakpoints;
	uint32_t _nextBreakpointId = 1;
	bool _jsonOutput;
	bool _quit = false;
	std::string _lastCommand;

	void PrintState();
	void PrintDisassemblyAtPC(int lines = 1);

	// Command handlers
	void CmdStep(int count);
	void CmdNext();
	void CmdFinish();
	void CmdRun();
	void CmdBreak(uint32_t addr);
	void CmdWatch(uint32_t addr);
	void CmdDelete(uint32_t id);
	void CmdInfoBreak();
	void CmdInfoRegions();
	void CmdInfoCpu();
	void CmdRegs();
	void CmdMem(uint32_t addr, uint32_t len);
	void CmdMemTyped(uint32_t addr, uint32_t len, int memType, const char* label);
	void CmdSet(uint32_t addr, uint8_t val);
	void CmdDisasm(uint32_t addr, int count);
	void CmdBacktrace();
	void CmdFrames(int count);
	void CmdReset();
	void CmdTrace(const std::string& filename);
	void CmdDump(const std::string& type, const std::string& filename);
	void CmdScreenshot(const std::string& filename);
	void CmdRunUntil(const RegCondition& cond);
	void CmdHelp();

	uint16_t GetRegisterValue(const std::string& name, const uint8_t* stateBuffer);
	bool ParseRegCondition(const std::string& expr, RegCondition& cond);
	bool EvalRegCondition(const RegCondition& cond, const uint8_t* stateBuffer);
	void SyncBreakpoints();
	std::vector<std::string> Tokenize(const std::string& line);
	uint32_t ParseAddress(const std::string& str);

public:
	DebuggerCli(Emulator* emu, std::shared_ptr<CliNotificationListener> listener,
	            CpuType primaryCpu, ConsoleType consoleType, bool jsonOutput);

	void AddInitialBreakpoint(uint32_t addr);
	void Run();  // enter REPL loop
};
