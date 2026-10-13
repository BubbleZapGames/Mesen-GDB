#include "test_harness.h"
#include "Core/Debugger/DAP/DbgFileParser.h"
#include "Core/Debugger/DAP/SourceMapper.h"
#include <fstream>

// Tests run against GDB/testdata/sample.dbg — a small checked-in fixture in
// real cc65 .dbg syntax. This used to point at an external project's build
// output, whose values silently drifted every time that project was rebuilt.
static std::string GetDbgPath()
{
	static const char* candidates[] = {
		"GDB/testdata/sample.dbg",
		"testdata/sample.dbg",
		"../GDB/testdata/sample.dbg",
	};
	for(const char* c : candidates) {
		std::ifstream f(c);
		if(f.is_open()) return c;
	}
	return candidates[0];
}

// ── DbgFileParser tests ──────────────────────────────────────────

TEST(dbg_parse_loads_file)
{
	DbgData data;
	bool ok = DbgFileParser::Parse(GetDbgPath(), data);
	ASSERT_TRUE(ok);
}

TEST(dbg_parse_nonexistent_file)
{
	DbgData data;
	ASSERT_FALSE(DbgFileParser::Parse("/nonexistent/path.dbg", data));
}

TEST(dbg_parse_segments)
{
	DbgData data;
	DbgFileParser::Parse(GetDbgPath(), data);
	ASSERT_EQ((int)data.segments.size(), 3);
	ASSERT_EQ(data.segments[0].id, 0);
	ASSERT_STR_EQ(data.segments[0].name, "BANK0");
	ASSERT_EQ((int)data.segments[0].start, 0x8000);
	ASSERT_EQ((int)data.segments[0].size, 0x8000);
	ASSERT_EQ((int)data.segments[0].ooffs, 0);
	ASSERT_EQ(data.segments[1].id, 1);
	ASSERT_STR_EQ(data.segments[1].name, "BANK1");
	ASSERT_EQ((int)data.segments[1].ooffs, 32768);
	ASSERT_STR_EQ(data.segments[2].name, "ZEROPAGE");
	ASSERT_EQ((int)data.segments[2].size, 0x100);
}

TEST(dbg_parse_files)
{
	DbgData data;
	DbgFileParser::Parse(GetDbgPath(), data);
	ASSERT_EQ((int)data.files.size(), 3);
	ASSERT_TRUE(data.files[0].name.find("sneslib.r65") != std::string::npos);
	ASSERT_TRUE(data.files[1].name.find("main.r65") != std::string::npos);
	ASSERT_TRUE(data.files[2].name.find("audio.r65") != std::string::npos);
}

TEST(dbg_parse_spans)
{
	DbgData data;
	DbgFileParser::Parse(GetDbgPath(), data);
	ASSERT_EQ((int)data.spans.size(), 6);
	ASSERT_EQ(data.spans[0].segId, 0);
	ASSERT_EQ((int)data.spans[0].start, 256);
	ASSERT_EQ((int)data.spans[0].size, 2);
	ASSERT_EQ(data.spans[4].segId, 1);
	ASSERT_EQ((int)data.spans[4].start, 64);
	ASSERT_EQ((int)data.spans[4].size, 5);
}

TEST(dbg_parse_lines)
{
	DbgData data;
	DbgFileParser::Parse(GetDbgPath(), data);
	ASSERT_EQ((int)data.lines.size(), 6);
	ASSERT_EQ(data.lines[0].fileId, 0);
	ASSERT_EQ(data.lines[0].line, 10);
	ASSERT_EQ((int)data.lines[0].spanIds.size(), 1);
	ASSERT_EQ(data.lines[0].spanIds[0], 0);
	ASSERT_EQ(data.lines[3].fileId, 1);
	ASSERT_EQ(data.lines[3].line, 100);
}

TEST(dbg_parse_multi_span_line)
{
	DbgData data;
	DbgFileParser::Parse(GetDbgPath(), data);
	ASSERT_EQ((int)data.lines[1].spanIds.size(), 2);
	ASSERT_EQ(data.lines[1].spanIds[0], 1);
	ASSERT_EQ(data.lines[1].spanIds[1], 2);
}

TEST(dbg_parse_line_without_span)
{
	DbgData data;
	DbgFileParser::Parse(GetDbgPath(), data);
	ASSERT_EQ(data.lines[5].fileId, 2);
	ASSERT_EQ(data.lines[5].line, 5);
	ASSERT_TRUE(data.lines[5].spanIds.empty());
}

