#pragma once
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <cstdint>

struct DbgData;

struct SourceLocation {
	std::string file;
	int line = 0;
	bool valid = false;
	bool cLine = false;    // a line of C source, not of assembly
	bool fileFound = false; // the file is on disk (or the client named it)
};

class SourceMapper {
public:
	static constexpr uint32_t NoRomOffset = 0xFFFFFFFF;

	// The ROM is an iNES image. headerSize bytes (the header, plus a trainer)
	// precede PRG-ROM in the file: a .dbg counts them in its segment offsets
	// and Mesen's PRG-ROM offsets don't. NES CPU addresses are flat 16-bit
	// ones, as the .dbg segments record them. Call before loading.
	void SetNesRom(uint32_t headerSize);

	// A cc65/ld65 .dbg file (also what R65 writes)
	bool LoadDbgFile(const std::string& path);

	// A WLA-DX symbol file (wlalink -S, or the nocash -s format): labels, and
	// address-to-line mapping when linked with -A. Labels are recorded as CPU
	// addresses only; toRomOffset turns one into a ROM offset, or NoRomOffset
	// when it isn't in ROM.
	bool LoadSymFile(const std::string& path, const std::function<uint32_t(uint32_t cpuAddr)>& toRomOffset);

	// Address → source. cpuAddr is the address as computed from the .dbg
	// segments; prefer the ROM-offset overloads where an absolute address is
	// available, since they are immune to bank mirroring (FastROM/HiROM).
	// Where a C line and the generated assembly both cover the address, the
	// C line wins.
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

	// The function an address is in: the innermost sized scope covering it,
	// else a label at the address or a sized label covering it.
	std::string GetSymbolName(uint32_t cpuAddr) const;

	// Symbol lookup by ROM file offset. Prefer this wherever an absolute
	// address is available: the CPU address of a symbol depends on which bank
	// mirror the code is executed through (FastROM, HiROM), the ROM offset
	// does not.
	std::string GetSymbolNameByRomOffset(uint32_t romOffset) const;

	// A label at exactly this ROM offset, or empty
	std::string GetLabelAtRomOffset(uint32_t romOffset) const;

	bool IsLoaded() const { return _loaded; }

	// Get all source files, as reported to the client
	const std::vector<std::string>& GetSourceFiles() const { return _displayNames; }

private:
	bool _loaded = false;
	bool _nes = false;
	uint32_t _romHeaderSize = 0;

	// Sorted by address for binary search
	struct AddrSpan {
		uint32_t addr;       // CPU address (segment.start + span.start)
		uint32_t romOffset;  // ROM file offset (segment.ooffs + span.start)
		uint32_t size;
		int fileId;
		int line;
		bool cLine;          // a line of C source, not of assembly
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

	// Shared by the two address → source lookups; byRom selects the key
	SourceLocation LookupSpan(const std::vector<AddrSpan>& spans, uint32_t key, bool byRom) const;

	// Symbol lookup
	struct SymEntry {
		uint32_t addr;       // CPU address as recorded in the .dbg
		uint32_t romOffset;  // ROM file offset, or NoRomOffset when not in ROM
		uint32_t size;
		std::string name;
	};
	std::vector<SymEntry> _symbols;     // sorted by addr
	std::vector<SymEntry> _romSymbols;  // ROM symbols only, sorted by romOffset
	std::vector<SymEntry> _scopes;      // sized function scopes

	// Shared by the two symbol lookups; byRom selects which key to search on
	static std::string LookupSymbol(const std::vector<SymEntry>& syms,
		uint32_t key, bool byRom);
	std::string LookupScope(uint32_t key, bool byRom) const;

	// What a loader hands to Build, in source and ROM terms
	struct RawSpan {
		uint32_t addr = 0;
		bool hasAddr = true;   // a .sym v2 mapping records no CPU address
		uint32_t romOffset = NoRomOffset;
		uint32_t size = 0;
		int fileId = -1;
		int line = 0;
		bool cLine = false;
	};
	void Build(const std::string& symbolPath, const std::vector<std::string>& files,
		const std::vector<RawSpan>& spans, std::vector<SymEntry> labels, std::vector<SymEntry> scopes);

	// File paths as recorded in the symbol file
	std::vector<std::string> _fileNames;
	// The path each file is reported as: the client's path if it named the
	// file, else where the recorded path resolves (see ResolveSourcePath in
	// SourceMapper.cpp), else as recorded.
	std::vector<std::string> _displayNames;
	// Whether each display name is a file the client can open: a library
	// built elsewhere (cc65's runtime) records paths that aren't on this disk
	std::vector<bool> _fileFound;

	// The file id a client path refers to, or -1
	int FindFileId(const std::string& path) const;
	const std::string& DisplayName(int fileId) const;

	// File name → id mapping (for resolving source breakpoints)
	std::unordered_map<std::string, int> _fileNameToId;
};
