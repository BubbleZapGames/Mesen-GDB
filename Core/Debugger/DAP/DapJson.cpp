#include "DapJson.h"
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <utility>
#include <cctype>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <cstdio>

JsonValue::JsonValue() : _type(Type::Null) {
	// Default constructor for null value
}

JsonValue::JsonValue(const JsonValue& other) : _type(other._type) {
	switch(_type) {
		case Type::Null:
			break;
		case Type::Bool:
			_boolValue = other._boolValue;
			break;
		case Type::Number:
			_numberValue = other._numberValue;
			break;
		case Type::String:
			_stringValue = other._stringValue;
			break;
		case Type::Array:
			_arrayValue = other._arrayValue;
			break;
		case Type::Object:
			_objectValue = other._objectValue;
			break;
	}
}

JsonValue::JsonValue(JsonValue&& other) noexcept : _type(other._type) {
	switch(_type) {
		case Type::Null:
			break;
		case Type::Bool:
			_boolValue = other._boolValue;
			break;
		case Type::Number:
			_numberValue = other._numberValue;
			break;
		case Type::String:
			_stringValue = std::move(other._stringValue);
			break;
		case Type::Array:
			_arrayValue = std::move(other._arrayValue);
			break;
		case Type::Object:
			_objectValue = std::move(other._objectValue);
			break;
	}
}

JsonValue& JsonValue::operator=(const JsonValue& other) {
	if(this != &other) {
		_type = other._type;
		switch(_type) {
			case Type::Null:
				break;
			case Type::Bool:
				_boolValue = other._boolValue;
				break;
			case Type::Number:
				_numberValue = other._numberValue;
				break;
			case Type::String:
				_stringValue = other._stringValue;
				break;
			case Type::Array:
				_arrayValue = other._arrayValue;
				break;
			case Type::Object:
				_objectValue = other._objectValue;
				break;
		}
	}
	return *this;
}

JsonValue& JsonValue::operator=(JsonValue&& other) noexcept {
	if(this != &other) {
		_type = other._type;
		switch(_type) {
			case Type::Null:
				break;
			case Type::Bool:
				_boolValue = other._boolValue;
				break;
			case Type::Number:
				_numberValue = other._numberValue;
				break;
			case Type::String:
				_stringValue = std::move(other._stringValue);
				break;
			case Type::Array:
				_arrayValue = std::move(other._arrayValue);
				break;
			case Type::Object:
				_objectValue = std::move(other._objectValue);
				break;
		}
	}
	return *this;
}

JsonValue::~JsonValue() {
	// Destructor is needed to properly clean up the union
	// It's already handled by the compiler because all members are trivially destructible except strings.
	// This is a placeholder.
}

JsonValue JsonValue::MakeNull() {
	return JsonValue();
}

JsonValue JsonValue::MakeBool(bool value) {
	JsonValue result;
	result._type = Type::Bool;
	result._boolValue = value;
	return result;
}

JsonValue JsonValue::MakeNumber(double value) {
	JsonValue result;
	result._type = Type::Number;
	result._numberValue = value;
	return result;
}

JsonValue JsonValue::MakeString(const std::string& value) {
	JsonValue result;
	result._type = Type::String;
	result._stringValue = value;
	return result;
}

JsonValue JsonValue::MakeString(std::string&& value) {
	JsonValue result;
	result._type = Type::String;
	result._stringValue = std::move(value);
	return result;
}

JsonValue JsonValue::MakeArray() {
	JsonValue result;
	result._type = Type::Array;
	return result;
}

JsonValue JsonValue::MakeObject() {
	JsonValue result;
	result._type = Type::Object;
	return result;
}

void JsonValue::Set(const std::string& key, JsonValue&& value) {
	if(_type != Type::Object) {
		// If we're changing type, create an object
		_type = Type::Object;
		_objectValue.clear();
	}
	_objectValue.emplace_back(key, std::move(value));
}

void JsonValue::Push(JsonValue&& value) {
	if(_type != Type::Array) {
		_type = Type::Array;
		_arrayValue.clear();
	}
	_arrayValue.emplace_back(std::move(value));
}

void JsonValue::SetNull() {
	_type = Type::Null;
}

void JsonValue::SetBool(bool value) {
	_type = Type::Bool;
	_boolValue = value;
}

void JsonValue::SetNumber(double value) {
	_type = Type::Number;
	_numberValue = value;
}

void JsonValue::SetString(const std::string& value) {
	_type = Type::String;
	_stringValue = value;
}

void JsonValue::SetString(std::string&& value) {
	_type = Type::String;
	_stringValue = std::move(value);
}

JsonValue::Type JsonValue::GetType() const {
	return _type;
}

const JsonValue& JsonValue::Get(const std::string& key) const {
	if(_type != Type::Object) {
		// Return empty null value
		static const JsonValue nullValue = JsonValue::MakeNull();
		return nullValue;
	}
	
	for(const auto& pair : _objectValue) {
		if(pair.first == key) {
			return pair.second;
		}
	}
	
	// Return empty null value if key not found
	static const JsonValue nullValue = JsonValue::MakeNull();
	return nullValue;
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
	return Get(key);
}