TEST(dbg_parse_symbols)
{
	DbgData data;
	DbgFileParser::Parse(GetDbgPath(), data);
	ASSERT_EQ((int)data.symbols.size(), 4);
	ASSERT_STR_EQ(data.symbols[0].name, "__init_start");
	ASSERT_EQ((int)data.symbols[0].val, 0x8100);
	ASSERT_STR_EQ(data.symbols[0].type, "lab");
	ASSERT_STR_EQ(data.symbols[1].name, "wait_nmi");
	ASSERT_EQ((int)data.symbols[1].val, 0x8102);
	ASSERT_EQ((int)data.symbols[1].size, 8);
	ASSERT_STR_EQ(data.symbols[2].name, "main");
	ASSERT_EQ((int)data.symbols[2].val, 0x18040);
	ASSERT_STR_EQ(data.symbols[3].name, "TILE_COUNT");
	ASSERT_STR_EQ(data.symbols[3].type, "equ");
}

TEST(dbg_parse_scopes)
{
	DbgData data;
	DbgFileParser::Parse(GetDbgPath(), data);
	ASSERT_EQ((int)data.scopes.size(), 3);
	ASSERT_STR_EQ(data.scopes[0].name, "");
	ASSERT_STR_EQ(data.scopes[2].name, "wait_nmi");
	ASSERT_EQ(data.scopes[2].parentId, 0);
	ASSERT_EQ(data.scopes[2].symId, 1);
}

// Optional smoke test against a real toolchain-produced .dbg. Values from a
// real build drift, so this only checks that such a file parses at all.
TEST(dbg_parse_real_file_smoke)
{
	const char* path = getenv("TEST_DBG_PATH");
	if(!path) {
		ASSERT_TRUE(true); // not configured — nothing to check
		return;
	}
	DbgData data;
	ASSERT_TRUE(DbgFileParser::Parse(path, data));
	ASSERT_TRUE(!data.segments.empty());
	ASSERT_TRUE(!data.files.empty());
	ASSERT_TRUE(!data.spans.empty());
	ASSERT_TRUE(!data.lines.empty());
	ASSERT_TRUE(!data.symbols.empty());
}

// ── SourceMapper tests ───────────────────────────────────────────

TEST(sourcemapper_load)
{
	SourceMapper mapper;
	ASSERT_FALSE(mapper.IsLoaded());
	ASSERT_TRUE(mapper.LoadDbgFile(GetDbgPath()));
	ASSERT_TRUE(mapper.IsLoaded());
}

TEST(sourcemapper_addr_to_source)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	SourceLocation loc = mapper.GetSourceLocation(0x8100);
	ASSERT_TRUE(loc.valid);
	ASSERT_EQ(loc.line, 10);
	ASSERT_TRUE(loc.file.find("sneslib.r65") != std::string::npos);
}

TEST(sourcemapper_addr_within_span)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	// $8101 is the second byte of the 2-byte span for line 10
	SourceLocation loc = mapper.GetSourceLocation(0x8101);
	ASSERT_TRUE(loc.valid);
	ASSERT_EQ(loc.line, 10);
}

TEST(sourcemapper_addr_multi_span_line)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	// Line 11 owns two spans: $8102+3 and $8105+4
	SourceLocation loc = mapper.GetSourceLocation(0x8103);
	ASSERT_TRUE(loc.valid);
	ASSERT_EQ(loc.line, 11);
	loc = mapper.GetSourceLocation(0x8106);
	ASSERT_TRUE(loc.valid);
	ASSERT_EQ(loc.line, 11);
}

TEST(sourcemapper_addr_second_bank)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	SourceLocation loc = mapper.GetSourceLocation(0x18040);
	ASSERT_TRUE(loc.valid);
	ASSERT_EQ(loc.line, 100);
	ASSERT_TRUE(loc.file.find("main.r65") != std::string::npos);
}

TEST(sourcemapper_addr_no_mapping)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	ASSERT_FALSE(mapper.GetSourceLocation(0x0000).valid);
	// Just past the end of the last span in bank 0
	ASSERT_FALSE(mapper.GetSourceLocation(0x810B).valid);
}

TEST(sourcemapper_source_to_addr)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	uint32_t addr = mapper.GetAddress("/proj/src/main.r65", 100);
	ASSERT_TRUE(addr != 0xFFFFFFFF);
	ASSERT_EQ((int)addr, 0x18040);
}

TEST(sourcemapper_source_to_addr_basename)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	ASSERT_EQ((int)mapper.GetAddress("main.r65", 100), 0x18040);
	// A path that doesn't match the .dbg still resolves by basename
	ASSERT_EQ((int)mapper.GetAddress("/elsewhere/main.r65", 100), 0x18040);
}

TEST(sourcemapper_source_to_addr_multi_span)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	// Line 11 has two spans — the lowest address wins
	ASSERT_EQ((int)mapper.GetAddress("sneslib.r65", 11), 0x8102);
}

