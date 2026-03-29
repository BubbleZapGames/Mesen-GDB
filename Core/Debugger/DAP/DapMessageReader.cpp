#include "DapMessageReader.h"
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>

DapMessageReader::DapMessageReader(FILE* input) : _input(input) {}

std::optional<JsonValue> DapMessageReader::ReadMessage() {
	char headerLine[256];
	size_t bytesRead = 0;
	
	// Read until we get an empty line (the Content-Length header)
	while(bytesRead < sizeof(headerLine) - 1) {
		int c = fgetc(_input);
		if(c == EOF) {
			return std::nullopt;
		}
		
		if(c == '\n') {
			// Check if this is an empty line (just \n after \r\n)
			if(bytesRead == 0 || (bytesRead == 1 && headerLine[0] == '\r')) {
				break;
			}
			headerLine[bytesRead++] = c;
		} else if(c == '\r') {
			// Skip \r, wait for \n
			continue;
		} else {
			headerLine[bytesRead++] = c;
		}
	}
	headerLine[bytesRead] = '\0';
	
	// Parse Content-Length header
	if(bytesRead == 0) {
		return std::nullopt;
	}
	
	std::string headerStr = headerLine;
	auto colonPos = headerStr.find(':');
	if(colonPos == std::string::npos) {
		return std::nullopt;
	}
	
	std::string lengthStr = headerStr.substr(colonPos + 1);
	lengthStr.erase(0, lengthStr.find_first_not_of(" \t"));
	lengthStr.erase(lengthStr.find_last_not_of(" \t\r\n") + 1);
	
	if(lengthStr.empty()) {
		return std::nullopt;
	}
	
	int contentLength = 0;
	try {
		contentLength = std::stoi(lengthStr);
	} catch(...) {
		return std::nullopt;
	}
	
	// Read content
	std::string content;
	content.resize(contentLength);
	
	size_t totalRead = 0;
	while(totalRead < (size_t)contentLength) {
		size_t toRead = contentLength - totalRead;
		size_t read = fread(&content[totalRead], 1, toRead, _input);
		if(read == 0) {
			return std::nullopt;
		}
		totalRead += read;
	}
	
	// Parse JSON
	return ParseJson(content);
}
