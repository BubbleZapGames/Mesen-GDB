#include "pch.h"
#include "Shared/Audio/AudioCapture.h"

AudioCapture::~AudioCapture()
{
	Stop();
}

static void Put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void Put32(uint8_t* p, uint32_t v) { Put16(p, (uint16_t)v); Put16(p + 2, (uint16_t)(v >> 16)); }

// WriteHeader - the 44-byte canonical WAV header, for the frames so far.
// Written once at the start (as a placeholder) and again by Stop.
void AudioCapture::WriteHeader()
{
	uint32_t dataBytes = (uint32_t)std::min<uint64_t>(_frames * 4, 0xFFFFFFFF - 36);
	uint8_t h[44] = {};
	memcpy(h, "RIFF", 4);
	Put32(h + 4, 36 + dataBytes);
	memcpy(h + 8, "WAVEfmt ", 8);
	Put32(h + 16, 16);           // fmt chunk size
	Put16(h + 20, 1);            // PCM
	Put16(h + 22, 2);            // stereo
	Put32(h + 24, _rate);
	Put32(h + 28, _rate * 4);    // bytes a second
	Put16(h + 32, 4);            // bytes a frame
	Put16(h + 34, 16);           // bits a sample
	memcpy(h + 36, "data", 4);
	Put32(h + 40, dataBytes);
	fseek(_wav, 0, SEEK_SET);
	fwrite(h, 1, sizeof(h), _wav);
	fseek(_wav, 0, SEEK_END);
}

bool AudioCapture::Start(const string& wavPath, const string& logPath)
{
	Stop();
	std::lock_guard<std::mutex> guard(_lock);
	_wav = fopen(wavPath.c_str(), "wb");
	if(!_wav) return false;
	if(!logPath.empty()) {
		_log = fopen(logPath.c_str(), "w");
		if(!_log) {
			fclose(_wav);
			_wav = nullptr;
			return false;
		}
		fprintf(_log, "# frame  reg value  pc    (frame = sample frame in %s)\n", wavPath.c_str());
	}
	_frames = 0;
	_rate = 0;
	WriteHeader();
	_logging = _log != nullptr;
	return true;
}

uint64_t AudioCapture::Stop()
{
	std::lock_guard<std::mutex> guard(_lock);
	_logging = false;
	uint64_t frames = _frames;
	if(_wav) {
		WriteHeader();
		fclose(_wav);
		_wav = nullptr;
	}
	if(_log) {
		fclose(_log);
		_log = nullptr;
	}
	return frames;
}

bool AudioCapture::IsRecording()
{
	std::lock_guard<std::mutex> guard(_lock);
	return _wav != nullptr;
}

uint32_t AudioCapture::GetRate()
{
	std::lock_guard<std::mutex> guard(_lock);
	return _rate;
}

void AudioCapture::AddSamples(const int16_t* samples, uint32_t count, uint32_t rate)
{
	std::lock_guard<std::mutex> guard(_lock);
	if(!_wav) return;
	if(_rate == 0) _rate = rate;
	// Little-endian on every platform Mesen builds for, so the samples go
	// out as they are.
	fwrite(samples, 4, count, _wav);
	_frames += count;
}

void AudioCapture::LogWrite(uint32_t pending, uint32_t pc, uint8_t reg, uint8_t value)
{
	std::lock_guard<std::mutex> guard(_lock);
	if(!_log) return;
	fprintf(_log, "%llu %02X %02X %04X\n", (unsigned long long)(_frames + pending), reg, value, pc);
}
