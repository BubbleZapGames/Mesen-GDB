#include "DapMessageReader.h"
#include <string>
#include <cstdio>

DapMessageReader::DapMessageReader(FILE* input) : _input(input) {}

std::optional<JsonValue> DapMessageReader::ReadMessage()
{
	int contentLength = -1;

	// Read headers line-by-line until we hit an empty line (\r\n\r\n)
	while(true) {
		std::string line;
		while(true) {
			int c = fgetc(_input);
			if(c == EOF) {
				return std::nullopt;
			}
			if(c == '\n') break;
			if(c != '\r') line += (char)c;
		}

		// Empty line = end of headers
		if(line.empty()) {
			break;
		}

		// Parse "Content-Length: N" header
		auto colonPos = line.find(':');
		if(colonPos != std::string::npos) {
			std::string key = line.substr(0, colonPos);
			std::string value = line.substr(colonPos + 1);

			// Trim leading whitespace from value
			size_t start = value.find_first_not_of(" \t");
			if(start != std::string::npos) {
				value = value.substr(start);
			}

			if(key == "Content-Length") {
				try {
					contentLength = std::stoi(value);
				} catch(...) {
					return std::nullopt;
				}
			}
		}
	}

	if(contentLength <= 0) {
		return std::nullopt;
	}

	// Read exactly contentLength bytes of JSON body
	std::string content(contentLength, '\0');
	size_t totalRead = 0;
	while(totalRead < (size_t)contentLength) {
		size_t read = fread(&content[totalRead], 1, contentLength - totalRead, _input);
		if(read == 0) {
			return std::nullopt;
		}
		totalRead += read;
	}

	return ParseJson(content);
}
