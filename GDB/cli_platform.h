#pragma once
#include <cstdio>
#include <memory>
#include <string>

// What the CLI needs from the OS that POSIX and Windows spell differently:
// the stdio descriptors, the home folder, and a stdin that can be waited on.
namespace CliPlatform {
	// Point fd 1 at stderr and return a stream on the real stdout, so the
	// emulator's chatter cannot corrupt a reply stream. nullptr on failure.
	FILE* TakeStdout();

	// Make the file the process's stdin (--script). False if it cannot be read.
	bool StdinFromFile(const std::string& path);

	bool StdinIsTerminal();

	// On Windows, binary mode for a stdin or stdout that is not a console:
	// text mode turns "\n" into "\r\n", which breaks DAP's Content-Length
	// framing. Nothing elsewhere.
	void UseBinaryStdio();

	// Mesen's home folder: .mesen-gdb under $MESEN_HOME, else the user's home.
	std::string MesenHome();
}

// StdinLines - stdin as lines, read by a thread of its own so the caller can
// wait with a timeout; poll() cannot wait on a Windows pipe or console. The
// thread reads the descriptor directly: std::cin's buffer would hold lines
// back from a client that keeps the pipe open and sends one at a time. It
// starts on the first Next(), after --script has replaced stdin, and outlives
// this object, blocked in a read until EOF or the process exits.
class StdinLines
{
public:
	enum class Status { Line, Timeout, End };

	// The next line, without its "\n" or "\r\n". At EOF, a last line without
	// its newline still counts; End follows it.
	Status Next(std::string& line, int timeoutMs);

private:
	struct State;
	std::shared_ptr<State> _state;

	static void Read(std::shared_ptr<State> state);
};
