#include "SourceMapper.h"
#include "DbgFileParser.h"
#include <algorithm>
#include <cstdio>

bool SourceMapper::LoadDbgFile(const std::string& path)
{
	DbgData data;
	if(!DbgFileParser::Parse(path, data)) {
		return false;
	}

	// Build file name list and reverse lookup
	_fileNames.clear();
	_fileNameToId.clear();
	for(auto& f : data.files) {
		if(f.id < 0) continue;
		if(f.id >= (int)_fileNames.size()) _fileNames.resize(f.id + 1);
		_fileNames[f.id] = f.name;
		_fileNameToId[f.name] = f.id;

		// Also index by basename for matching source breakpoints
		size_t slash = f.name.find_last_of("/\\");
		if(slash != std::string::npos) {
			_fileNameToId[f.name.substr(slash + 1)] = f.id;
		}
	}

	// Build address spans from lines → spans → segments
	_addrSpans.clear();
	_romOffsetSpans.clear();
	_fileLineToAddr.clear();
	_fileLineToRomOffset.clear();

	// Largest ROM offset any span reaches — a self-contained upper bound on the
	// ROM size, used below to tell ROM symbols from RAM ones.
	uint32_t maxRomOffset = 0;

	for(auto& line : data.lines) {
		if(line.fileId < 0 || line.spanIds.empty()) continue;

		uint32_t firstAddr = 0xFFFFFFFF;
		uint32_t firstRomOffset = 0xFFFFFFFF;

		for(int spanId : line.spanIds) {
			if(spanId < 0 || spanId >= (int)data.spans.size()) continue;
			const DbgSpan& span = data.spans[spanId];
			if(span.id < 0 || span.segId < 0 || span.segId >= (int)data.segments.size()) continue;

			const DbgSegment& seg = data.segments[span.segId];

			// Compute CPU address from ROM file offset.
			// For SNES LoROM: each 32KB ROM chunk maps to $xx:8000-$xx:FFFF
			uint32_t romOffset = seg.ooffs + span.start;
			uint32_t bankSize = seg.size > 0 ? seg.size : 0x8000;
			uint32_t bank = romOffset / bankSize;
			uint32_t offsetInBank = romOffset % bankSize;
			uint32_t addr = (bank << 16) | ((seg.start & 0xFFFF) + offsetInBank);

			AddrSpan as;
			as.addr = addr;
			as.romOffset = romOffset;
			as.size = span.size;
			as.fileId = line.fileId;
			as.line = line.line;
			_addrSpans.push_back(as);

			if(romOffset + span.size > maxRomOffset) {
				maxRomOffset = romOffset + span.size;
			}

			if(addr < firstAddr) {
				firstAddr = addr;
			}
			if(romOffset < firstRomOffset) {
				firstRomOffset = romOffset;
			}
		}

		// Map file+line → first address
		FileLineKey key{line.fileId, line.line};
		if(firstAddr != 0xFFFFFFFF) {
			auto it = _fileLineToAddr.find(key);
			if(it == _fileLineToAddr.end() || firstAddr < it->second) {
				_fileLineToAddr[key] = firstAddr;
			}
		}
		if(firstRomOffset != 0xFFFFFFFF) {
			auto it = _fileLineToRomOffset.find(key);
			if(it == _fileLineToRomOffset.end() || firstRomOffset < it->second) {
				_fileLineToRomOffset[key] = firstRomOffset;
			}
		}
	}

	// Sort address spans for binary search
	std::sort(_addrSpans.begin(), _addrSpans.end(),
		[](const AddrSpan& a, const AddrSpan& b) { return a.addr < b.addr; });

	_romOffsetSpans = _addrSpans;
	std::sort(_romOffsetSpans.begin(), _romOffsetSpans.end(),
		[](const AddrSpan& a, const AddrSpan& b) { return a.romOffset < b.romOffset; });

	// Build symbol table. Symbol values are CPU addresses measured from the ROM
	// segment base — the same origin span offsets use — so inverting that gives
	// a ROM offset, which is what survives bank mirroring.
	bool haveRomSeg = false;
	uint32_t segBase = 0;
	for(auto& seg : data.segments) {
		if(seg.type != "ro") continue;
		segBase = seg.start & 0xFFFF;
		haveRomSeg = true;
		break;
	}

	_symbols.clear();
	_romSymbols.clear();
	for(auto& sym : data.symbols) {
		if(sym.name.empty() || sym.type != "lab") continue;
		SymEntry entry;
		entry.addr = sym.val;
		entry.size = sym.size;
		entry.name = sym.name;
		entry.romOffset = NoRomOffset;
		// The bound matters: RAM statics are recorded as "lab" too (a $7Exxxx
		// work variable would otherwise masquerade as a huge ROM offset).
		if(haveRomSeg && sym.val >= segBase && (sym.val - segBase) < maxRomOffset) {
			entry.romOffset = sym.val - segBase;
			_romSymbols.push_back(entry);
		}
		_symbols.push_back(entry);
	}
	std::sort(_symbols.begin(), _symbols.end(),
		[](const SymEntry& a, const SymEntry& b) { return a.addr < b.addr; });
	std::sort(_romSymbols.begin(), _romSymbols.end(),
		[](const SymEntry& a, const SymEntry& b) { return a.romOffset < b.romOffset; });

	_loaded = true;
	fprintf(stderr, "[DAP] Loaded .dbg: %zu files, %zu spans, %zu symbols\n",
		_fileNames.size(), _addrSpans.size(), _symbols.size());
	return true;
}

