#pragma once

#include <fstream>
#include <filesystem>

namespace utf8 {
	class utf8
	{
	public:
		static std::wstring decode(const std::string &str);
		static std::string encode(const std::wstring &wstr);
		static std::string encode(const std::u16string &wstr);
	};
		
#ifdef _WIN32
	// Paths are UTF-8; u8path opens them through the wide Windows API. The
	// destructors are out of line, so UTF8Util.cpp alone emits the vtables:
	// MinGW's LTO link rejects a copy from every user.
	class ifstream : public std::ifstream
	{
	public:
		ifstream(const std::string& path, ios_base::openmode mode = ios_base::in) : std::ifstream(std::filesystem::u8path(path), mode) { }
		ifstream() : std::ifstream() { }
		~ifstream() override;
		void open(const std::string& path, ios_base::openmode mode = ios_base::in) { std::ifstream::open(std::filesystem::u8path(path), mode); }
	};

	class ofstream : public std::ofstream
	{
	public:
		ofstream(const std::string& path, ios_base::openmode mode = ios_base::out) : std::ofstream(std::filesystem::u8path(path), mode) { }
		ofstream() : std::ofstream() { }
		~ofstream() override;
		void open(const std::string& path, ios_base::openmode mode = ios_base::out) { std::ofstream::open(std::filesystem::u8path(path), mode); }
	};
#else
	using std::ifstream;
	using std::ofstream;
#endif
}