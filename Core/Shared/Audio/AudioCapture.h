#pragma once
#include "pch.h"
#include <atomic>
#include <mutex>

// AudioCapture - the debugger's `record` command: the console's own audio,
// as the sound chip produced it (before Mesen's resampler, equalizer, reverb
// and volume), written to a 16-bit stereo WAV at the chip's native rate.
//
// It can also keep a log of sound-chip register writes (the SNES DSP's,
// from Spc), each stamped with the index of the WAV sample frame it lands
// on, so a click in the WAV can be traced to the write that caused it.
class AudioCapture
{
private:
	std::mutex _lock;
	FILE* _wav = nullptr;
	FILE* _log = nullptr;
	std::atomic<bool> _logging = false;
	uint64_t _frames = 0;      // sample frames written to the WAV
	uint32_t _rate = 0;        // the source rate, from the first buffer

	void WriteHeader();

public:
	~AudioCapture();

	// Start - open *wavPath*, and *logPath* too unless it is empty. False if
	// either cannot be written (nothing is left open then).
	bool Start(const string& wavPath, const string& logPath);
	// Stop - finish the WAV's header and close both. Returns the frames written.
	uint64_t Stop();
	bool IsRecording();
	uint32_t GetRate();

	// AddSamples - *count* interleaved stereo frames at *rate*.
	void AddSamples(const int16_t* samples, uint32_t count, uint32_t rate);
	// IsLogging - cheap, for the register write path.
	bool IsLogging() { return _logging; }
	// LogWrite - register *reg* set to *value* by code at *pc*, *pending*
	// frames into the buffer the chip has not handed over yet.
	void LogWrite(uint32_t pending, uint32_t pc, uint8_t reg, uint8_t value);
};
