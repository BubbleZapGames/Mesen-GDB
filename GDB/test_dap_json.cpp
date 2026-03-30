#include "test_harness.h"
#include "Core/Debugger/DAP/DapJson.h"

// ── Null / Bool / Number basics ───────────────────────────────────

TEST(json_null)
{
	auto v = JsonValue::MakeNull();
	ASSERT_EQ(v.GetType(), JsonValue::Type::Null);
	ASSERT_STR_EQ(v.Serialize(), "null");
}

TEST(json_bool)
{
	auto t = JsonValue::MakeBool(true);
	auto f = JsonValue::MakeBool(false);
	ASSERT_EQ(t.GetBool(), true);
	ASSERT_EQ(f.GetBool(), false);
	ASSERT_STR_EQ(t.Serialize(), "true");
	ASSERT_STR_EQ(f.Serialize(), "false");
}

TEST(json_number_integer)
{
	auto v = JsonValue::MakeNumber(42);
	ASSERT_EQ(v.GetType(), JsonValue::Type::Number);
	ASSERT_EQ(v.GetInt(), 42);
	ASSERT_STR_EQ(v.Serialize(), "42");
}

TEST(json_number_negative)
{
	auto v = JsonValue::MakeNumber(-7);
	ASSERT_EQ(v.GetInt(), -7);
	ASSERT_STR_EQ(v.Serialize(), "-7");
}

TEST(json_number_zero)
{
	auto v = JsonValue::MakeNumber(0);
	ASSERT_STR_EQ(v.Serialize(), "0");
}

TEST(json_string_simple)
{
	auto v = JsonValue::MakeString("hello");
	ASSERT_EQ(v.GetType(), JsonValue::Type::String);
	ASSERT_STR_EQ(v.GetString(), "hello");
	ASSERT_STR_EQ(v.Serialize(), "\"hello\"");
}

TEST(json_string_escaping)
{
	auto v = JsonValue::MakeString("a\"b\\c\nd");
	std::string s = v.Serialize();
	ASSERT_STR_EQ(s, "\"a\\\"b\\\\c\\nd\"");
}

TEST(json_string_empty)
{
	auto v = JsonValue::MakeString("");
	ASSERT_STR_EQ(v.GetString(), "");
	ASSERT_STR_EQ(v.Serialize(), "\"\"");
}

// ── Array ────────────────────────────────────────────────────────

TEST(json_array_empty)
{
	auto v = JsonValue::MakeArray();
	ASSERT_EQ(v.GetType(), JsonValue::Type::Array);
	ASSERT_EQ(v.GetArray().size(), (size_t)0);
	ASSERT_STR_EQ(v.Serialize(), "[]");
}

TEST(json_array_push)
{
	auto arr = JsonValue::MakeArray();
	arr.Push(JsonValue::MakeNumber(1));
	arr.Push(JsonValue::MakeString("two"));
	arr.Push(JsonValue::MakeBool(true));
	ASSERT_EQ(arr.GetArray().size(), (size_t)3);
	ASSERT_EQ(arr[0].GetInt(), 1);
	ASSERT_STR_EQ(arr[1].GetString(), "two");
	ASSERT_EQ(arr[2].GetBool(), true);
	ASSERT_STR_EQ(arr.Serialize(), "[1,\"two\",true]");
}

// ── Object ───────────────────────────────────────────────────────

TEST(json_object_empty)
{
	auto v = JsonValue::MakeObject();
	ASSERT_EQ(v.GetType(), JsonValue::Type::Object);
	ASSERT_STR_EQ(v.Serialize(), "{}");
}

TEST(json_object_set_get)
{
	auto obj = JsonValue::MakeObject();
	obj.Set("name", JsonValue::MakeString("test"));
	obj.Set("value", JsonValue::MakeNumber(42));
	obj.Set("flag", JsonValue::MakeBool(true));
	ASSERT_STR_EQ(obj["name"].GetString(), "test");
	ASSERT_EQ(obj["value"].GetInt(), 42);
	ASSERT_EQ(obj["flag"].GetBool(), true);
}

TEST(json_object_missing_key)
{
	auto obj = JsonValue::MakeObject();
	obj.Set("a", JsonValue::MakeNumber(1));
	ASSERT_EQ(obj["missing"].GetType(), JsonValue::Type::Null);
	ASSERT_EQ(obj["missing"].GetInt(), 0);
	ASSERT_STR_EQ(obj["missing"].GetString(), "");
}

TEST(json_object_nested)
{
	auto inner = JsonValue::MakeObject();
	inner.Set("x", JsonValue::MakeNumber(10));
	auto outer = JsonValue::MakeObject();
	outer.Set("inner", std::move(inner));
	ASSERT_EQ(outer["inner"]["x"].GetInt(), 10);
}

// ── Parsing ──────────────────────────────────────────────────────

TEST(json_parse_null)
{
	auto r = ParseJson("null");
	ASSERT_TRUE(r.has_value());
	ASSERT_EQ(r->GetType(), JsonValue::Type::Null);
}

