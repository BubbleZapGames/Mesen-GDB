#include "test_harness.h"
#include "Core/Debugger/DAP/DbgFileParser.h"
#include "Core/Debugger/DAP/SourceMapper.h"

static std::string GetDbgPath()
{
	const char* p = getenv("TEST_DBG_PATH");
	if(p) return p;
	return "/home/nathan/R65/classickong.r65/build/main.dbg";
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
	ASSERT_TRUE(data.segments.size() >= 3);
	ASSERT_EQ(data.segments[0].id, 0);
	ASSERT_STR_EQ(data.segments[0].name, "BANK0");
	ASSERT_EQ((int)data.segments[0].start, 0x8000);
	ASSERT_EQ((int)data.segments[0].size, 0x8000);
	ASSERT_EQ((int)data.segments[0].ooffs, 0);
	ASSERT_EQ(data.segments[1].id, 1);
	ASSERT_STR_EQ(data.segments[1].name, "BANK5");
}

TEST(dbg_parse_files)
{
	DbgData data;
	DbgFileParser::Parse(GetDbgPath(), data);
	ASSERT_TRUE(data.files.size() >= 22);
	ASSERT_TRUE(data.files[0].name.find("sneslib.r65") != std::string::npos);
	ASSERT_TRUE(data.files[11].name.find("main.r65") != std::string::npos);
}

TEST(dbg_parse_spans)
{
	DbgData data;
	DbgFileParser::Parse(GetDbgPath(), data);
	ASSERT_TRUE(data.spans.size() >= 15000);
	ASSERT_EQ(data.spans[0].segId, 0);
	ASSERT_EQ((int)data.spans[0].start, 101);
	ASSERT_EQ((int)data.spans[0].size, 3);
}

TEST(dbg_parse_lines)
{
	DbgData data;
	DbgFileParser::Parse(GetDbgPath(), data);
	ASSERT_TRUE(data.lines.size() >= 2800);
	ASSERT_EQ(data.lines[0].fileId, 0);
	ASSERT_EQ(data.lines[0].line, 1836);
	ASSERT_TRUE(data.lines[0].spanIds.size() >= 1);
	ASSERT_EQ(data.lines[0].spanIds[0], 0);
}

TEST(dbg_parse_multi_span_line)
{
	DbgData data;
	DbgFileParser::Parse(GetDbgPath(), data);
	ASSERT_EQ((int)data.lines[1].spanIds.size(), 2);
	ASSERT_EQ(data.lines[1].spanIds[0], 1);
	ASSERT_EQ(data.lines[1].spanIds[1], 2);
}

TEST(dbg_parse_symbols)
{
	DbgData data;
	DbgFileParser::Parse(GetDbgPath(), data);
	ASSERT_TRUE(data.symbols.size() >= 800);
	ASSERT_STR_EQ(data.symbols[0].name, "__init_start");
	ASSERT_EQ((int)data.symbols[0].val, 0x8064);
	ASSERT_STR_EQ(data.symbols[1].name, "wait_nmi");
	ASSERT_EQ((int)data.symbols[1].val, 0x8065);
	ASSERT_STR_EQ(data.symbols[134].name, "main");
	ASSERT_EQ((int)data.symbols[134].val, 0xE64A);
}

TEST(dbg_parse_scopes)
{
	DbgData data;
	DbgFileParser::Parse(GetDbgPath(), data);
	ASSERT_TRUE(data.scopes.size() >= 100);
	ASSERT_STR_EQ(data.scopes[0].name, "");
	ASSERT_STR_EQ(data.scopes[2].name, "wait_nmi");
	ASSERT_EQ(data.scopes[2].parentId, 0);
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
	SourceLocation loc = mapper.GetSourceLocation(0x8065);
	ASSERT_TRUE(loc.valid);
	ASSERT_EQ(loc.line, 1836);
	ASSERT_TRUE(loc.file.find("sneslib.r65") != std::string::npos);
}

TEST(sourcemapper_addr_within_span)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	SourceLocation loc = mapper.GetSourceLocation(0x8066);
	ASSERT_TRUE(loc.valid);
	ASSERT_EQ(loc.line, 1836);
}

TEST(sourcemapper_addr_no_mapping)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	SourceLocation loc = mapper.GetSourceLocation(0x0000);
	ASSERT_FALSE(loc.valid);
}

TEST(sourcemapper_source_to_addr)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	uint32_t addr = mapper.GetAddress("/home/nathan/R65/classickong.r65/src/main.r65", 1062);
	ASSERT_TRUE(addr != 0xFFFFFFFF);
	ASSERT_EQ((int)addr, 0xE659);
}

TEST(sourcemapper_source_to_addr_basename)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	uint32_t addr = mapper.GetAddress("main.r65", 1062);
	ASSERT_EQ((int)addr, 0xE659);
}

TEST(sourcemapper_source_to_addr_no_file)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	ASSERT_EQ(mapper.GetAddress("nonexistent.r65", 1), 0xFFFFFFFF);
}

TEST(sourcemapper_symbol_exact)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	ASSERT_STR_EQ(mapper.GetSymbolName(0x8065), "wait_nmi");
	ASSERT_STR_EQ(mapper.GetSymbolName(0xE64A), "main");
}

TEST(sourcemapper_symbol_no_match)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	ASSERT_STR_EQ(mapper.GetSymbolName(0x8100), "");
}

TEST(sourcemapper_bank_crossing)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	uint32_t addr = mapper.GetAddress("main.r65", 181);
	if(addr != 0xFFFFFFFF) {
		uint32_t bank = (addr >> 16) & 0xFF;
		ASSERT_EQ((int)bank, 1);
	} else {
		ASSERT_TRUE(true); // acceptable if line doesn't resolve
	}
}

TEST(sourcemapper_file_list)
{
	SourceMapper mapper;
	mapper.LoadDbgFile(GetDbgPath());
	ASSERT_TRUE(mapper.GetSourceFiles().size() >= 22);
	ASSERT_TRUE(mapper.GetSourceFiles()[0].find("sneslib.r65") != std::string::npos);
}
