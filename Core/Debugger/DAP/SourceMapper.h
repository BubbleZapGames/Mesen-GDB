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

	// Source → address. A line without code resolves to the nearest line
	// within 5 that has some; resolvedLine (if given) receives the line used.
	uint32_t GetAddress(const std::string& file, int line, int* resolvedLine = nullptr) const;

	// Source → ROM file offset. This is what the .dbg actually records
	// (segment ooffs + span start), so it matches whichever bank mirror the
	// CPU happens to execute from.
	uint32_t GetRomOffset(const std::string& file, int line, int* resolvedLine = nullptr) const;

	// The client knows a source file by this path (it set breakpoints there).
	// Locations in the matching .dbg file are reported with this path from
	// now on, so the client opens the file it already has.
	void NoteClientPath(const std::string& path);

	// Symbol lookup
	std::string GetSymbolName(uint32_t cpuAddr) const;

	// Symbol lookup by ROM file offset. Prefer this wherever an absolute
	// address is available: the CPU address of a symbol depends on which bank
	// mirror the code is executed through (FastROM, HiROM), the ROM offset
	// does not.
	std::string GetSymbolNameByRomOffset(uint32_t romOffset) const;

	bool IsLoaded() const { return _loaded; }

	// Get all source files, as reported to the client
	const std::vector<std::string>& GetSourceFiles() const { return _displayNames; }

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
		const std::string& file, int line, int* resolvedLine) const;

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

	// File paths as recorded in the .dbg
	std::vector<std::string> _fileNames;
	// The path each file is reported as: the client's path if it named the
	// file, else the recorded path if it exists, else the file of that name
	// beside the .dbg (a build moved or copied elsewhere), else as recorded.
	std::vector<std::string> _displayNames;

	// The .dbg file id a client path refers to, or -1
	int FindFileId(const std::string& path) const;
	const std::string& DisplayName(int fileId) const;

	// File name → id mapping (for resolving source breakpoints)
	std::unordered_map<std::string, int> _fileNameToId;
};