TEST(sourcemapper_source_to_addr_nearby_line)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	// Line 98 has no code; the +/-5 line search finds line 100
	ASSERT_EQ((int)mapper.GetAddress("main.r65", 98), 0x18040);
	// Line 200 is outside the search window
	ASSERT_EQ(mapper.GetAddress("main.r65", 200), 0xFFFFFFFF);
}

TEST(sourcemapper_source_to_addr_no_file)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	ASSERT_EQ(mapper.GetAddress("nonexistent.r65", 1), 0xFFFFFFFF);
}

TEST(sourcemapper_source_to_addr_line_without_code)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	// audio.r65 line 5 is listed but emits no spans
	ASSERT_EQ(mapper.GetAddress("audio.r65", 5), 0xFFFFFFFF);
}

TEST(sourcemapper_symbol_exact)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	ASSERT_STR_EQ(mapper.GetSymbolName(0x8100), "__init_start");
	ASSERT_STR_EQ(mapper.GetSymbolName(0x8102), "wait_nmi");
	ASSERT_STR_EQ(mapper.GetSymbolName(0x18040), "main");
}

TEST(sourcemapper_symbol_within_size)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	// wait_nmi covers $8102-$8109
	ASSERT_STR_EQ(mapper.GetSymbolName(0x8105), "wait_nmi");
	ASSERT_STR_EQ(mapper.GetSymbolName(0x8109), "wait_nmi");
	ASSERT_STR_EQ(mapper.GetSymbolName(0x810A), "");
}

TEST(sourcemapper_symbol_no_match)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	// __init_start has size 0, so the following byte resolves to nothing
	ASSERT_STR_EQ(mapper.GetSymbolName(0x8101), "");
	// Non-label symbols (equ) are not part of the symbol table
	ASSERT_STR_EQ(mapper.GetSymbolName(0x0020), "");
}

// The .dbg records ROM file offsets, which are the same whichever bank mirror
// the CPU fetches through — this is what makes symbol lookup work on a FastROM
// build running from $80:xxxx, or a HiROM build running from $C0:xxxx.
TEST(sourcemapper_symbol_by_rom_offset)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	// Segment base is $8000, so $8100/$8102 sit at ROM offsets 0x100/0x102
	ASSERT_STR_EQ(mapper.GetSymbolNameByRomOffset(0x100), "__init_start");
	ASSERT_STR_EQ(mapper.GetSymbolNameByRomOffset(0x102), "wait_nmi");
}

TEST(sourcemapper_symbol_by_rom_offset_within_size)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	// wait_nmi covers ROM offsets 0x102-0x109
	ASSERT_STR_EQ(mapper.GetSymbolNameByRomOffset(0x105), "wait_nmi");
	ASSERT_STR_EQ(mapper.GetSymbolNameByRomOffset(0x109), "wait_nmi");
	ASSERT_STR_EQ(mapper.GetSymbolNameByRomOffset(0x10A), "");
	// __init_start has size 0, so only its exact offset resolves
	ASSERT_STR_EQ(mapper.GetSymbolNameByRomOffset(0x101), "");
}

TEST(sourcemapper_symbol_by_rom_offset_excludes_non_rom)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	// TILE_COUNT is an equ, not a label — absent from both symbol tables
	ASSERT_STR_EQ(mapper.GetSymbolNameByRomOffset(0x20), "");
	ASSERT_STR_EQ(mapper.GetSymbolName(0x20), "");
}

// Known limitation: a symbol in a bank other than the first is not resolvable by
// ROM offset. Its recorded value ($018040) minus the segment base does not yield
// its true ROM offset (32832), so it is deliberately left out of the ROM table
// and only the CPU-address lookup finds it.
TEST(sourcemapper_symbol_second_bank_addr_only)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	ASSERT_STR_EQ(mapper.GetSymbolNameByRomOffset(32832), "");
	ASSERT_STR_EQ(mapper.GetSymbolName(0x18040), "main");
}

TEST(sourcemapper_source_to_rom_offset)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	// sneslib.r65:10 -> seg 0 (ooffs 0) + span start 256
	ASSERT_EQ((int)mapper.GetRomOffset("/proj/src/lib/sneslib.r65", 10), 256);
	// main.r65:100 -> seg 1 (ooffs 32768) + span start 64
	ASSERT_EQ((int)mapper.GetRomOffset("main.r65", 100), 32832);
	ASSERT_EQ(mapper.GetRomOffset("nonexistent.r65", 1), 0xFFFFFFFF);
}

