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
	_fileLineToAddr.clear();

	for(auto& line : data.lines) {
		if(line.fileId < 0 || line.spanIds.empty()) continue;

		uint32_t firstAddr = 0xFFFFFFFF;

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
			as.size = span.size;
			as.fileId = line.fileId;
			as.line = line.line;
			_addrSpans.push_back(as);

			if(addr < firstAddr) {
				firstAddr = addr;
			}
		}

		// Map file+line → first address
		if(firstAddr != 0xFFFFFFFF) {
			FileLineKey key{line.fileId, line.line};
			auto it = _fileLineToAddr.find(key);
			if(it == _fileLineToAddr.end() || firstAddr < it->second) {
				_fileLineToAddr[key] = firstAddr;
			}
		}
	}

	// Sort address spans for binary search
	std::sort(_addrSpans.begin(), _addrSpans.end(),
		[](const AddrSpan& a, const AddrSpan& b) { return a.addr < b.addr; });

	// Build symbol table
	_symbols.clear();
	for(auto& sym : data.symbols) {
		if(sym.name.empty() || sym.type != "lab") continue;
		SymEntry entry;
		entry.addr = sym.val;
		entry.size = sym.size;
		entry.name = sym.name;
		_symbols.push_back(entry);
	}
	std::sort(_symbols.begin(), _symbols.end(),
		[](const SymEntry& a, const SymEntry& b) { return a.addr < b.addr; });

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

uint32_t SourceMapper::GetAddress(const std::string& file, int line) const
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
	auto it = _fileLineToAddr.find(FileLineKey{fileIt->second, line});
	if(it != _fileLineToAddr.end()) {
		return it->second;
	}

	// Search nearby lines (within +5 lines) for closest match
	for(int delta = 1; delta <= 5; delta++) {
		it = _fileLineToAddr.find(FileLineKey{fileIt->second, line + delta});
		if(it != _fileLineToAddr.end()) return it->second;
		it = _fileLineToAddr.find(FileLineKey{fileIt->second, line - delta});
		if(it != _fileLineToAddr.end()) return it->second;
	}

	return 0xFFFFFFFF;
}

std::string SourceMapper::GetSymbolName(uint32_t cpuAddr) const
{
	if(_symbols.empty()) return "";

	// Binary search for exact match
	auto it = std::lower_bound(_symbols.begin(), _symbols.end(), cpuAddr,
		[](const SymEntry& sym, uint32_t addr) { return sym.addr < addr; });

	if(it != _symbols.end() && it->addr == cpuAddr) {
		return it->name;
	}

	// Check if the address falls within a sized symbol
	if(it != _symbols.begin()) {
		--it;
		if(it->size > 0 && cpuAddr >= it->addr && cpuAddr < it->addr + it->size) {
			return it->name;
		}
	}

	// No exact match or containing symbol — return empty
	return "";
}
