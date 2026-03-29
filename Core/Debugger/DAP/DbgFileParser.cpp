#include "DbgFileParser.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <algorithm>

// Parse comma-separated key=value pairs, calling callback for each
void DbgFileParser::ParseKeyValue(const std::string& data,
	void (*callback)(const std::string& key, const std::string& value, void* ctx), void* ctx)
{
	size_t pos = 0;
	while(pos < data.size()) {
		size_t eq = data.find('=', pos);
		if(eq == std::string::npos) break;

		std::string key = data.substr(pos, eq - pos);

		size_t valStart = eq + 1;
		size_t valEnd;

		// Handle quoted values
		if(valStart < data.size() && data[valStart] == '"') {
			valStart++;
			valEnd = data.find('"', valStart);
			if(valEnd == std::string::npos) valEnd = data.size();
			callback(key, data.substr(valStart, valEnd - valStart), ctx);
			// Skip past closing quote and comma
			pos = valEnd + 1;
			if(pos < data.size() && data[pos] == ',') pos++;
		} else {
			valEnd = data.find(',', valStart);
			if(valEnd == std::string::npos) valEnd = data.size();
			callback(key, data.substr(valStart, valEnd - valStart), ctx);
			pos = valEnd;
			if(pos < data.size() && data[pos] == ',') pos++;
		}
	}
}

static uint32_t ParseHexOrDec(const std::string& s)
{
	if(s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		return (uint32_t)strtoul(s.c_str() + 2, nullptr, 16);
	}
	return (uint32_t)strtoul(s.c_str(), nullptr, 10);
}

static std::vector<int> ParseIntList(const std::string& s)
{
	std::vector<int> result;
	size_t pos = 0;
	while(pos < s.size()) {
		size_t next = s.find('+', pos);
		if(next == std::string::npos) next = s.size();
		std::string part = s.substr(pos, next - pos);
		if(!part.empty()) {
			result.push_back(atoi(part.c_str()));
		}
		pos = next + 1;
	}
	return result;
}

bool DbgFileParser::Parse(const std::string& path, DbgData& out)
{
	std::ifstream file(path);
	if(!file.is_open()) {
		return false;
	}

	std::string line;
	while(std::getline(file, line)) {
		if(line.empty()) continue;

		// Split on tab: "keyword\tkey=value,key=value,..."
		size_t tab = line.find('\t');
		if(tab == std::string::npos) continue;

		std::string keyword = line.substr(0, tab);
		std::string data = line.substr(tab + 1);

		if(keyword == "file") {
			DbgFile f;
			ParseKeyValue(data, [](const std::string& k, const std::string& v, void* ctx) {
				DbgFile* f = (DbgFile*)ctx;
				if(k == "id") f->id = atoi(v.c_str());
				else if(k == "name") f->name = v;
			}, &f);
			if(f.id >= 0) {
				if(f.id >= (int)out.files.size()) out.files.resize(f.id + 1);
				out.files[f.id] = f;
			}
		} else if(keyword == "seg") {
			DbgSegment s;
			ParseKeyValue(data, [](const std::string& k, const std::string& v, void* ctx) {
				DbgSegment* s = (DbgSegment*)ctx;
				if(k == "id") s->id = atoi(v.c_str());
				else if(k == "name") s->name = v;
				else if(k == "start") s->start = ParseHexOrDec(v);
				else if(k == "size") s->size = ParseHexOrDec(v);
				else if(k == "ooffs") s->ooffs = ParseHexOrDec(v);
			}, &s);
			if(s.id >= 0) {
				if(s.id >= (int)out.segments.size()) out.segments.resize(s.id + 1);
				out.segments[s.id] = s;
			}
		} else if(keyword == "span") {
			DbgSpan sp;
			ParseKeyValue(data, [](const std::string& k, const std::string& v, void* ctx) {
				DbgSpan* sp = (DbgSpan*)ctx;
				if(k == "id") sp->id = atoi(v.c_str());
				else if(k == "seg") sp->segId = atoi(v.c_str());
				else if(k == "start") sp->start = ParseHexOrDec(v);
				else if(k == "size") sp->size = ParseHexOrDec(v);
			}, &sp);
			if(sp.id >= 0) {
				if(sp.id >= (int)out.spans.size()) out.spans.resize(sp.id + 1);
				out.spans[sp.id] = sp;
			}
		} else if(keyword == "line") {
			DbgLine l;
			std::string spanStr;
			struct LineCtx { DbgLine* l; std::string* spanStr; };
			LineCtx ctx{&l, &spanStr};
			ParseKeyValue(data, [](const std::string& k, const std::string& v, void* c) {
				LineCtx* ctx = (LineCtx*)c;
				if(k == "id") ctx->l->id = atoi(v.c_str());
				else if(k == "file") ctx->l->fileId = atoi(v.c_str());
				else if(k == "line") ctx->l->line = atoi(v.c_str());
				else if(k == "span") *ctx->spanStr = v;
			}, &ctx);
			if(!spanStr.empty()) {
				l.spanIds = ParseIntList(spanStr);
			}
			if(l.id >= 0) {
				if(l.id >= (int)out.lines.size()) out.lines.resize(l.id + 1);
				out.lines[l.id] = l;
			}
		} else if(keyword == "sym") {
			DbgSymbol sym;
			ParseKeyValue(data, [](const std::string& k, const std::string& v, void* ctx) {
				DbgSymbol* sym = (DbgSymbol*)ctx;
				if(k == "id") sym->id = atoi(v.c_str());
				else if(k == "name") sym->name = v;
				else if(k == "val") sym->val = ParseHexOrDec(v);
				else if(k == "seg") sym->segId = atoi(v.c_str());
				else if(k == "type") sym->type = v;
				else if(k == "size") sym->size = ParseHexOrDec(v);
				else if(k == "scope") sym->scopeId = atoi(v.c_str());
			}, &sym);
			if(sym.id >= 0) {
				if(sym.id >= (int)out.symbols.size()) out.symbols.resize(sym.id + 1);
				out.symbols[sym.id] = sym;
			}
		} else if(keyword == "scope") {
			DbgScope sc;
			ParseKeyValue(data, [](const std::string& k, const std::string& v, void* ctx) {
				DbgScope* sc = (DbgScope*)ctx;
				if(k == "id") sc->id = atoi(v.c_str());
				else if(k == "name") sc->name = v;
				else if(k == "parent") sc->parentId = atoi(v.c_str());
				else if(k == "sym") sc->symId = atoi(v.c_str());
				else if(k == "size") sc->size = ParseHexOrDec(v);
			}, &sc);
			if(sc.id >= 0) {
				if(sc.id >= (int)out.scopes.size()) out.scopes.resize(sc.id + 1);
				out.scopes[sc.id] = sc;
			}
		}
		// version, info, mod, csym, type — ignored for now
	}

	return true;
}