TEST(json_parse_bool)
{
	auto t = ParseJson("true");
	auto f = ParseJson("false");
	ASSERT_TRUE(t.has_value());
	ASSERT_TRUE(f.has_value());
	ASSERT_EQ(t->GetBool(), true);
	ASSERT_EQ(f->GetBool(), false);
}

TEST(json_parse_number)
{
	auto r = ParseJson("12345");
	ASSERT_TRUE(r.has_value());
	ASSERT_EQ(r->GetInt(), 12345);
}

TEST(json_parse_negative)
{
	auto r = ParseJson("-99");
	ASSERT_TRUE(r.has_value());
	ASSERT_EQ(r->GetInt(), -99);
}

TEST(json_parse_string)
{
	auto r = ParseJson("\"hello world\"");
	ASSERT_TRUE(r.has_value());
	ASSERT_STR_EQ(r->GetString(), "hello world");
}

TEST(json_parse_string_escapes)
{
	auto r = ParseJson("\"a\\\"b\\\\c\\nd\"");
	ASSERT_TRUE(r.has_value());
	ASSERT_STR_EQ(r->GetString(), "a\"b\\c\nd");
}

TEST(json_parse_array)
{
	auto r = ParseJson("[1, 2, 3]");
	ASSERT_TRUE(r.has_value());
	ASSERT_EQ(r->GetArray().size(), (size_t)3);
	ASSERT_EQ((*r)[0].GetInt(), 1);
	ASSERT_EQ((*r)[2].GetInt(), 3);
}

TEST(json_parse_object)
{
	auto r = ParseJson("{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\"}");
	ASSERT_TRUE(r.has_value());
	ASSERT_EQ((*r)["seq"].GetInt(), 1);
	ASSERT_STR_EQ((*r)["type"].GetString(), "request");
	ASSERT_STR_EQ((*r)["command"].GetString(), "initialize");
}

TEST(json_parse_nested_object)
{
	auto r = ParseJson("{\"body\":{\"supportsConfigurationDoneRequest\":true}}");
	ASSERT_TRUE(r.has_value());
	ASSERT_EQ((*r)["body"]["supportsConfigurationDoneRequest"].GetBool(), true);
}

TEST(json_parse_whitespace)
{
	auto r = ParseJson("  { \"a\" : 1 , \"b\" : [ 2 , 3 ] }  ");
	ASSERT_TRUE(r.has_value());
	ASSERT_EQ((*r)["a"].GetInt(), 1);
	ASSERT_EQ((*r)["b"][0].GetInt(), 2);
}

TEST(json_parse_empty_string)
{
	auto r = ParseJson("");
	ASSERT_FALSE(r.has_value());
}

TEST(json_parse_invalid)
{
	ASSERT_FALSE(ParseJson("{invalid}").has_value());
	ASSERT_FALSE(ParseJson("{\"a\":}").has_value());
}

// ── Round-trip ───────────────────────────────────────────────────

TEST(json_roundtrip_dap_message)
{
	auto resp = JsonValue::MakeObject();
	resp.Set("seq", JsonValue::MakeNumber(1));
	resp.Set("type", JsonValue::MakeString("response"));
	resp.Set("command", JsonValue::MakeString("initialize"));
	resp.Set("success", JsonValue::MakeBool(true));
	auto body = JsonValue::MakeObject();
	body.Set("supportsConfigurationDoneRequest", JsonValue::MakeBool(true));
	resp.Set("body", std::move(body));

	std::string json = resp.Serialize();
	auto parsed = ParseJson(json);
	ASSERT_TRUE(parsed.has_value());
	ASSERT_EQ((*parsed)["seq"].GetInt(), 1);
	ASSERT_STR_EQ((*parsed)["command"].GetString(), "initialize");
	ASSERT_EQ((*parsed)["success"].GetBool(), true);
	ASSERT_EQ((*parsed)["body"]["supportsConfigurationDoneRequest"].GetBool(), true);
}

// ── Copy / Move semantics ────────────────────────────────────────

TEST(json_copy)
{
	auto orig = JsonValue::MakeObject();
	orig.Set("key", JsonValue::MakeString("value"));
	JsonValue copy = orig;
	ASSERT_STR_EQ(copy["key"].GetString(), "value");
	copy.Set("key2", JsonValue::MakeNumber(2));
	ASSERT_EQ(orig["key2"].GetType(), JsonValue::Type::Null);
}

TEST(json_move)
{
	auto orig = JsonValue::MakeString("hello");
	JsonValue moved = std::move(orig);
	ASSERT_STR_EQ(moved.GetString(), "hello");
}

TEST(json_assign_cross_type)
{
	auto a = JsonValue::MakeNumber(1);
	auto b = JsonValue::MakeString("two");
	a = b;
	ASSERT_STR_EQ(a.GetString(), "two");
}

TEST(json_getint_getuint)
{
	auto v = JsonValue::MakeNumber(65537);
	ASSERT_EQ(v.GetInt(), 65537);
	ASSERT_EQ(v.GetUint(), (uint32_t)65537);
}
