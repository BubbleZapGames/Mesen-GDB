#include "cli_platform.h"
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fcntl.h>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

FILE* CliPlatform::TakeStdout()
{
	fflush(stdout);
#ifdef _WIN32
	int fd = _dup(_fileno(stdout));
	if(fd < 0) return nullptr;
	_dup2(_fileno(stderr), _fileno(stdout));
	return _fdopen(fd, "wb");
#else
	int fd = dup(fileno(stdout));
	if(fd < 0) return nullptr;
	dup2(fileno(stderr), fileno(stdout));
	return fdopen(fd, "w");
#endif
}

bool CliPlatform::StdinFromFile(const std::string& path)
{
#ifdef _WIN32
	int fd = _open(path.c_str(), _O_RDONLY | _O_BINARY);
	if(fd < 0) return false;
	_dup2(fd, 0);
	_close(fd);
#else
	int fd = open(path.c_str(), O_RDONLY);
	if(fd < 0) return false;
	dup2(fd, STDIN_FILENO);
	close(fd);
#endif
	return true;
}

bool CliPlatform::StdinIsTerminal()
{
#ifdef _WIN32
	return _isatty(0) != 0;
#else
	return isatty(STDIN_FILENO) != 0;
#endif
}

void CliPlatform::UseBinaryStdio()
{
#ifdef _WIN32
	// A console stays in text mode: it is a person typing, and binary mode
	// would turn Ctrl+Z into a character instead of EOF.
	if(!_isatty(0)) _setmode(0, _O_BINARY);
	if(!_isatty(1)) _setmode(1, _O_BINARY);
#endif
}

std::string CliPlatform::MesenHome()
{
	const char* home = getenv("MESEN_HOME");
	if(!home) home = getenv("HOME");
#ifdef _WIN32
	if(!home) home = getenv("USERPROFILE");
#endif
	std::string base = home ? home : std::filesystem::temp_directory_path().string();
	return base + "/.mesen-dap";
}

struct StdinLines::State
{
	std::mutex lock;
	std::condition_variable ready;
	std::deque<std::string> lines;
	bool ended = false;
};

// Read stdin into lines until EOF or an error, which both end the stream.
void StdinLines::Read(std::shared_ptr<State> state)
{
	std::string partial;
	char buf[4096];
	while(true) {
#ifdef _WIN32
		int n = _read(0, buf, sizeof(buf));
#else
		ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
#endif
		if(n < 0 && errno == EINTR) continue;
		std::lock_guard<std::mutex> guard(state->lock);
		if(n <= 0) {
			if(!partial.empty()) state->lines.push_back(partial);
			state->ended = true;
			state->ready.notify_all();
			return;
		}
		partial.append(buf, (size_t)n);
		size_t start = 0;
		size_t nl;
		while((nl = partial.find('\n', start)) != std::string::npos) {
			size_t end = nl > start && partial[nl - 1] == '\r' ? nl - 1 : nl;
			state->lines.push_back(partial.substr(start, end - start));
			start = nl + 1;
		}
		partial.erase(0, start);
		state->ready.notify_all();
	}
}

StdinLines::Status StdinLines::Next(std::string& line, int timeoutMs)
{
	if(!_state) {
		_state = std::make_shared<State>();
		std::thread(Read, _state).detach();
	}
	std::unique_lock<std::mutex> guard(_state->lock);
	_state->ready.wait_for(guard, std::chrono::milliseconds(timeoutMs),
		[this] { return !_state->lines.empty() || _state->ended; });
	if(!_state->lines.empty()) {
		line = std::move(_state->lines.front());
		_state->lines.pop_front();
		return Status::Line;
	}
	return _state->ended ? Status::End : Status::Timeout;
}