SourceLocation SourceMapper::GetSourceLocation(uint32_t cpuAddr) const
{
	if(_addrSpans.empty()) return {};

	// Binary search: find the last span where addr <= cpuAddr
	auto it = std::upper_bound(_addrSpans.begin(), _addrSpans.end(), cpuAddr,
		[](uint32_t addr, const AddrSpan& span) { return addr < span.addr; });

	// Check spans around this position (may need to check multiple since
	// spans can overlap and we want the smallest containing span)
	SourceLocation best;
	uint32_t bestSize = 0xFFFFFFFF;

	// Search backwards from upper_bound position
	auto checkIt = it;
	while(checkIt != _addrSpans.begin()) {
		--checkIt;
		if(checkIt->addr + checkIt->size <= cpuAddr) {
			// This span ends before our address and all prior spans have
			// even lower addresses, so we can stop
			if(checkIt->addr + 256 < cpuAddr) break; // heuristic: stop if too far
		}
		if(cpuAddr >= checkIt->addr && cpuAddr < checkIt->addr + checkIt->size) {
			if(checkIt->size < bestSize) {
				bestSize = checkIt->size;
				best.valid = true;
				best.line = checkIt->line;
				if(checkIt->fileId >= 0 && checkIt->fileId < (int)_fileNames.size()) {
					best.file = _fileNames[checkIt->fileId];
				}
			}
		}
	}

	return best;
}

uint32_t SourceMapper::LookupFileLine(
	const std::unordered_map<FileLineKey, uint32_t, FileLineHash>& table,
	const std::string& file, int line) const
{
	// Try full path first
	auto fileIt = _fileNameToId.find(file);
	if(fileIt == _fileNameToId.end()) {
		// Try basename
		size_t slash = file.find_last_of("/\\");
		if(slash != std::string::npos) {
			fileIt = _fileNameToId.find(file.substr(slash + 1));
		}
	}

	if(fileIt == _fileNameToId.end()) {
		return 0xFFFFFFFF;
	}

	// Exact line match
	auto it = table.find(FileLineKey{fileIt->second, line});
	if(it != table.end()) {
		return it->second;
	}

	// Search nearby lines (within +5 lines) for closest match
	for(int delta = 1; delta <= 5; delta++) {
		it = table.find(FileLineKey{fileIt->second, line + delta});
		if(it != table.end()) return it->second;
		it = table.find(FileLineKey{fileIt->second, line - delta});
		if(it != table.end()) return it->second;
	}

	return 0xFFFFFFFF;
}

uint32_t SourceMapper::GetAddress(const std::string& file, int line) const
{
	return LookupFileLine(_fileLineToAddr, file, line);
}

uint32_t SourceMapper::GetRomOffset(const std::string& file, int line) const
{
	return LookupFileLine(_fileLineToRomOffset, file, line);
}

SourceLocation SourceMapper::GetSourceLocationByRomOffset(uint32_t romOffset) const
{
	if(_romOffsetSpans.empty()) return {};

	auto it = std::upper_bound(_romOffsetSpans.begin(), _romOffsetSpans.end(), romOffset,
		[](uint32_t off, const AddrSpan& span) { return off < span.romOffset; });

	SourceLocation best;
	uint32_t bestSize = 0xFFFFFFFF;

	auto checkIt = it;
	while(checkIt != _romOffsetSpans.begin()) {
		--checkIt;
		if(checkIt->romOffset + checkIt->size <= romOffset) {
			// Stop once we are clearly past any span that could contain it
			if(checkIt->romOffset + 256 < romOffset) break;
		}
		if(romOffset >= checkIt->romOffset && romOffset < checkIt->romOffset + checkIt->size) {
			if(checkIt->size < bestSize) {
				bestSize = checkIt->size;
				best.valid = true;
				best.line = checkIt->line;
				if(checkIt->fileId >= 0 && checkIt->fileId < (int)_fileNames.size()) {
					best.file = _fileNames[checkIt->fileId];
				}
			}
		}
	}

	return best;
}

std::string SourceMapper::LookupSymbol(const std::vector<SymEntry>& syms,
	uint32_t key, bool byRom)
{
	if(syms.empty()) return "";

	auto keyOf = [byRom](const SymEntry& sym) { return byRom ? sym.romOffset : sym.addr; };

	// Binary search for exact match
	auto it = std::lower_bound(syms.begin(), syms.end(), key,
		[&](const SymEntry& sym, uint32_t k) { return keyOf(sym) < k; });

	if(it != syms.end() && keyOf(*it) == key) {
		return it->name;
	}

	// Check if the address falls within a sized symbol
	if(it != syms.begin()) {
		--it;
		if(it->size > 0 && key >= keyOf(*it) && key < keyOf(*it) + it->size) {
			return it->name;
		}
	}

	// No exact match or containing symbol — return empty
	return "";
}

std::string SourceMapper::GetSymbolName(uint32_t cpuAddr) const
{
	return LookupSymbol(_symbols, cpuAddr, false);
}

std::string SourceMapper::GetSymbolNameByRomOffset(uint32_t romOffset) const
{
	return LookupSymbol(_romSymbols, romOffset, true);
}