TEST(sourcemapper_rom_offset_to_source)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	SourceLocation loc = mapper.GetSourceLocationByRomOffset(256);
	ASSERT_TRUE(loc.valid);
	ASSERT_EQ(loc.line, 10);
	ASSERT_TRUE(loc.file.find("sneslib.r65") != std::string::npos);

	// Second bank, and a byte inside the span rather than at its start
	loc = mapper.GetSourceLocationByRomOffset(32834);
	ASSERT_TRUE(loc.valid);
	ASSERT_EQ(loc.line, 100);
	ASSERT_TRUE(loc.file.find("main.r65") != std::string::npos);

	// Past the end of every span in the first bank
	ASSERT_FALSE(mapper.GetSourceLocationByRomOffset(300).valid);
}

TEST(sourcemapper_bank_crossing)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	uint32_t addr = mapper.GetAddress("main.r65", 100);
	ASSERT_TRUE(addr != 0xFFFFFFFF);
	ASSERT_EQ((int)((addr >> 16) & 0xFF), 1);
}

TEST(sourcemapper_file_list)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	ASSERT_EQ((int)mapper.GetSourceFiles().size(), 3);
	ASSERT_TRUE(mapper.GetSourceFiles()[0].find("sneslib.r65") != std::string::npos);
	ASSERT_TRUE(mapper.GetSourceFiles()[1].find("main.r65") != std::string::npos);
}

// ── SourceMapper: resolved lines, macro expansions, source paths ─

static std::string Fixture(const char* name)
{
	for(const char* dir : { "GDB/testdata/", "testdata/", "../GDB/testdata/" }) {
		std::string path = std::string(dir) + name;
		std::ifstream f(path);
		if(f.is_open()) return path;
	}
	return std::string("GDB/testdata/") + name;
}

static bool EndsWith(const std::string& s, const std::string& suffix)
{
	return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

TEST(sourcemapper_reports_resolved_line)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	int line = 0;
	// Line 98 has no code: the breakpoint lands on 100, and says so
	ASSERT_EQ((int)mapper.GetAddress("main.r65", 98, &line), 0x18040);
	ASSERT_EQ(line, 100);
	ASSERT_EQ((int)mapper.GetRomOffset("main.r65", 100, &line), 0x8040);
	ASSERT_EQ(line, 100);
}

TEST(sourcemapper_macro_expansion_belongs_to_its_statement)
{
	SourceMapper mapper;
	ASSERT_TRUE(mapper.LoadDbgFile(Fixture("macro.dbg")));
	// $8003 and $8005 are the expansion of a macro invoked on line 5
	SourceLocation loc = mapper.GetSourceLocation(0x8003);
	ASSERT_TRUE(loc.valid);
	ASSERT_EQ(loc.line, 5);
	ASSERT_TRUE(EndsWith(loc.file, "game.r65"));
	loc = mapper.GetSourceLocation(0x8006);
	ASSERT_TRUE(loc.valid);
	ASSERT_EQ(loc.line, 5);
	ASSERT_EQ(mapper.GetSourceLocationByRomOffset(0x0005).line, 5);
	// The next real statement is untouched
	ASSERT_EQ(mapper.GetSourceLocation(0x8008).line, 9);
	// An expansion with no statement just before it stays unmapped rather
	// than naming a file the client cannot open
	ASSERT_FALSE(mapper.GetSourceLocation(0x8040).valid);
}

TEST(sourcemapper_uses_source_beside_dbg_when_recorded_path_is_gone)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(Fixture("macro.dbg"));
	// macro.dbg records /nonexistent/build/game.r65; testdata/game.r65 exists
	SourceLocation loc = mapper.GetSourceLocation(0x8000);
	ASSERT_TRUE(EndsWith(loc.file, "testdata/game.r65"));
	ASSERT_TRUE(loc.file.find("nonexistent") == std::string::npos);
}

TEST(sourcemapper_reports_the_clients_path)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(Fixture("macro.dbg"));
	mapper.NoteClientPath("/home/someone/project/game.r65");
	ASSERT_STR_EQ(mapper.GetSourceLocation(0x8000).file, "/home/someone/project/game.r65");
	ASSERT_EQ((int)mapper.GetAddress("/home/someone/project/game.r65", 9), 0x8008);
}

TEST(sourcemapper_same_name_prefers_matching_directories)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(Fixture("samename.dbg"));
	ASSERT_EQ((int)mapper.GetAddress("/checkout/a/main.r65", 3), 0x8000);
	ASSERT_EQ((int)mapper.GetAddress("/checkout/b/main.r65", 3), 0x8010);
}

TEST(sourcemapper_same_name_never_falls_back_beside_dbg)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(Fixture("samename.dbg"));
	// testdata/main.r65 exists, but could be either of the two recorded files
	ASSERT_STR_EQ(mapper.GetSourceLocation(0x8000).file, "/nonexistent/a/main.r65");
	ASSERT_STR_EQ(mapper.GetSourceLocation(0x8010).file, "/nonexistent/b/main.r65");
}
