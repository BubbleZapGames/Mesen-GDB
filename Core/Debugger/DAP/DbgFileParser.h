#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct DbgSegment {
	int id = -1;
	std::string name;
	uint32_t start = 0;   // CPU base address
	uint32_t size = 0;
	uint32_t ooffs = 0;   // ROM file offset
};

struct DbgFile {
	int id = -1;
	std::string name;     // source file path
};

struct DbgSpan {
	int id = -1;
	int segId = -1;
	uint32_t start = 0;   // offset within segment
	uint32_t size = 0;
};

struct DbgLine {
	int id = -1;
	int fileId = -1;
	int line = 0;          // 1-based source line number
	std::vector<int> spanIds;
};

struct DbgSymbol {
	int id = -1;
	std::string name;
	uint32_t val = 0;      // absolute address
	int segId = -1;
	int scopeId = -1;
	std::string type;       // "lab", "equ", etc.
	uint32_t size = 0;
};

struct DbgScope {
	int id = -1;
	std::string name;
	int parentId = -1;
	int symId = -1;
	uint32_t size = 0;
};

struct DbgData {
	std::vector<DbgSegment> segments;
	std::vector<DbgFile> files;
	std::vector<DbgSpan> spans;
	std::vector<DbgLine> lines;
	std::vector<DbgSymbol> symbols;
	std::vector<DbgScope> scopes;
};

class DbgFileParser {
public:
	static bool Parse(const std::string& path, DbgData& out);

private:
	static void ParseKeyValue(const std::string& data,
		void (*callback)(const std::string& key, const std::string& value, void* ctx), void* ctx);
};
