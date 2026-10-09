#include "cli_symbols.h"
#include "Core/Debugger/DAP/DbgFileParser.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

static bool FileExists(const std::string& path)
{
	std::ifstream f(path);
	return f.is_open();
}

// WithExtension - *path* with its extension (if any) replaced by *ext*.
static std::string WithExtension(const std::string& path, const char* ext)
{
	size_t slash = path.find_last_of("/\\");
	size_t dot = path.find_last_of('.');
	if(dot == std::string::npos || (slash != std::string::npos && dot < slash)) return path + ext;
	return path.substr(0, dot) + ext;
}

std::string CliSymbols::FindSymFile(const std::string& romPath)
{
	std::string sym = WithExtension(romPath, ".sym");
	return FileExists(sym) ? sym : "";
}

std::string CliSymbols::FindDbgFile(const std::string& romPath)
{
	std::string dbg = WithExtension(romPath, ".dbg");
	if(FileExists(dbg)) return dbg;
	size_t slash = romPath.find_last_of("/\\");
	std::string main = (slash == std::string::npos ? "" : romPath.substr(0, slash + 1)) + "main.dbg";
	return FileExists(main) ? main : "";
}

bool CliSymbols::Load(const std::string& symPath, const std::string& dbgPath)
{
	bool ok = true;
	if(!dbgPath.empty()) {
		DbgData data;
		if(DbgFileParser::Parse(dbgPath, data)) {
			for(const DbgSymbol& s : data.symbols) {
				if(s.name.empty()) continue;
				CliSymbol sym;
				sym.name = s.name;
				sym.addr = s.val;
				sym.size = s.size;
				sym.isConst = s.type == "equ";
				// Data not placed yet reads 0 here; the .sym has its address.
				if(!sym.isConst && sym.addr == 0 && sym.size) continue;
				_byName[sym.name] = sym;
			}
		} else {
			ok = false;
		}
	}
	if(!symPath.empty()) {
		std::ifstream f(symPath);
		if(f.is_open()) {
			std::stringstream ss;
			ss << f.rdbuf();
			ParseSym(ss.str());
		} else {
			ok = false;
		}
	}
	return ok;
}

void CliSymbols::ParseSym(const std::string& text)
{
	std::unordered_map<std::string, uint32_t> labels;
	std::unordered_map<std::string, uint32_t> sizes;
	std::string section;
	std::istringstream in(text);
	std::string line;
	while(std::getline(in, line)) {
		size_t start = line.find_first_not_of(" \t\r");
		if(start == std::string::npos || line[start] == ';') continue;
		line = line.substr(start, line.find_last_not_of(" \t\r") + 1 - start);
		if(line[0] == '[') { section = line; continue; }

		std::istringstream fields(line);
		std::string value, name, extra;
		if(!(fields >> value >> name) || (fields >> extra)) continue;
		try {
			if(section == "[labels]" || section == "[ramsections]") {
				// bb:aaaa name
				if(value.size() != 7 || value[2] != ':') continue;
				size_t used = 0;
				uint32_t bank = std::stoul(value.substr(0, 2), &used, 16);
				if(used != 2) continue;
				uint32_t addr = std::stoul(value.substr(3), &used, 16);
				if(used != 4) continue;
				labels[name] = (bank << 16) | addr;
			} else if(section == "[definitions]") {
				// 0000000a _sizeof_name
				static const std::string prefix = "_sizeof_";
				if(value.size() != 8 || name.compare(0, prefix.size(), prefix) != 0) continue;
				size_t used = 0;
				uint32_t size = std::stoul(value, &used, 16);
				if(used != 8) continue;
				sizes[name.substr(prefix.size())] = size;
			}
		} catch(const std::exception&) {
			continue;
		}
	}

	for(auto& [name, addr] : labels) {
		CliSymbol sym;
		sym.name = name;
		sym.addr = addr;
		auto size = sizes.find(name);
		auto prior = _byName.find(name);
		if(size != sizes.end()) sym.size = size->second;
		else if(prior != _byName.end()) sym.size = prior->second.size;
		_byName[name] = sym;
	}
}

