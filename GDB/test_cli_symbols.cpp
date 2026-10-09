#include "test_harness.h"
#include "cli_symbols.h"
#include <stdexcept>

// The CLI's symbol table and address parsing, on a .sym in WLA-DX syntax.
static const char* kSym =
	"; wla symbolic information\n"
	"[labels]\n"
	"00:8100 main\n"
	"01:8000 __table_data\n"
	"[definitions]\n"
	"00000002 _sizeof_heli_x_px\n"
	"00000040 _sizeof_units__x_px\n"
	"00000010 _sizeof___table_data\n"
	"[ramsections]\n"
	"00:1624 heli_x_px\n"
	"7e:2000 units__x_px\n"
	"00:00be beef\n";

static CliSymbols Loaded()
{
	CliSymbols s;
	s.LoadSymText(kSym);
	return s;
}

static bool Throws(const std::string& text, const CliSymbols* syms)
{
	try { ParseCliAddress(text, syms); } catch(const std::invalid_argument&) { return true; }
	return false;
}

TEST(cli_sym_labels_and_sizes)
{
	CliSymbols s = Loaded();
	ASSERT_EQ(s.Count(), (size_t)5);
	ASSERT_EQ(s.Find("main")->addr, 0x008100u);
	ASSERT_EQ(s.Find("main")->size, 0u);
	ASSERT_EQ(s.Find("units__x_px")->addr, 0x7E2000u);
	ASSERT_EQ(s.Find("units__x_px")->size, 0x40u);
	ASSERT_EQ(s.Find("__table_data")->size, 0x10u);
	ASSERT_TRUE(s.Find("_sizeof_heli_x_px") == nullptr);
}

TEST(cli_sym_numbers)
{
	ASSERT_EQ(ParseCliAddress("$1624", nullptr), 0x1624u);
	ASSERT_EQ(ParseCliAddress("0x1624", nullptr), 0x1624u);
	ASSERT_EQ(ParseCliAddress("1624", nullptr), 0x1624u);
	ASSERT_EQ(ParseCliAddress("7E:2000", nullptr), 0x7E2000u);
	ASSERT_EQ(ParseCliAddress("ff", nullptr), 0xFFu);
	ASSERT_TRUE(Throws("12zz", nullptr));
	ASSERT_EQ(ParseCliNumber("90"), 90u);
	ASSERT_EQ(ParseCliNumber("0x5A"), 0x5Au);
	ASSERT_EQ(ParseCliNumber("$5A"), 0x5Au);
}

TEST(cli_sym_names)
{
	CliSymbols s = Loaded();
	uint32_t size = 0;
	ASSERT_EQ(ParseCliAddress("heli_x_px", &s, &size), 0x1624u);
	ASSERT_EQ(size, 2u);
	ASSERT_EQ(ParseCliAddress("heli_x_px+1", &s, &size), 0x1625u);
	ASSERT_EQ(size, 0u);
	ASSERT_EQ(ParseCliAddress("units__x_px+10", &s), 0x7E2010u);   // offsets are hex
	// A name beats hex; a word that names nothing may still be hex.
	ASSERT_EQ(ParseCliAddress("beef", &s), 0xBEu);
	ASSERT_EQ(ParseCliAddress("dead", &s), 0xDEADu);
	ASSERT_EQ(ParseCliAddress("$beef", &s), 0xBEEFu);
	ASSERT_TRUE(Throws("heli_y_px", &s));
	ASSERT_TRUE(Throws("nope+1", &s));
}

TEST(cli_sym_missing_name_suggests)
{
	CliSymbols s = Loaded();
	std::string message;
	try { ParseCliAddress("x_px", &s); } catch(const std::invalid_argument& e) { message = e.what(); }
	ASSERT_STR_EQ(message, "no symbol 'x_px' (similar: heli_x_px, units__x_px)");
	ASSERT_STR_EQ(NoSymbolMessage("x", nullptr), "no symbol 'x' (no symbols loaded)");
}

TEST(cli_sym_dbg_then_sym)
{
	// The .dbg fixture names wait_nmi with a size and TILE_COUNT as a constant;
	// a .sym loaded with it wins the address and keeps the .dbg's size.
	static const char* candidates[] = { "GDB/testdata/sample.dbg", "testdata/sample.dbg" };
	CliSymbols s;
	for(const char* c : candidates) {
		if(s.Load("", c) && s.Count() > 0) break;
	}
	ASSERT_EQ(s.Find("wait_nmi")->addr, 0x008102u);
	ASSERT_EQ(s.Find("wait_nmi")->size, 8u);
	ASSERT_TRUE(s.Find("TILE_COUNT")->isConst);
	ASSERT_EQ(s.Find("TILE_COUNT")->addr, 0x20u);
	s.LoadSymText("[labels]\n00:9000 wait_nmi\n");
	ASSERT_EQ(s.Find("wait_nmi")->addr, 0x009000u);
	ASSERT_EQ(s.Find("wait_nmi")->size, 8u);
}
