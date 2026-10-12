#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <cstdio>
#include "Shared/CpuType.h"
#include "Shared/SettingTypes.h"
#include "cli_platform.h"

class Emulator;
class Debugger;
class CliNotificationListener;
class CliInputProvider;
class CliSymbols;

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

	// JSON mode: every command answers with exactly one JSON line on _out
	// (the real stdout); fd 1 is pointed at stderr so emulator chatter and
	// the text-only commands cannot corrupt the stream.
	FILE* _out = nullptr;
	bool _maxSpeed = false;   // headless/turbo: run every command flat out
	std::shared_ptr<CliInputProvider> _input;   // the `input` command's controller state
	bool _replied = false;
	StdinLines _stdin;
	const CliSymbols* _symbols = nullptr;   // names for addresses, when a build left any
	int _lineNo = 0;          // stdin lines read, for an expect failure's report
	bool _scripted = false;   // stdin is not a terminal
	int _expectPassed = 0;
	int _expectFailed = 0;
	int _expectErrors = 0;    // an expect that could not be evaluated
	bool _commandFailed = false;   // the current command reported an error
	std::string _lastError;        // ... and its message
	std::string _scriptPath;       // --script: stop at the first failed command
	bool _scriptStopped = false;
	int _runTimeoutMs = 0;         // run/rwatch deadline; 0 = none (interactive)

	void PrintState();
	void PrintDisassemblyAtPC(int lines = 1);

	std::string StateJson();
	void Reply(const std::string& json);
	void ReplyOk(const std::string& fields = "");
	void ReplyError(const std::string& message);
	void ReplyState(const char* stopped, const std::string& fields = "");
	void Usage(const char* text);
	bool Fail(const std::string& message);
	std::string StopReason();
	FILE* Results() { return _out ? _out : stdout; }   // where expect results go
	bool ReadLine(std::string& line);
	bool WaitOrPause(int timeoutMs);
	uint32_t ProgramCounter();
	void EnsureSpeed();
	bool WaitUntilStopped(int timeoutMs);

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
	void CmdTrace(const std::string& filename, bool withRegs = false);
	void CmdDump(const std::string& type, const std::string& filename);
	void CmdScreenshot(const std::string& filename);
	void CmdRunUntil(const RegCondition& cond);
	void CmdUntil(uint32_t addr, int maxFrames);
	void CmdJump(const std::string& target);
	void CmdWrite(uint32_t addr, const std::vector<uint8_t>& bytes);
	void CmdSaveState(const std::string& filename);
	void CmdLoadState(const std::string& filename);
	void CmdMovie(const std::string& filename);
	void CmdInput(int port, const std::string& buttons);
	void CmdInfoSymbols(const std::string& text);
	void CmdExpect(const std::vector<std::string>& tokens, const std::string& line);
	void CmdHelp();

	uint16_t GetRegisterValue(const std::string& name, const uint8_t* stateBuffer);
	bool ParseRegCondition(const std::string& expr, RegCondition& cond);
	bool EvalRegCondition(const RegCondition& cond, const uint8_t* stateBuffer);
	void SyncBreakpoints(int64_t extraAddr = -1);
	std::vector<std::string> Tokenize(const std::string& line);
	uint32_t ParseAddress(const std::string& str, uint32_t* size = nullptr);
	bool IsRegisterName(const std::string& name);
	uint32_t ParseExpectValue(const std::string& text);

public:
	DebuggerCli(Emulator* emu, std::shared_ptr<CliNotificationListener> listener,
	            CpuType primaryCpu, ConsoleType consoleType, bool jsonOutput);

	void AddInitialBreakpoint(uint32_t addr);
	// JSON or script mode: where replies or results go (the real stdout,
	// saved before anything else could write to it). Without one, Run()
	// redirects stdout itself.
	void SetOutput(FILE* out) { _out = out; }
	// SetScript - commands come from *path* (already on stdin): results only
	// on stdout, the first failed command stops it, and run/rwatch give up
	// after *timeoutMs*.
	void SetScript(const std::string& path, int timeoutMs) { _scriptPath = path; _runTimeoutMs = timeoutMs; }
	void SetMaximumSpeed(bool on) { _maxSpeed = on; }
	void SetInputProvider(std::shared_ptr<CliInputProvider> input) { _input = input; }
	void SetSymbols(const CliSymbols* symbols) { _symbols = symbols; }
	// Run - the REPL loop. Returns the exit status: 0, or 1 if an `expect`
	// failed, or 2 if one could not be evaluated or a script stopped.
	int Run();
};