const CliSymbol* CliSymbols::Find(const std::string& name) const
{
	auto it = _byName.find(name);
	return it == _byName.end() ? nullptr : &it->second;
}

std::vector<std::string> CliSymbols::Similar(const std::string& name, size_t max) const
{
	std::vector<std::string> out;
	for(const CliSymbol* s : Matching(name)) {
		if(out.size() == max) break;
		out.push_back(s->name);
	}
	return out;
}

std::vector<const CliSymbol*> CliSymbols::Matching(const std::string& text) const
{
	std::vector<const CliSymbol*> out;
	for(auto& [name, sym] : _byName) {
		if(name.find(text) != std::string::npos) out.push_back(&sym);
	}
	std::sort(out.begin(), out.end(), [](const CliSymbol* a, const CliSymbol* b) { return a->name < b->name; });
	return out;
}

// ParseWhole - all of *text* as a number in *base*, or throw.
static uint32_t ParseWhole(const std::string& text, int base, const std::string& original)
{
	size_t used = 0;
	unsigned long value = 0;
	bool ok = !text.empty() && isxdigit((unsigned char)text[0]);
	if(ok) {
		try { value = std::stoul(text, &used, base); } catch(const std::exception&) { ok = false; }
	}
	if(!ok || used != text.size() || value > 0xFFFFFFFFul) {
		throw std::invalid_argument("bad number '" + original + "'");
	}
	return (uint32_t)value;
}

std::string NoSymbolMessage(const std::string& name, const CliSymbols* symbols)
{
	std::string message = "no symbol '" + name + "'";
	if(!symbols || symbols->Count() == 0) return message + " (no symbols loaded)";
	auto near = symbols->Similar(name);
	if(!near.empty()) {
		message += " (similar:";
		for(size_t i = 0; i < near.size(); i++) message += (i ? ", " : " ") + near[i];
		message += ")";
	}
	return message;
}

static bool IsHexPrefixed(const std::string& s)
{
	return s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
}

uint32_t ParseCliNumber(const std::string& text)
{
	if(text.size() > 1 && text[0] == '$') return ParseWhole(text.substr(1), 16, text);
	if(IsHexPrefixed(text)) return ParseWhole(text.substr(2), 16, text);
	return ParseWhole(text, 10, text);
}

// ParseHexAddress - "$1234", "0x1234", "1234" or "00:8000", all hex.
static uint32_t ParseHexAddress(const std::string& text)
{
	size_t colon = text.find(':');
	if(colon != std::string::npos) {
		uint32_t bank = ParseWhole(text.substr(0, colon), 16, text);
		uint32_t addr = ParseWhole(text.substr(colon + 1), 16, text);
		return (bank << 16) | (addr & 0xFFFF);
	}
	if(text.size() > 1 && text[0] == '$') return ParseWhole(text.substr(1), 16, text);
	if(IsHexPrefixed(text)) return ParseWhole(text.substr(2), 16, text);
	return ParseWhole(text, 16, text);
}

uint32_t ParseCliAddress(const std::string& text, const CliSymbols* symbols, uint32_t* size)
{
	if(size) *size = 0;
	if(text.empty()) throw std::invalid_argument("missing address");
	bool word = isalpha((unsigned char)text[0]) || text[0] == '_' || text[0] == '.';
	if(!word) return ParseHexAddress(text);

	size_t plus = text.find('+');
	std::string name = text.substr(0, plus);
	const CliSymbol* sym = symbols ? symbols->Find(name) : nullptr;
	if(!sym) {
		// A bare word that names nothing may still be a hex address ("beef").
		bool hex = plus == std::string::npos &&
			std::all_of(name.begin(), name.end(), [](char c) { return isxdigit((unsigned char)c); });
		if(hex) return ParseWhole(name, 16, text);
		throw std::invalid_argument(NoSymbolMessage(name, symbols));
	}
	if(plus == std::string::npos) {
		if(size) *size = sym->size;
		return sym->addr;
	}
	return sym->addr + ParseHexAddress(text.substr(plus + 1));
}
