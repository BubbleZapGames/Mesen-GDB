#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>

// A label: a CPU address, bank and offset as WLA prints them
struct WlaSymLabel {
	uint32_t addr = 0;     // (bank << 16) | offset
	std::string name;
};

// One entry of [addr-to-line mapping]: the code at an address starts a line
struct WlaSymLine {
	bool hasRomOffset = false;
	uint32_t romOffset = 0;  // v2 records the ROM offset itself
	uint32_t cpuAddr = 0;    // v1 records only (bank << 16) | offset
	uint32_t fileKey = 0;    // key into WlaSymData::files
	int line = 0;            // 1-based
};

struct WlaSymData {
	std::map<uint32_t, std::string> files;  // file key → path as recorded
	std::vector<WlaSymLabel> labels;
	std::vector<WlaSymLine> lines;
};

// Reads a WLA-DX symbol file: wlalink -S (with -A for line mapping), or the
// nocash format of -s, which is labels only with no section headers. Follows
// the reader rules in WLA-DX's doc/symbols.rst: text after ';' is a comment,
// "[name]" starts a section, and unknown sections are skipped.
class WlaSymParser {
public:
	static bool Parse(const std::string& path, WlaSymData& out);
};
