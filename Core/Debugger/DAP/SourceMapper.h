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

	// Address → source
	SourceLocation GetSourceLocation(uint32_t cpuAddr) const;

	// Source → address
	uint32_t GetAddress(const std::string& file, int line) const;

	// Symbol lookup
	std::string GetSymbolName(uint32_t cpuAddr) const;

	bool IsLoaded() const { return _loaded; }

	// Get all source files
	const std::vector<std::string>& GetSourceFiles() const { return _fileNames; }

private:
	bool _loaded = false;

	// Sorted by address for binary search
	struct AddrSpan {
		uint32_t addr;     // CPU address (segment.start + span.start)
		uint32_t size;
		int fileId;
		int line;
	};
	std::vector<AddrSpan> _addrSpans;  // sorted by addr

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

	// Symbol lookup: sorted by address
	struct SymEntry {
		uint32_t addr;
		uint32_t size;
		std::string name;
	};
	std::vector<SymEntry> _symbols;  // sorted by addr

	// File paths
	std::vector<std::string> _fileNames;

	// File name → id mapping (for resolving source breakpoints)
	std::unordered_map<std::string, int> _fileNameToId;
};
