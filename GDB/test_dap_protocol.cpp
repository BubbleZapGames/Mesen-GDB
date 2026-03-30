#include "test_harness.h"
#include "Core/Debugger/DAP/DapMessageReader.h"
#include "Core/Debugger/DAP/DapMessageWriter.h"
#include "Core/Debugger/DAP/DapJson.h"
#include "Core/Debugger/DAP/DapTypes.h"
#include <unistd.h>

static bool MakePipe(FILE*& readEnd, FILE*& writeEnd)
{
	int fds[2];
	if(pipe(fds) != 0) return false;
	readEnd = fdopen(fds[0], "r");
	writeEnd = fdopen(fds[1], "w");
	return readEnd && writeEnd;
}

// ── Writer tests ─────────────────────────────────────────────────

TEST(protocol_write_format)
{
	FILE* r; FILE* w;
	ASSERT_TRUE(MakePipe(r, w));

	DapMessageWriter writer(w);
	auto msg = JsonValue::MakeObject();
	msg.Set("seq", JsonValue::MakeNumber(1));
	msg.Set("type", JsonValue::MakeString("response"));
	writer.SendMessage(std::move(msg));
	fclose(w);

	char buf[1024];
	size_t n = fread(buf, 1, sizeof(buf) - 1, r);
	buf[n] = '\0';
	fclose(r);

	std::string raw(buf, n);
	ASSERT_TRUE(raw.find("Content-Length:") == 0);
	ASSERT_TRUE(raw.find("\r\n\r\n") != std::string::npos);
	ASSERT_TRUE(raw.find("\"seq\":1") != std::string::npos);
}

// ── Reader tests ─────────────────────────────────────────────────

TEST(protocol_read_message)
{
	FILE* r; FILE* w;
	ASSERT_TRUE(MakePipe(r, w));

	std::string body = "{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\"}";
	fprintf(w, "Content-Length: %d\r\n\r\n%s", (int)body.size(), body.c_str());
	fclose(w);

	DapMessageReader reader(r);
	auto msg = reader.ReadMessage();
	fclose(r);

	ASSERT_TRUE(msg.has_value());
	ASSERT_EQ(msg->Get("seq").GetInt(), 1);
	ASSERT_STR_EQ(msg->Get("command").GetString(), "initialize");
}

TEST(protocol_read_eof)
{
	FILE* r; FILE* w;
	ASSERT_TRUE(MakePipe(r, w));
	fclose(w);

	DapMessageReader reader(r);
	ASSERT_FALSE(reader.ReadMessage().has_value());
	fclose(r);
}

// ── Round-trip tests ─────────────────────────────────────────────

TEST(protocol_roundtrip)
{
	FILE* r; FILE* w;
	ASSERT_TRUE(MakePipe(r, w));

	DapMessageWriter writer(w);
	auto outMsg = JsonValue::MakeObject();
	outMsg.Set("seq", JsonValue::MakeNumber(42));
	outMsg.Set("type", JsonValue::MakeString("event"));
	outMsg.Set("event", JsonValue::MakeString("stopped"));
	auto body = JsonValue::MakeObject();
	body.Set("reason", JsonValue::MakeString("breakpoint"));
	body.Set("threadId", JsonValue::MakeNumber(1));
	outMsg.Set("body", std::move(body));
	writer.SendMessage(std::move(outMsg));
	fclose(w);

	DapMessageReader reader(r);
	auto inMsg = reader.ReadMessage();
	fclose(r);

	ASSERT_TRUE(inMsg.has_value());
	ASSERT_EQ(inMsg->Get("seq").GetInt(), 42);
	ASSERT_STR_EQ(inMsg->Get("event").GetString(), "stopped");
	ASSERT_STR_EQ(inMsg->Get("body")["reason"].GetString(), "breakpoint");
	ASSERT_EQ(inMsg->Get("body")["threadId"].GetInt(), 1);
}

TEST(protocol_multiple_messages)
{
	FILE* r; FILE* w;
	ASSERT_TRUE(MakePipe(r, w));

	DapMessageWriter writer(w);
	for(int i = 1; i <= 3; i++) {
		auto msg = JsonValue::MakeObject();
		msg.Set("seq", JsonValue::MakeNumber(i));
		msg.Set("type", JsonValue::MakeString("test"));
		writer.SendMessage(std::move(msg));
	}
	fclose(w);

	DapMessageReader reader(r);
	for(int i = 1; i <= 3; i++) {
		auto msg = reader.ReadMessage();
		ASSERT_TRUE(msg.has_value());
		ASSERT_EQ(msg->Get("seq").GetInt(), i);
	}
	ASSERT_FALSE(reader.ReadMessage().has_value());
	fclose(r);
}

TEST(protocol_large_message)
{
	FILE* r; FILE* w;
	ASSERT_TRUE(MakePipe(r, w));

	std::string bigStr(4096, 'X');
	DapMessageWriter writer(w);
	auto msg = JsonValue::MakeObject();
	msg.Set("data", JsonValue::MakeString(bigStr));
	writer.SendMessage(std::move(msg));
	fclose(w);

	DapMessageReader reader(r);
	auto result = reader.ReadMessage();
	fclose(r);

	ASSERT_TRUE(result.has_value());
	ASSERT_EQ((int)result->Get("data").GetString().size(), 4096);
}

// ── DapTypes constants ───────────────────────────────────────────

TEST(dap_types_commands)
{
	ASSERT_STR_EQ(DapCommand::Initialize, "initialize");
	ASSERT_STR_EQ(DapCommand::Launch, "launch");
	ASSERT_STR_EQ(DapCommand::StepIn, "stepIn");
	ASSERT_STR_EQ(DapCommand::Next, "next");
	ASSERT_STR_EQ(DapCommand::StepOut, "stepOut");
	ASSERT_STR_EQ(DapCommand::SetBreakpoints, "setBreakpoints");
	ASSERT_STR_EQ(DapCommand::Disassemble, "disassemble");
	ASSERT_STR_EQ(DapCommand::Evaluate, "evaluate");
	ASSERT_STR_EQ(DapCommand::ReadMemory, "readMemory");
	ASSERT_STR_EQ(DapCommand::WriteMemory, "writeMemory");
}

TEST(dap_types_events)
{
	ASSERT_STR_EQ(DapEvent::Initialized, "initialized");
	ASSERT_STR_EQ(DapEvent::Stopped, "stopped");
	ASSERT_STR_EQ(DapEvent::Terminated, "terminated");
}

TEST(dap_types_stopped_reasons)
{
	ASSERT_STR_EQ(DapStoppedReason::Entry, "entry");
	ASSERT_STR_EQ(DapStoppedReason::Breakpoint, "breakpoint");
	ASSERT_STR_EQ(DapStoppedReason::Step, "step");
	ASSERT_STR_EQ(DapStoppedReason::Pause, "pause");
}
