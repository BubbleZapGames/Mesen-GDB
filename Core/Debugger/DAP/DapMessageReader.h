#pragma once
#include <cstdio>
#include <optional>
#include "DapJson.h"

class DapMessageReader {
private:
	FILE* _input;

public:
	DapMessageReader(FILE* input = stdin);
	std::optional<JsonValue> ReadMessage();
};