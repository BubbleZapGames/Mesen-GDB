#include "pch.h"

#include "Shared/RecordedRomTest.h"
#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"
#include "Shared/MessageManager.h"
#include "Shared/NotificationManager.h"
#include "Utilities/VirtualFile.h"
#include "Utilities/FolderUtilities.h"
#include "Utilities/md5.h"

RecordedRomTest::RecordedRomTest(Emulator* emu, bool inBackground)
{
	_emu = emu;
	_inBackground = inBackground;
	Reset();
}

RecordedRomTest::~RecordedRomTest()
{
	Reset();
}

void RecordedRomTest::SaveFrame()
{
	PpuFrameInfo frame = _emu->GetPpuFrame();

	uint8_t md5Hash[16];
	GetMd5Sum(md5Hash, frame.FrameBuffer, frame.FrameBufferSize);

	if(memcmp(_previousHash, md5Hash, 16) == 0 && _currentCount < 255) {
		_currentCount++;
	} else {
		uint8_t* hash = new uint8_t[16];
		memcpy(hash, md5Hash, 16);
		_screenshotHashes.push_back(hash);
		if(_currentCount > 0) {
			_repetitionCount.push_back(_currentCount);
		}
		_currentCount = 1;

		memcpy(_previousHash, md5Hash, 16);

		_signal.Signal();
	}
}

void RecordedRomTest::ValidateFrame()
{
	PpuFrameInfo frame = _emu->GetPpuFrame();

	uint8_t md5Hash[16];
	GetMd5Sum(md5Hash, frame.FrameBuffer, frame.FrameBufferSize);

	if(_currentCount == 0) {
		_currentCount = _repetitionCount.front();
		_repetitionCount.pop_front();
		_screenshotHashes.pop_front();
	}
	_currentCount--;

	if(memcmp(_screenshotHashes.front(), md5Hash, 16) != 0) {
		_badFrameCount++;
		_isLastFrameGood = false;
		//_console->BreakIfDebugging();
	} else {
		_isLastFrameGood = true;
	}
	
	if(_currentCount == 0 && _repetitionCount.empty()) {
		//End of test
		_runningTest = false;
		_signal.Signal();
	}
}

void RecordedRomTest::ProcessNotification(ConsoleNotificationType type, void* parameter)
{
	switch(type) {
		case ConsoleNotificationType::PpuFrameDone:
			if(_recording) {
				SaveFrame();
			} else if(_runningTest) {
				ValidateFrame();
			}
			break;
		
		default:
			break;
	}
}

void RecordedRomTest::Reset()
{
	memset(_previousHash, 0xFF, 16);
	
	_currentCount = 0;
	_repetitionCount.clear();

	for(uint8_t* hash : _screenshotHashes) {
		delete[] hash;
	}
	_screenshotHashes.clear();

	_runningTest = false;
	_recording = false;
	_badFrameCount = 0;
}

void RecordedRomTest::Record(string filename, bool reset)
{
	_emu->GetNotificationManager()->RegisterNotificationListener(shared_from_this());
	_filename = filename;

	string mrtFilename = FolderUtilities::CombinePath(FolderUtilities::GetFolderName(filename), FolderUtilities::GetFilename(filename, false) + ".mrt");
	_file.open(mrtFilename, ios::out | ios::binary);

	if(_file) {
		_emu->Lock();
		Reset();

		EmuSettings* settings = _emu->GetSettings();
		settings->GetSnesConfig().RamPowerOnState = RamState::AllZeros;
		settings->GetNesConfig().RamPowerOnState = RamState::AllZeros;
		settings->GetGameboyConfig().RamPowerOnState = RamState::AllZeros;
		settings->GetPcEngineConfig().RamPowerOnState = RamState::AllZeros;
		settings->GetSmsConfig().RamPowerOnState = RamState::AllZeros;
		settings->GetCvConfig().RamPowerOnState = RamState::AllZeros;
		settings->GetGbaConfig().RamPowerOnState = RamState::AllZeros;

		settings->GetSnesConfig().DisableFrameSkipping = true;
		settings->GetPcEngineConfig().DisableFrameSkipping = true;
		settings->GetGbaConfig().DisableFrameSkipping = true;

		settings->GetGbaConfig().SkipBootScreen = false;
		settings->GetWsConfig().UseBootRom = true;
		settings->GetWsConfig().LcdShowIcons = true;
				
		_recording = true;
		_emu->Unlock();
	}
}

RomTestResult RecordedRomTest::Run(string filename)
{
	RomTestResult result = {};
	result.ErrorCode = -1;
	return result;
}

void RecordedRomTest::Stop()
{
	if(_recording) {
		Save();
	}
	Reset();
}

void RecordedRomTest::Save()
{
}