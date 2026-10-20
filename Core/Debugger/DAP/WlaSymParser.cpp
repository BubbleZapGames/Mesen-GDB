#include "WlaSymParser.h"
#include <fstream>
#include <sstream>
#include <cstdlib>

static bool IsHex(const std::string& s)
{
	if(s.empty()) return false;
	for(char c : s) {
		if(!isxdigit((unsigned char)c)) return false;
	}
	return true;
}

static uint32_t Hex(const std::string& s)
{
	return (uint32_t)strtoul(s.c_str(), nullptr, 16);
}

// "hh:hhhh" (or "hhhh:hhhh") → (bank << 16) | offset
static bool ParseBankAddr(const std::string& s, uint32_t& out)
{
	size_t colon = s.find(':');
	if(colon == std::string::npos) return false;
	std::string bank = s.substr(0, colon);
	std::string addr = s.substr(colon + 1);
	if(!IsHex(bank) || !IsHex(addr) || addr.size() != 4) return false;
	out = (Hex(bank) << 16) | Hex(addr);
	return true;
}

// "a:b" or "a:b:c", each part hex
static bool ParseHexFields(const std::string& s, std::vector<uint32_t>& out)
{
	out.clear();
	std::stringstream ss(s);
	std::string part;
	while(std::getline(ss, part, ':')) {
		if(!IsHex(part)) return false;
		out.push_back(Hex(part));
	}
	return true;
}

static std::string Trim(const std::string& s)
{
	size_t start = s.find_first_not_of(" \t\r\n");
	if(start == std::string::npos) return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

// A source file path, written bare or quoted
static std::string UnquotePath(const std::string& s)
{
	std::string t = Trim(s);
	if(t.size() >= 2 && t[0] == '"') {
		size_t close = t.find('"', 1);
		if(close != std::string::npos) return t.substr(1, close - 1);
	}
	return t;
}

bool WlaSymParser::Parse(const std::string& path, WlaSymData& out)
{
	std::ifstream file(path);
	if(!file.is_open()) {
		return false;
	}

	// The nocash format has no sections: every line is a label
	std::string section = "labels";
	std::string raw;
	while(std::getline(file, raw)) {
		// A path may hold a ';' only inside quotes, so cut comments elsewhere
		std::string line;
		bool quoted = false;
		for(char c : raw) {
			if(c == '"') quoted = !quoted;
			if(c == ';' && !quoted) break;
			line += c;
		}
		line = Trim(line);
		if(line.empty()) continue;

		if(line.front() == '[' && line.back() == ']') {
			section = line.substr(1, line.size() - 2);
			continue;
		}

		std::stringstream ss(line);
		std::string first, second;
		ss >> first >> second;

		if(section == "labels") {
			// hh:hhhh name
			WlaSymLabel label;
			if(!second.empty() && ParseBankAddr(first, label.addr)) {
				label.name = second;
				out.labels.push_back(label);
			}
		} else if(section == "source files" || section == "source files v2") {
			// v1: ffff cccccccc path
			// v2: oooo:ffff cccccccc path
			std::vector<uint32_t> key;
			if(!ParseHexFields(first, key) || key.empty() || key.size() > 2 || !IsHex(second)) continue;
			size_t at = line.find(second, first.size()) + second.size();
			std::string name = UnquotePath(line.substr(at));
			if(name.empty()) continue;
			out.files[key.size() == 2 ? (key[0] << 16) | key[1] : key[0]] = name;
		} else if(section == "addr-to-line mapping") {
			// hh:hhhh ffff:llllllll
			WlaSymLine entry;
			std::vector<uint32_t> fl;
			if(!ParseBankAddr(first, entry.cpuAddr) || !ParseHexFields(second, fl) || fl.size() != 2) continue;
			entry.fileKey = fl[0];
			entry.line = (int)fl[1];
			out.lines.push_back(entry);
		} else if(section == "addr-to-line mapping v2") {
			// rrrrrrrr bb:oooo mmmm oooo:ffff:llllllll — ROM offset, ROM bank
			// and offset in it, memory address, object:file:line
			std::string memAddr, where;
			ss >> memAddr >> where;
			WlaSymLine entry;
			std::vector<uint32_t> ofl;
			uint32_t bankAddr;
			if(!IsHex(first) || !ParseBankAddr(second, bankAddr) || !IsHex(memAddr)
				|| !ParseHexFields(where, ofl) || ofl.size() != 3) {
				continue;
			}
			entry.hasRomOffset = true;
			entry.romOffset = Hex(first);
			entry.fileKey = (ofl[0] << 16) | ofl[1];
			entry.line = (int)ofl[2];
			out.lines.push_back(entry);
		}
		// [information], [definitions], [breakpoints], [symbols],
		// [rom checksum], [sections], [ramsections]: not needed for debugging
	}

	return true;
}
