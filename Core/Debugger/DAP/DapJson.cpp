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
#include <new>

JsonValue::JsonValue() : _type(Type::Null) {}

// Private helper: destroy the active union member
void JsonValue::Destroy()
{
	switch(_type) {
		case Type::String:
			_stringValue.~basic_string();
			break;
		case Type::Array:
			using ArrayType = std::vector<JsonValue>;
			_arrayValue.~ArrayType();
			break;
		case Type::Object:
			using ObjectType = std::vector<std::pair<std::string, JsonValue>>;
			_objectValue.~ObjectType();
			break;
		default:
			break;
	}
	_type = Type::Null;
}

JsonValue::JsonValue(const JsonValue& other) : _type(Type::Null)
{
	CopyFrom(other);
}

JsonValue::JsonValue(JsonValue&& other) noexcept : _type(Type::Null)
{
	MoveFrom(std::move(other));
}

JsonValue& JsonValue::operator=(const JsonValue& other)
{
	if(this != &other) {
		Destroy();
		CopyFrom(other);
	}
	return *this;
}

JsonValue& JsonValue::operator=(JsonValue&& other) noexcept
{
	if(this != &other) {
		Destroy();
		MoveFrom(std::move(other));
	}
	return *this;
}

JsonValue::~JsonValue()
{
	Destroy();
}

void JsonValue::CopyFrom(const JsonValue& other)
{
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
			new (&_stringValue) std::string(other._stringValue);
			break;
		case Type::Array:
			new (&_arrayValue) std::vector<JsonValue>(other._arrayValue);
			break;
		case Type::Object:
			new (&_objectValue) std::vector<std::pair<std::string, JsonValue>>(other._objectValue);
			break;
	}
}

void JsonValue::MoveFrom(JsonValue&& other)
{
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
			new (&_stringValue) std::string(std::move(other._stringValue));
			break;
		case Type::Array:
			new (&_arrayValue) std::vector<JsonValue>(std::move(other._arrayValue));
			break;
		case Type::Object:
			new (&_objectValue) std::vector<std::pair<std::string, JsonValue>>(std::move(other._objectValue));
			break;
	}
}

JsonValue JsonValue::MakeNull()
{
	return JsonValue();
}

JsonValue JsonValue::MakeBool(bool value)
{
	JsonValue result;
	result._type = Type::Bool;
	result._boolValue = value;
	return result;
}

JsonValue JsonValue::MakeNumber(double value)
{
	JsonValue result;
	result._type = Type::Number;
	result._numberValue = value;
	return result;
}

JsonValue JsonValue::MakeString(const std::string& value)
{
	JsonValue result;
	result._type = Type::String;
	new (&result._stringValue) std::string(value);
	return result;
}

JsonValue JsonValue::MakeString(std::string&& value)
{
	JsonValue result;
	result._type = Type::String;
	new (&result._stringValue) std::string(std::move(value));
	return result;
}

JsonValue JsonValue::MakeArray()
{
	JsonValue result;
	result._type = Type::Array;
	new (&result._arrayValue) std::vector<JsonValue>();
	return result;
}

JsonValue JsonValue::MakeObject()
{
	JsonValue result;
	result._type = Type::Object;
	new (&result._objectValue) std::vector<std::pair<std::string, JsonValue>>();
	return result;
}

void JsonValue::Set(const std::string& key, JsonValue&& value)
{
	if(_type != Type::Object) {
		Destroy();
		new (&_objectValue) std::vector<std::pair<std::string, JsonValue>>();
		_type = Type::Object;
	}
	_objectValue.emplace_back(key, std::move(value));
}

void JsonValue::Push(JsonValue&& value)
{
	if(_type != Type::Array) {
		Destroy();
		new (&_arrayValue) std::vector<JsonValue>();
		_type = Type::Array;
	}
	_arrayValue.emplace_back(std::move(value));
}

void JsonValue::SetNull()
{
	Destroy();
}

void JsonValue::SetBool(bool value)
{
	Destroy();
	_type = Type::Bool;
	_boolValue = value;
}

