#pragma once
#include <cstdio>
#include <string>
#include "DapJson.h"
#include "SimpleLock.h"

class DapMessageWriter {
private:
	FILE* _output;
	SimpleLock _lock;

public:
	DapMessageWriter(FILE* output = stdout);
	void SendMessage(JsonValue&& message);
};