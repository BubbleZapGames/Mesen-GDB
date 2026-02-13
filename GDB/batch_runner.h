#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include "Shared/CpuType.h"
#include "Shared/SettingTypes.h"

class Emulator;
class CliNotificationListener;

struct BatchAssertion {
	enum class Type { Reg, Mem };
	Type type;
	std::string name;     // register name or hex address
	uint32_t address;     // parsed address (for mem type)
	uint16_t expected;
	uint8_t size;         // 1 or 2 bytes
};

struct MemoryDump {
	int memType;          // MemoryType enum cast to int
	std::string filename;
};

class BatchRunner {
private:
	Emulator* _emu;
	std::shared_ptr<CliNotificationListener> _listener;
	CpuType _primaryCpu;
	ConsoleType _consoleType;
	bool _jsonOutput;
	int _timeoutMs;
	std::vector<BatchAssertion> _assertions;
	std::vector<MemoryDump> _dumps;

	uint16_t GetRegisterValue(const std::string& name, const uint8_t* stateBuffer, bool& found);

public:
	BatchRunner(Emulator* emu, std::shared_ptr<CliNotificationListener> listener,
	            CpuType primaryCpu, ConsoleType consoleType,
	            bool jsonOutput, int timeoutMs);

	void AddAssertion(const BatchAssertion& assertion);
	void AddDump(int memType, const std::string& filename);
	int Run();  // returns exit code: 0 = pass, 1 = fail, 2 = error/timeout
};