void JsonValue::SetNumber(double value)
{
	Destroy();
	_type = Type::Number;
	_numberValue = value;
}

void JsonValue::SetString(const std::string& value)
{
	if(_type == Type::String) {
		_stringValue = value;
	} else {
		Destroy();
		new (&_stringValue) std::string(value);
		_type = Type::String;
	}
}

void JsonValue::SetString(std::string&& value)
{
	if(_type == Type::String) {
		_stringValue = std::move(value);
	} else {
		Destroy();
		new (&_stringValue) std::string(std::move(value));
		_type = Type::String;
	}
}

void JsonValue::SetInt(int value)
{
	Destroy();
	_type = Type::Number;
	_numberValue = (double)value;
}

void JsonValue::SetUint(uint32_t value)
{
	Destroy();
	_type = Type::Number;
	_numberValue = (double)value;
}

JsonValue::Type JsonValue::GetType() const
{
	return _type;
}

const JsonValue& JsonValue::Get(const std::string& key) const
{
	if(_type != Type::Object) {
		static const JsonValue nullValue;
		return nullValue;
	}

	for(const auto& pair : _objectValue) {
		if(pair.first == key) {
			return pair.second;
		}
	}

	static const JsonValue nullValue;
	return nullValue;
}

const JsonValue& JsonValue::operator[](const std::string& key) const
{
	return Get(key);
}

const JsonValue& JsonValue::operator[](size_t index) const
{
	if(_type != Type::Array || index >= _arrayValue.size()) {
		static const JsonValue nullValue;
		return nullValue;
	}
	return _arrayValue[index];
}

int JsonValue::GetInt() const
{
	if(_type == Type::Number) return (int)_numberValue;
	return 0;
}

uint32_t JsonValue::GetUint() const
{
	if(_type == Type::Number) return (uint32_t)_numberValue;
	return 0;
}

bool JsonValue::GetBool() const
{
	if(_type == Type::Bool) return _boolValue;
	return false;
}

double JsonValue::GetNumber() const
{
	if(_type == Type::Number) return _numberValue;
	return 0.0;
}

const std::string& JsonValue::GetString() const
{
	if(_type == Type::String) return _stringValue;
	static const std::string empty;
	return empty;
}

const std::vector<std::pair<std::string, JsonValue>>& JsonValue::GetObject() const
{
	if(_type == Type::Object) return _objectValue;
	static const std::vector<std::pair<std::string, JsonValue>> empty;
	return empty;
}

const std::vector<JsonValue>& JsonValue::GetArray() const
{
	if(_type == Type::Array) return _arrayValue;
	static const std::vector<JsonValue> empty;
	return empty;
}

namespace {
	void EscapeString(const std::string& str, std::string& escaped)
	{
		escaped.clear();
		escaped.reserve(str.size() + 10);

		for(char c : str) {
			switch(c) {
				case '"':  escaped += "\\\""; break;
				case '\\': escaped += "\\\\"; break;
				case '\b': escaped += "\\b";  break;
				case '\f': escaped += "\\f";  break;
				case '\n': escaped += "\\n";  break;
				case '\r': escaped += "\\r";  break;
				case '\t': escaped += "\\t";  break;
				default:   escaped += c;      break;
			}
		}
	}

