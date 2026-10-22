#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

struct CliSymbol {
	std::string name;
	uint32_t addr = 0;    // 24-bit CPU-bus address (the value, for a constant)
	uint32_t size = 0;    // bytes, 0 when no source says
	bool isConst = false; // a .dbg `type=equ`: a value, not a place
};

// CliSymbols - names for the CLI's addresses, from the files a build leaves
// beside its ROM. The rules match python/mesen_gdb/rom_driver/symbols.py:
//
// * the WLA-DX `.sym` file is the truth for addresses (labels and
//   `.RAMSECTION` statics), with a size wherever it defines `_sizeof_<name>`;
// * a cc65-format `.dbg` adds what the assembler never lists (RAM a compiler
//   names with `.DEFINE`, constants) and sizes. A compiler may write it before
//   assembly, so a name both files know takes the `.sym` address.
//
// Names are exactly as the files spell them.
class CliSymbols
{
private:
	std::unordered_map<std::string, CliSymbol> _byName;

	void ParseSym(const std::string& text);

public:
	// Load - read either file or both (an empty path is skipped); returns
	// false if a named file cannot be read.
	bool Load(const std::string& symPath, const std::string& dbgPath);
	void LoadSymText(const std::string& text) { ParseSym(text); }

	size_t Count() const { return _byName.size(); }
	const CliSymbol* Find(const std::string& name) const;

	// Similar - up to *max* names containing *name*, for an error message.
	std::vector<std::string> Similar(const std::string& name, size_t max = 5) const;

	// Matching - every symbol whose name contains *text*, sorted by name.
	std::vector<const CliSymbol*> Matching(const std::string& text) const;

	// The newest `.sym`, and the newest `.dbg`, in *romPath*'s folder - the
	// DAP's rule. Empty when the folder has none.
	static std::string FindSymFile(const std::string& romPath);
	static std::string FindDbgFile(const std::string& romPath);
};

// ParseCliNumber - "$1F", "0x1F" (hex) or "31" (decimal); throws
// std::invalid_argument when the whole text is not one number.
uint32_t ParseCliNumber(const std::string& text);

// NoSymbolMessage - "no symbol 'x'", with names like it when there are any.
std::string NoSymbolMessage(const std::string& name, const CliSymbols* symbols);

// ParseCliAddress - an address: "$1234", "0x1234", "1234" (bare is hex),
// "00:8000" (bank:addr), or a symbol, optionally "+offset" (hex). A bare
// word that names a symbol is the symbol; one that does not but reads as hex
// is the number. *size*, when given, receives the symbol's size (0 for a
// number or an offset). Throws std::invalid_argument for an unknown name.
uint32_t ParseCliAddress(const std::string& text, const CliSymbols* symbols, uint32_t* size = nullptr);