const JsonValue& JsonValue::operator[](size_t index) const {
	if(_type != Type::Array || index >= _arrayValue.size()) {
		// Return empty null value
		static const JsonValue nullValue = JsonValue::MakeNull();
		return nullValue;
	}
	return _arrayValue[index];
}

bool JsonValue::GetBool() const {
	if(_type == Type::Bool) return _boolValue;
	return false;
}

double JsonValue::GetNumber() const {
	if(_type == Type::Number) return _numberValue;
	return 0.0;
}

const std::string& JsonValue::GetString() const {
	if(_type == Type::String) return _stringValue;
	// Return empty string for non-string types
	static const std::string empty = "";
	return empty;
}

const std::vector<std::pair<std::string, JsonValue>>& JsonValue::GetObject() const {
	if(_type == Type::Object) return _objectValue;
	// Return empty vector for non-object types
	static const std::vector<std::pair<std::string, JsonValue>> empty = {};
	return empty;
}

const std::vector<JsonValue>& JsonValue::GetArray() const {
	if(_type == Type::Array) return _arrayValue;
	// Return empty vector for non-array types
	static const std::vector<JsonValue> empty = {};
	return empty;
}

namespace {
	void EscapeString(const std::string& str, std::string& escaped) {
		escaped.clear();
		escaped.reserve(str.size() + 10);  // Reserve some space for escapes
		
		for(char c : str) {
			switch(c) {
				case '"':
					escaped += "\\\"";
					break;
				case '\\':
					escaped += "\\\\";
					break;
				case '/':
					escaped += "\\/";
					break;
				case '\b':
					escaped += "\\b";
					break;
				case '\f':
					escaped += "\\f";
					break;
				case '\n':
					escaped += "\\n";
					break;
				case '\r':
					escaped += "\\r";
					break;
				case '\t':
					escaped += "\\t";
					break;
				default:
					escaped += c;
					break;
			}
		}
	}
	
	void SerializeValue(const JsonValue& value, std::string& result) {
		switch(value.GetType()) {
			case JsonValue::Type::Null:
				result += "null";
				break;
			case JsonValue::Type::Bool:
				result += value.GetBool() ? "true" : "false";
				break;
			case JsonValue::Type::Number:
			{
				double num = value.GetNumber();
				if(std::isfinite(num)) {
					std::ostringstream oss;
					oss.precision(17);  // Enough precision to represent all double values
					oss << num;
					result += oss.str();
				} else {
					result += "null";   // Non-finite numbers get serialized as null
				}
				break;
			}
			case JsonValue::Type::String:
			{
				std::string escaped;
				EscapeString(value.GetString(), escaped);
				result += '"';
				result += escaped;
				result += '"';
				break;
			}
			case JsonValue::Type::Array:
			{
				result += '[';
				const auto& array = value.GetArray();
				for(size_t i = 0; i < array.size(); i++) {
					if(i > 0) result += ',';
					SerializeValue(array[i], result);
				}
				result += ']';
				break;
			}
			case JsonValue::Type::Object:
			{
				result += '{';
				const auto& object = value.GetObject();
				for(size_t i = 0; i < object.size(); i++) {
					if(i > 0) result += ',';
					std::string escaped;
					EscapeString(object[i].first, escaped);
					result += '"';
					result += escaped;
					result += "\":";
					SerializeValue(object[i].second, result);
				}
				result += '}';
				break;
			}
		}
	}
}

std::string JsonValue::Serialize() const {
	std::string result;
	SerializeValue(*this, result);
	return result;
}

bool JsonValue::operator==(const JsonValue& other) const {
	if(_type != other._type) return false;
	
	switch(_type) {
		case Type::Null:
			return true;
		case Type::Bool:
			return _boolValue == other._boolValue;
		case Type::Number:
			return _numberValue == other._numberValue;
		case Type::String:
			return _stringValue == other._stringValue;
		case Type::Array:
			return _arrayValue == other._arrayValue;
		case Type::Object:
			return _objectValue == other._objectValue;
	}
	return false;
}

namespace {
	
class JsonParser {
private:
	const std::string& _input;
	size_t _pos;
	
public:
	JsonParser(const std::string& input) : _input(input), _pos(0) {}
	
	void SkipWhitespace() {
		while(_pos < _input.size() && std::isspace(_input[_pos])) {
			_pos++;
		}
	}
	
