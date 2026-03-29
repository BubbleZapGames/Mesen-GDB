#include "DapMessageWriter.h"
#include <string>
#include <cstdio>
#include <cstdint>
#include <string>
#include "SimpleLock.h"

DapMessageWriter::DapMessageWriter(FILE* output) : _output(output) {}

void DapMessageWriter::SendMessage(JsonValue&& message) {
	auto lock = _lock.AcquireSafe();
	
	std::string json = message.Serialize();
	std::string header = "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n";
	
	fwrite(header.c_str(), 1, header.size(), _output);
	fwrite(json.c_str(), 1, json.size(), _output);
	fflush(_output);
}
