#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

struct DbgData;

struct SourceLocation {
	std::string file;
	int line = 0;
	bool valid = false;
};

class SourceMapper {
public:
	bool LoadDbgFile(const std::string& path);

	// Address → source. cpuAddr is the address as computed from the .dbg
	// segments; prefer the ROM-offset overloads where an absolute address is
	// available, since they are immune to bank mirroring (FastROM/HiROM).
	SourceLocation GetSourceLocation(uint32_t cpuAddr) const;
	SourceLocation GetSourceLocationByRomOffset(uint32_t romOffset) const;

	// Source → address
	uint32_t GetAddress(const std::string& file, int line) const;

	// Source → ROM file offset. This is what the .dbg actually records
	// (segment ooffs + span start), so it matches whichever bank mirror the
	// CPU happens to execute from.
	uint32_t GetRomOffset(const std::string& file, int line) const;

	// Symbol lookup
	std::string GetSymbolName(uint32_t cpuAddr) const;

	// Symbol lookup by ROM file offset. Prefer this wherever an absolute
	// address is available: the CPU address of a symbol depends on which bank
	// mirror the code is executed through (FastROM, HiROM), the ROM offset
	// does not.
	std::string GetSymbolNameByRomOffset(uint32_t romOffset) const;

	bool IsLoaded() const { return _loaded; }

	// Get all source files
	const std::vector<std::string>& GetSourceFiles() const { return _fileNames; }

private:
	bool _loaded = false;

	// Sorted by address for binary search
	struct AddrSpan {
		uint32_t addr;       // CPU address (segment.start + span.start)
		uint32_t romOffset;  // ROM file offset (segment.ooffs + span.start)
		uint32_t size;
		int fileId;
		int line;
	};
	std::vector<AddrSpan> _addrSpans;     // sorted by addr
	std::vector<AddrSpan> _romOffsetSpans; // same spans, sorted by romOffset

	// File+line → address
	struct FileLineKey {
		int fileId;
		int line;
		bool operator==(const FileLineKey& o) const { return fileId == o.fileId && line == o.line; }
	};
	struct FileLineHash {
		size_t operator()(const FileLineKey& k) const {
			return std::hash<int>()(k.fileId) ^ (std::hash<int>()(k.line) << 16);
		}
	};
	std::unordered_map<FileLineKey, uint32_t, FileLineHash> _fileLineToAddr;
	std::unordered_map<FileLineKey, uint32_t, FileLineHash> _fileLineToRomOffset;

	// Shared by the two file+line lookups
	uint32_t LookupFileLine(const std::unordered_map<FileLineKey, uint32_t, FileLineHash>& table,
		const std::string& file, int line) const;

	// Symbol lookup
	struct SymEntry {
		uint32_t addr;       // CPU address as recorded in the .dbg
		uint32_t romOffset;  // ROM file offset, or NoRomOffset when not in ROM
		uint32_t size;
		std::string name;
	};
	static constexpr uint32_t NoRomOffset = 0xFFFFFFFF;
	std::vector<SymEntry> _symbols;     // sorted by addr
	std::vector<SymEntry> _romSymbols;  // ROM symbols only, sorted by romOffset

	// Shared by the two symbol lookups; byRom selects which key to search on
	static std::string LookupSymbol(const std::vector<SymEntry>& syms,
		uint32_t key, bool byRom);

	// File paths
	std::vector<std::string> _fileNames;

	// File name → id mapping (for resolving source breakpoints)
	std::unordered_map<std::string, int> _fileNameToId;
};