	bool ParseString(std::string& result) {
		if(_pos >= _input.size() || _input[_pos] != '"') {
			return false;
		}
		_pos++;  // consume opening quote
		
		result.clear();
		while(_pos < _input.size()) {
			char c = _input[_pos];
			if(c == '"') {
				_pos++;  // consume closing quote
				return true;
			} else if(c == '\\') {
				_pos++;  // consume backslash
				if(_pos >= _input.size()) return false;
				c = _input[_pos];
				switch(c) {
					case '"': result += '"'; break;
					case '\\': result += '\\'; break;
					case '/': result += '/'; break;
					case 'b': result += '\b'; break;
					case 'f': result += '\f'; break;
					case 'n': result += '\n'; break;
					case 'r': result += '\r'; break;
					case 't': result += '\t'; break;
					case 'u': {
						// Handle \uXXXX - for simplicity, we're not actually parsing it here
						// But we still need to skip 4 characters
						_pos += 4;
						break;
					}
					default:
						result += c;  // Default to literal character for unknown escapes
						break;
				}
			} else {
				result += c;
			}
			_pos++;
		}
		
		return false;  // Unterminated string
	}
	
	bool ParseNumber(double& result) {
		if(_pos >= _input.size()) return false;
		
		size_t start = _pos;
		if(_input[_pos] == '-') _pos++;
		
		while(_pos < _input.size() && (std::isdigit(_input[_pos]) || _input[_pos] == '.')) {
			_pos++;
		}
		
		if(_pos > start) {
			// Extract substring for conversion
			std::string numberStr = _input.substr(start, _pos - start);
			try {
				result = std::stod(numberStr);
				return true;
			} catch (...) {
				return false;
			}
		}
		
		return false;
	}
	
	bool ParseLiteral(const std::string& literal) {
		if(_pos + literal.size() > _input.size()) return false;
		
		for(size_t i = 0; i < literal.size(); i++) {
			if(_input[_pos + i] != literal[i]) {
				return false;
			}
		}
		
		_pos += literal.size();
		return true;
	}
	
	bool ParseObject(JsonValue& result) {
		if(_pos >= _input.size() || _input[_pos] != '{') {
			return false;
		}
		_pos++;  // consume '{'
		SkipWhitespace();
		
		result = JsonValue::MakeObject();
		
		// Handle empty object
		if(_pos < _input.size() && _input[_pos] == '}') {
			_pos++;  // consume '}'
			return true;
		}
		
		while(true) {
			SkipWhitespace();
			
			std::string key;
			if(!ParseString(key)) {
				return false;
			}
			
			SkipWhitespace();
			
			if(_pos >= _input.size() || _input[_pos] != ':') {
				return false;
			}
			_pos++;  // consume ':'
			
			SkipWhitespace();
			
			JsonValue value;
			if(!ParseValue(value)) {
				return false;
			}
			
			result.Set(key, std::move(value));
			
			SkipWhitespace();
			
			if(_pos < _input.size() && _input[_pos] == '}') {
				_pos++;  // consume '}'
				return true;
			} else if(_pos < _input.size() && _input[_pos] == ',') {
				_pos++;  // consume ','
				continue;  // More key-value pairs
			} else {
				return false;  // No closing brace or comma
			}
		}
	}
	
	bool ParseArray(JsonValue& result) {
		if(_pos >= _input.size() || _input[_pos] != '[') {
			return false;
		}
		_pos++;  // consume '['
		SkipWhitespace();
		
		// Handle empty array
		if(_pos < _input.size() && _input[_pos] == ']') {
			_pos++;  // consume ']'
			result = JsonValue::MakeArray();
			return true;
		}
		
		result = JsonValue::MakeArray();
		
		while(true) {
			SkipWhitespace();
			
			JsonValue value;
			if(!ParseValue(value)) {
				return false;
			}
			
			result.Push(std::move(value));
			
			SkipWhitespace();
			
			if(_pos < _input.size() && _input[_pos] == ']') {
				_pos++;  // consume ']'
				return true;
			} else if(_pos < _input.size() && _input[_pos] == ',') {
				_pos++;  // consume ','
				continue;  // More values
			} else {
				return false;  // No closing bracket or comma
			}
		}
	}
	
	bool ParseValue(JsonValue& result) {
		SkipWhitespace();
		
		if(_pos >= _input.size()) {
			return false;
		}
		
		char c = _input[_pos];
		switch(c) {
			case '{':
				return ParseObject(result);
			case '[':
				return ParseArray(result);
			case '"':
				{
					std::string str;
					if(!ParseString(str)) {
						return false;
					}
					result = JsonValue::MakeString(std::move(str));
					return true;
				}
			case 't':
				if(ParseLiteral("true")) {
					result = JsonValue::MakeBool(true);
					return true;
				}
				return false;
			case 'f':
				if(ParseLiteral("false")) {
					result = JsonValue::MakeBool(false);
					return true;
				}
				return false;
			case 'n':
				if(ParseLiteral("null")) {
					result = JsonValue::MakeNull();
					return true;
				}
				return false;
			default:
				if(std::isdigit(c) || c == '-') {
					double num;
					if(ParseNumber(num)) {
						result = JsonValue::MakeNumber(num);
						return true;
					}
				}
				return false;
		}
	}
	
public:
	static std::optional<JsonValue> Parse(const std::string& input) {
		JsonParser parser(input);
		JsonValue result;
		if(parser.ParseValue(result)) {
			return result;
		}
		return std::nullopt;
	}
};

}

std::optional<JsonValue> ParseJson(const std::string& input) {
	return JsonParser::Parse(input);
}
