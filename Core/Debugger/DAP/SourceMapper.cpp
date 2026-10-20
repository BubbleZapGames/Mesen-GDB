#include "SourceMapper.h"
#include "DbgFileParser.h"
#include "WlaSymParser.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>

static bool FileExists(const std::string& path)
{
	std::ifstream f(path);
	return f.good();
}

static std::string Basename(const std::string& path)
{
	size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

static bool IsAbsolute(const std::string& path)
{
	return !path.empty() && (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'));
}

// Generated code with no file of its own, e.g. "<macro:set_backdrop>" for a
// macro expansion. The client cannot open it.
static bool IsPseudoFile(const std::string& name)
{
	return !name.empty() && name[0] == '<';
}

// Where a recorded source path is on disk, or empty. A relative path is
// tried against the symbol file's folder and each folder above it before
// the working directory: the build ran in one of them (cc65 records paths
// as given on its command line, and a .dbg often lands in build/).
static std::string FindSourceFile(const std::string& recorded, const std::string& symbolDir)
{
	if(!IsAbsolute(recorded)) {
		std::string dir = symbolDir;
		while(!dir.empty()) {
			if(FileExists(dir + recorded)) return dir + recorded;
			if(dir.size() < 2) break;
			size_t slash = dir.find_last_of("/\\", dir.size() - 2);
			if(slash == std::string::npos) break;
			dir = dir.substr(0, slash + 1);
		}
	}
	return FileExists(recorded) ? recorded : "";
}

// cc65 prefixes a C name with '_' (main is _main). "__name" is the
// runtime's own, so it keeps its underscores.
static std::string CName(const std::string& name)
{
	if(name.size() > 1 && name[0] == '_' && name[1] != '_') return name.substr(1);
	return name;
}

// Largest gap between the end of a statement's code and a macro expansion
// that is still taken to belong to that statement
static const uint32_t MACRO_ATTACH_GAP = 8;

// A .sym line mapping records where each line's code starts, not its size:
// it runs to the next mapped address, but never further than this
static const uint32_t SYM_LINE_MAX_SIZE = 64;

// Nor does a .sym label say how long it is: its code runs to the next label,
// within this
static const uint32_t SYM_LABEL_MAX_SIZE = 0x2000;

void SourceMapper::SetNesRom(uint32_t headerSize)
{
	_nes = true;
	_romHeaderSize = headerSize;
}

bool SourceMapper::LoadDbgFile(const std::string& path)
{
	DbgData data;
	if(!DbgFileParser::Parse(path, data)) {
		return false;
	}

	std::vector<std::string> files(data.files.size());
	for(auto& f : data.files) {
		if(f.id >= 0) files[f.id] = f.name;
	}

	// Names in a C program are the C ones, not cc65's assembler names
	bool hasCLines = std::any_of(data.lines.begin(), data.lines.end(),
		[](const DbgLine& l) { return l.type == 1; });

	std::vector<RawSpan> spans;
	// Largest ROM offset any span reaches — a self-contained upper bound on the
	// ROM size, used below to tell ROM symbols from RAM ones.
	uint32_t maxRomOffset = 0;
	for(auto& line : data.lines) {
		if(line.fileId < 0) continue;
		for(int spanId : line.spanIds) {
			if(spanId < 0 || spanId >= (int)data.spans.size()) continue;
			const DbgSpan& span = data.spans[spanId];
			if(span.id < 0 || span.segId < 0 || span.segId >= (int)data.segments.size()) continue;

			// Only ROM segments are in the file the offsets count
			const DbgSegment& seg = data.segments[span.segId];
			if(seg.type == "rw") continue;
			uint32_t fileOffset = seg.ooffs + span.start;
			if(fileOffset < _romHeaderSize) continue;

			RawSpan rs;
			rs.romOffset = fileOffset - _romHeaderSize;
			if(_nes || (seg.start >> 16) != 0) {
				// The segment's own address: NES, or a segment placed in a
				// bank explicitly (ca65 with a SNES memory map, $80:8000)
				rs.addr = seg.start + span.start;
			} else {
				// For SNES LoROM: each 32KB ROM chunk maps to $xx:8000-$xx:FFFF
				uint32_t bankSize = seg.size > 0 ? seg.size : 0x8000;
				uint32_t bank = fileOffset / bankSize;
				uint32_t offsetInBank = fileOffset % bankSize;
				rs.addr = (bank << 16) | ((seg.start & 0xFFFF) + offsetInBank);
			}
			rs.size = span.size;
			rs.fileId = line.fileId;
			rs.line = line.line;
			rs.cLine = line.type == 1;
			spans.push_back(rs);

			maxRomOffset = std::max(maxRomOffset, rs.romOffset + span.size);
		}
	}

	// A label in a ROM segment lies at that segment's offset plus its distance
	// from the segment's start. R65 writes banked values (bank << 16 | addr)
	// that its segments don't cover; those are measured from the first ROM
	// segment's base, the same origin its span offsets use.
	bool haveRomSeg = false;
	uint32_t segBase = 0;
	for(auto& seg : data.segments) {
		if(seg.type != "ro") continue;
		segBase = seg.start & 0xFFFF;
		haveRomSeg = true;
		break;
	}
	auto romOffsetOf = [&](uint32_t val, int segId) -> uint32_t {
		if(segId >= 0 && segId < (int)data.segments.size()) {
			const DbgSegment& seg = data.segments[segId];
			if(seg.type != "rw" && val >= seg.start && val < seg.start + seg.size) {
				uint32_t fileOffset = seg.ooffs + (val - seg.start);
				return fileOffset >= _romHeaderSize ? fileOffset - _romHeaderSize : NoRomOffset;
			}
		}
		// The bound matters: RAM statics are recorded as "lab" too (a $7Exxxx
		// work variable would otherwise masquerade as a huge ROM offset).
		if(!_nes && haveRomSeg && val >= segBase && (val - segBase) < maxRomOffset) {
			return val - segBase;
		}
		return NoRomOffset;
	};

	std::vector<SymEntry> labels;
	for(auto& sym : data.symbols) {
		if(sym.name.empty() || sym.type != "lab") continue;
		labels.push_back({ sym.val, romOffsetOf(sym.val, sym.segId), sym.size,
			hasCLines ? CName(sym.name) : sym.name });
	}

	// A sized scope is a function (.proc, or a C function): it names every
	// address inside it, past whatever internal labels it holds
	std::vector<SymEntry> scopes;
	for(auto& sc : data.scopes) {
		if(sc.type != "scope" || sc.size == 0) continue;
		if(sc.symId < 0 || sc.symId >= (int)data.symbols.size()) continue;
		const DbgSymbol& sym = data.symbols[sc.symId];
		if(sym.id < 0 || sym.name.empty()) continue;
		scopes.push_back({ sym.val, romOffsetOf(sym.val, sym.segId), sc.size,
			hasCLines ? CName(sym.name) : sym.name });
	}

	Build(path, files, spans, std::move(labels), std::move(scopes));
	fprintf(stderr, "[DAP] Loaded .dbg: %zu files, %zu spans, %zu symbols\n",
		_fileNames.size(), _addrSpans.size(), _symbols.size());
	return true;
}

bool SourceMapper::LoadSymFile(const std::string& path, const std::function<uint32_t(uint32_t cpuAddr)>& toRomOffset)
{
	WlaSymData data;
	if(!WlaSymParser::Parse(path, data)) {
		return false;
	}

	std::vector<std::string> files;
	std::map<uint32_t, int> fileIds;
	for(auto& [key, name] : data.files) {
		fileIds[key] = (int)files.size();
		files.push_back(name);
	}

	std::vector<RawSpan> spans;
	for(auto& entry : data.lines) {
		auto file = fileIds.find(entry.fileKey);
		if(file == fileIds.end() || entry.line <= 0) continue;
		RawSpan rs;
		rs.fileId = file->second;
		rs.line = entry.line;
		if(entry.hasRomOffset) {
			// v2 gives the ROM offset; the address it prints is the slot's,
			// not one the CPU necessarily runs the code at
			rs.romOffset = entry.romOffset;
			rs.hasAddr = false;
		} else {
			rs.addr = entry.cpuAddr;
			rs.romOffset = toRomOffset(entry.cpuAddr);
		}
		spans.push_back(rs);
	}

	// Each line's code runs to where the next mapped line starts
	auto sizeSpans = [&spans](bool byRom) {
		std::vector<RawSpan*> ordered;
		for(auto& s : spans) {
			if(byRom ? s.romOffset != NoRomOffset : (s.hasAddr && s.romOffset == NoRomOffset)) ordered.push_back(&s);
		}
		auto keyOf = [byRom](const RawSpan* s) { return byRom ? s->romOffset : s->addr; };
		std::sort(ordered.begin(), ordered.end(),
			[&](const RawSpan* a, const RawSpan* b) { return keyOf(a) < keyOf(b); });
		for(size_t i = 0; i < ordered.size(); i++) {
			uint32_t gap = SYM_LINE_MAX_SIZE;
			for(size_t j = i + 1; j < ordered.size(); j++) {
				if(keyOf(ordered[j]) > keyOf(ordered[i])) {
					gap = std::min(gap, keyOf(ordered[j]) - keyOf(ordered[i]));
					break;
				}
			}
			ordered[i]->size = gap;
		}
	};
	sizeSpans(true);
	sizeSpans(false);

	std::vector<SymEntry> labels;
	for(auto& label : data.labels) {
		labels.push_back({ label.addr, toRomOffset(label.addr), 0, label.name });
	}
	// Each ROM label runs to the next one
	std::vector<SymEntry*> romLabels;
	for(auto& l : labels) {
		if(l.romOffset != NoRomOffset) romLabels.push_back(&l);
	}
	std::sort(romLabels.begin(), romLabels.end(),
		[](const SymEntry* a, const SymEntry* b) { return a->romOffset < b->romOffset; });
	for(size_t i = 0; i < romLabels.size(); i++) {
		for(size_t j = i + 1; j < romLabels.size(); j++) {
			uint32_t gap = romLabels[j]->romOffset - romLabels[i]->romOffset;
			if(gap == 0) continue;
			if(gap <= SYM_LABEL_MAX_SIZE) romLabels[i]->size = gap;
			break;
		}
	}

	Build(path, files, spans, std::move(labels), {});
	fprintf(stderr, "[DAP] Loaded .sym: %zu files, %zu line mappings, %zu labels\n",
		_fileNames.size(), _romOffsetSpans.size(), _symbols.size());
	return true;
}

void SourceMapper::Build(const std::string& symbolPath, const std::vector<std::string>& files,
	const std::vector<RawSpan>& rawSpans, std::vector<SymEntry> labels, std::vector<SymEntry> scopes)
{
	// Build file name list and reverse lookup
	_fileNames.clear();
	_displayNames.clear();
	_fileNameToId.clear();
	std::string symbolDir = symbolPath.substr(0, symbolPath.size() - Basename(symbolPath).size());
	// The file beside the symbol file can only stand in for a missing file
	// whose name no other file shares — with two main.r65s it could be either
	std::unordered_map<std::string, int> nameCount;
	for(auto& name : files) {
		if(!name.empty() && !IsPseudoFile(name)) nameCount[Basename(name)]++;
	}
	_fileNames = files;
	_displayNames.resize(files.size());
	_fileFound.assign(files.size(), false);
	for(int id = 0; id < (int)files.size(); id++) {
		const std::string& name = files[id];
		if(name.empty()) continue;
		_fileNameToId[name] = id;

		std::string display = name;
		if(!IsPseudoFile(name)) {
			std::string found = FindSourceFile(name, symbolDir);
			if(!found.empty()) {
				display = found;
				_fileFound[id] = true;
			} else if(nameCount[Basename(name)] == 1 && FileExists(symbolDir + Basename(name))) {
				display = symbolDir + Basename(name);
				_fileFound[id] = true;
			}
		}
		_displayNames[id] = display;
		_fileNameToId[display] = id;
	}

	// Map file+line → first address
	_fileLineToAddr.clear();
	_fileLineToRomOffset.clear();
	for(const RawSpan& span : rawSpans) {
		FileLineKey key{span.fileId, span.line};
		if(span.hasAddr) {
			auto it = _fileLineToAddr.find(key);
			if(it == _fileLineToAddr.end() || span.addr < it->second) {
				_fileLineToAddr[key] = span.addr;
			}
		}
		if(span.romOffset != NoRomOffset) {
			auto it = _fileLineToRomOffset.find(key);
			if(it == _fileLineToRomOffset.end() || span.romOffset < it->second) {
				_fileLineToRomOffset[key] = span.romOffset;
			}
		}
	}

	// A macro expansion is part of the statement that invoked it. The .dbg
	// files it under a pseudo-file, so take the line of the real code just
	// before it; with none close by, leave it unmapped rather than point the
	// client at a file it cannot open.
	std::vector<RawSpan> sorted = rawSpans;
	std::sort(sorted.begin(), sorted.end(),
		[](const RawSpan& a, const RawSpan& b) { return a.romOffset < b.romOffset; });
	std::vector<RawSpan> attached;
	int lastReal = -1;
	for(const RawSpan& span : sorted) {
		bool pseudo = span.fileId >= 0 && span.fileId < (int)_fileNames.size() && IsPseudoFile(_fileNames[span.fileId]);
		if(!pseudo) {
			attached.push_back(span);
			lastReal = (int)attached.size() - 1;
			continue;
		}
		if(lastReal < 0 || span.romOffset == NoRomOffset) continue;
		const RawSpan& owner = attached[lastReal];
		uint32_t ownerEnd = owner.romOffset + owner.size;
		if(span.romOffset >= ownerEnd && span.romOffset - ownerEnd <= MACRO_ATTACH_GAP) {
			RawSpan owned = span;
			owned.fileId = owner.fileId;
			owned.line = owner.line;
			owned.cLine = owner.cLine;
			attached.push_back(owned);
			// A long expansion is several spans: each continues from the last
			lastReal = (int)attached.size() - 1;
		}
	}

	_addrSpans.clear();
	_romOffsetSpans.clear();
	for(const RawSpan& span : attached) {
		AddrSpan as{span.addr, span.romOffset, span.size, span.fileId, span.line, span.cLine};
		if(span.hasAddr) _addrSpans.push_back(as);
		if(span.romOffset != NoRomOffset) _romOffsetSpans.push_back(as);
	}
	std::sort(_addrSpans.begin(), _addrSpans.end(),
		[](const AddrSpan& a, const AddrSpan& b) { return a.addr < b.addr; });
	std::sort(_romOffsetSpans.begin(), _romOffsetSpans.end(),
		[](const AddrSpan& a, const AddrSpan& b) { return a.romOffset < b.romOffset; });

	_symbols = std::move(labels);
	_romSymbols.clear();
	for(auto& sym : _symbols) {
		if(sym.romOffset != NoRomOffset) _romSymbols.push_back(sym);
	}
	std::sort(_symbols.begin(), _symbols.end(),
		[](const SymEntry& a, const SymEntry& b) { return a.addr < b.addr; });
	std::sort(_romSymbols.begin(), _romSymbols.end(),
		[](const SymEntry& a, const SymEntry& b) { return a.romOffset < b.romOffset; });
	_scopes = std::move(scopes);

	_loaded = true;
}

SourceLocation SourceMapper::LookupSpan(const std::vector<AddrSpan>& spans, uint32_t key, bool byRom) const
{
	if(spans.empty()) return {};

	auto keyOf = [byRom](const AddrSpan& span) { return byRom ? span.romOffset : span.addr; };

	// Binary search: find the last span that starts at or before the key
	auto it = std::upper_bound(spans.begin(), spans.end(), key,
		[&](uint32_t k, const AddrSpan& span) { return k < keyOf(span); });

	// Spans overlap: a C line covers the assembly generated for it, and a
	// line can cover a macro expansion. A C line wins, else the smallest.
	const AddrSpan* best = nullptr;
	auto checkIt = it;
	while(checkIt != spans.begin()) {
		--checkIt;
		uint32_t start = keyOf(*checkIt);
		if(start + checkIt->size <= key) {
			// Stop once we are clearly past any span that could contain it
			if(start + 256 < key) break;
		}
		if(key >= start && key < start + checkIt->size) {
			bool better = !best
				|| (checkIt->cLine != best->cLine ? checkIt->cLine : checkIt->size < best->size);
			if(better) best = &*checkIt;
		}
	}

	SourceLocation loc;
	if(best) {
		loc.valid = true;
		loc.line = best->line;
		loc.file = DisplayName(best->fileId);
		loc.cLine = best->cLine;
		loc.fileFound = best->fileId >= 0 && best->fileId < (int)_fileFound.size() && _fileFound[best->fileId];
	}
	return loc;
}

SourceLocation SourceMapper::GetSourceLocation(uint32_t cpuAddr) const
{
	return LookupSpan(_addrSpans, cpuAddr, false);
}

SourceLocation SourceMapper::GetSourceLocationByRomOffset(uint32_t romOffset) const
{
	return LookupSpan(_romOffsetSpans, romOffset, true);
}

uint32_t SourceMapper::LookupFileLine(
	const std::unordered_map<FileLineKey, uint32_t, FileLineHash>& table,
	const std::string& file, int line, int* resolvedLine) const
{
	int fileId = FindFileId(file);
	if(fileId < 0) {
		return 0xFFFFFFFF;
	}

	// The line itself, else the nearest line within 5 that has code — later
	// lines first, since a breakpoint on a blank or comment line means the
	// statement that follows
	auto it = table.find(FileLineKey{fileId, line});
	if(it != table.end()) {
		if(resolvedLine) *resolvedLine = line;
		return it->second;
	}
	for(int delta = 1; delta <= 5; delta++) {
		it = table.find(FileLineKey{fileId, line + delta});
		if(it != table.end()) {
			if(resolvedLine) *resolvedLine = line + delta;
			return it->second;
		}
		it = table.find(FileLineKey{fileId, line - delta});
		if(it != table.end()) {
			if(resolvedLine) *resolvedLine = line - delta;
			return it->second;
		}
	}

	return 0xFFFFFFFF;
}

uint32_t SourceMapper::GetAddress(const std::string& file, int line, int* resolvedLine) const
{
	return LookupFileLine(_fileLineToAddr, file, line, resolvedLine);
}

uint32_t SourceMapper::GetRomOffset(const std::string& file, int line, int* resolvedLine) const
{
	return LookupFileLine(_fileLineToRomOffset, file, line, resolvedLine);
}

int SourceMapper::FindFileId(const std::string& path) const
{
	auto exact = _fileNameToId.find(path);
	if(exact != _fileNameToId.end()) {
		return exact->second;
	}

	// Same file name elsewhere: a build moved or copied since, or the client
	// opened it through another path. Several files can share a name, so
	// prefer the one whose path agrees with the most trailing characters.
	std::string name = Basename(path);
	int best = -1;
	size_t bestShared = 0;
	for(int id = 0; id < (int)_fileNames.size(); id++) {
		const std::string& recorded = _fileNames[id];
		if(recorded.empty() || IsPseudoFile(recorded) || Basename(recorded) != name) continue;
		size_t shared = 0;
		while(shared < recorded.size() && shared < path.size()
			&& recorded[recorded.size() - 1 - shared] == path[path.size() - 1 - shared]) {
			shared++;
		}
		if(best < 0 || shared > bestShared) {
			best = id;
			bestShared = shared;
		}
	}
	return best;
}

const std::string& SourceMapper::DisplayName(int fileId) const
{
	static const std::string none;
	if(fileId < 0 || fileId >= (int)_displayNames.size()) return none;
	return _displayNames[fileId];
}

void SourceMapper::NoteClientPath(const std::string& path)
{
	int fileId = FindFileId(path);
	if(fileId >= 0) {
		_displayNames[fileId] = path;
		_fileFound[fileId] = true;
		_fileNameToId[path] = fileId;
	}
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

std::string SourceMapper::LookupScope(uint32_t key, bool byRom) const
{
	const SymEntry* best = nullptr;
	for(const SymEntry& scope : _scopes) {
		uint32_t start = byRom ? scope.romOffset : scope.addr;
		if(byRom && start == NoRomOffset) continue;
		if(key >= start && key - start < scope.size && (!best || scope.size < best->size)) {
			best = &scope;
		}
	}
	return best ? best->name : "";
}

std::string SourceMapper::GetSymbolName(uint32_t cpuAddr) const
{
	std::string scope = LookupScope(cpuAddr, false);
	return scope.empty() ? LookupSymbol(_symbols, cpuAddr, false) : scope;
}

std::string SourceMapper::GetSymbolNameByRomOffset(uint32_t romOffset) const
{
	std::string scope = LookupScope(romOffset, true);
	return scope.empty() ? LookupSymbol(_romSymbols, romOffset, true) : scope;
}

std::string SourceMapper::GetLabelAtRomOffset(uint32_t romOffset) const
{
	// A function starting here, else any label here
	for(const SymEntry& scope : _scopes) {
		if(scope.romOffset == romOffset) return scope.name;
	}
	auto it = std::lower_bound(_romSymbols.begin(), _romSymbols.end(), romOffset,
		[](const SymEntry& sym, uint32_t k) { return sym.romOffset < k; });
	return it != _romSymbols.end() && it->romOffset == romOffset ? it->name : "";
}

bool SourceMapper::FindFunction(const std::string& name, uint32_t& romOffset, uint32_t& cpuAddr) const
{
	// The names as typed, then without or with cc65's '_': the table holds
	// C names without it, and assembly-defined ones as written
	std::vector<std::string> names = { name };
	if(name.size() > 1 && name[0] == '_') names.push_back(name.substr(1));
	else names.push_back("_" + name);

	for(const std::string& n : names) {
		for(const std::vector<SymEntry>* table : { &_scopes, &_symbols }) {
			for(const SymEntry& sym : *table) {
				if(sym.name == n) {
					romOffset = sym.romOffset;
					cpuAddr = sym.addr;
					return true;
				}
			}
		}
	}
	return false;
}