	void SerializeValue(const JsonValue& value, std::string& result)
	{
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
					double intPart;
					if(std::modf(num, &intPart) == 0.0 && num >= -1e15 && num <= 1e15) {
						// Whole number: serialize without decimal point
						char buf[32];
						snprintf(buf, sizeof(buf), "%.0f", num);
						result += buf;
					} else {
						char buf[32];
						snprintf(buf, sizeof(buf), "%.17g", num);
						result += buf;
					}
				} else {
					result += "null";
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

std::string JsonValue::Serialize() const
{
	std::string result;
	SerializeValue(*this, result);
	return result;
}

bool JsonValue::operator==(const JsonValue& other) const
{
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

	void SkipWhitespace()
	{
		while(_pos < _input.size() && std::isspace(_input[_pos])) {
			_pos++;
		}
	}

	bool ParseString(std::string& result)
	{
		if(_pos >= _input.size() || _input[_pos] != '"') {
			return false;
		}
		_pos++;

		result.clear();
		while(_pos < _input.size()) {
			char c = _input[_pos];
			if(c == '"') {
				_pos++;
				return true;
			} else if(c == '\\') {
				_pos++;
				if(_pos >= _input.size()) return false;
				c = _input[_pos];
				switch(c) {
					case '"':  result += '"';  break;
					case '\\': result += '\\'; break;
					case '/':  result += '/';  break;
					case 'b':  result += '\b'; break;
					case 'f':  result += '\f'; break;
					case 'n':  result += '\n'; break;
					case 'r':  result += '\r'; break;
					case 't':  result += '\t'; break;
					case 'u': {
						// Skip \uXXXX — simplified: skip 4 hex digits
						if(_pos + 4 < _input.size()) {
							_pos += 4;
						}
						break;
					}
					default:
						result += c;
						break;
				}
			} else {
				result += c;
			}
			_pos++;
		}

		return false;
	}

	bool ParseNumber(double& result)
	{
		if(_pos >= _input.size()) return false;

		size_t start = _pos;
		if(_input[_pos] == '-') _pos++;

		while(_pos < _input.size() && std::isdigit(_input[_pos])) {
			_pos++;
		}

		if(_pos < _input.size() && _input[_pos] == '.') {
			_pos++;
			while(_pos < _input.size() && std::isdigit(_input[_pos])) {
				_pos++;
			}
		}

		if(_pos < _input.size() && (_input[_pos] == 'e' || _input[_pos] == 'E')) {
			_pos++;
			if(_pos < _input.size() && (_input[_pos] == '+' || _input[_pos] == '-')) {
				_pos++;
			}
			while(_pos < _input.size() && std::isdigit(_input[_pos])) {
				_pos++;
			}
		}

		if(_pos > start) {
			std::string numberStr = _input.substr(start, _pos - start);
			try {
				result = std::stod(numberStr);
				return true;
			} catch(...) {
				return false;
			}
		}

		return false;
	}

	bool ParseLiteral(const std::string& literal)
	{
		if(_pos + literal.size() > _input.size()) return false;

		for(size_t i = 0; i < literal.size(); i++) {
			if(_input[_pos + i] != literal[i]) {
				return false;
			}
		}

		_pos += literal.size();
		return true;
	}

	bool ParseObject(JsonValue& result)
	{
		if(_pos >= _input.size() || _input[_pos] != '{') {
			return false;
		}
		_pos++;
		SkipWhitespace();

		result = JsonValue::MakeObject();

		if(_pos < _input.size() && _input[_pos] == '}') {
			_pos++;
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
			_pos++;

			SkipWhitespace();

			JsonValue value;
			if(!ParseValue(value)) {
				return false;
			}

			result.Set(key, std::move(value));

			SkipWhitespace();

			if(_pos < _input.size() && _input[_pos] == '}') {
				_pos++;
				return true;
			} else if(_pos < _input.size() && _input[_pos] == ',') {
				_pos++;
				continue;
			} else {
				return false;
			}
		}
	}

	bool ParseArray(JsonValue& result)
	{
		if(_pos >= _input.size() || _input[_pos] != '[') {
			return false;
		}
		_pos++;
		SkipWhitespace();

		if(_pos < _input.size() && _input[_pos] == ']') {
			_pos++;
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
				_pos++;
				return true;
			} else if(_pos < _input.size() && _input[_pos] == ',') {
				_pos++;
				continue;
			} else {
				return false;
			}
		}
	}

	bool ParseValue(JsonValue& result)
	{
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
	static std::optional<JsonValue> Parse(const std::string& input)
	{
		JsonParser parser(input);
		JsonValue result;
		if(parser.ParseValue(result)) {
			return result;
		}
		return std::nullopt;
	}
};

}

std::optional<JsonValue> ParseJson(const std::string& input)
{
	return JsonParser::Parse(input);
}
