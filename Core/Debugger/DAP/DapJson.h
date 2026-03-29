#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <utility>

class JsonValue {
public:
	enum class Type {
		Null,
		Bool,
		Number,
		String,
		Array,
		Object
	};

private:
	Type _type;
	
	union {
		bool _boolValue;
		double _numberValue;
		std::string _stringValue;
		std::vector<std::pair<std::string, JsonValue>> _objectValue;
		std::vector<JsonValue> _arrayValue;
	};

public:
	JsonValue();
	JsonValue(const JsonValue& other);
	JsonValue(JsonValue&& other) noexcept;
	JsonValue& operator=(const JsonValue& other);
	JsonValue& operator=(JsonValue&& other) noexcept;
	~JsonValue();

	// Constructors
	static JsonValue MakeNull();
	static JsonValue MakeBool(bool value);
	static JsonValue MakeNumber(double value);
	static JsonValue MakeString(const std::string& value);
	static JsonValue MakeString(std::string&& value);
	static JsonValue MakeArray();
	static JsonValue MakeObject();

	// Mutators
	void Set(const std::string& key, JsonValue&& value);
	void Push(JsonValue&& value);
	void SetNull();
	void SetBool(bool value);
	void SetNumber(double value);
	void SetString(const std::string& value);
	void SetString(std::string&& value);
	void SetInt(int value);
	void SetUint(uint32_t value);

	// Accessors
	Type GetType() const;
	const JsonValue& Get(const std::string& key) const;
	const JsonValue& operator[](const std::string& key) const;
	const JsonValue& operator[](size_t index) const;
	int GetInt() const;
	uint32_t GetUint() const;
	bool GetBool() const;
	double GetNumber() const;
	const std::string& GetString() const;
	const std::vector<std::pair<std::string, JsonValue>>& GetObject() const;
	const std::vector<JsonValue>& GetArray() const;

	// Serialization
	std::string Serialize() const;

	// For comparison
	bool operator==(const JsonValue& other) const;

private:
	void Destroy();
	void CopyFrom(const JsonValue& other);
	void MoveFrom(JsonValue&& other);
};

std::optional<JsonValue> ParseJson(const std::string& input);