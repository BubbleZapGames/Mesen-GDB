#pragma once
#include "pch.h"

class IBatteryProvider
{
public:
	virtual vector<uint8_t> LoadBattery(string extension) = 0;
};

class BatteryManager
{
private:
	string _romName;
	bool _hasBattery = false;

	std::weak_ptr<IBatteryProvider> _provider;

	string GetBasePath(string& extension);

public:
	void Initialize(string romName, bool setBatteryFlag = false);

	bool HasBattery() { return _hasBattery; }

	void SetBatteryProvider(shared_ptr<IBatteryProvider> provider);
	
	void SaveBattery(string extension, uint8_t* data, uint32_t length);
	
	vector<uint8_t> LoadBattery(string extension);
	void LoadBattery(string extension, uint8_t* data, uint32_t length);
	uint32_t GetBatteryFileSize(string extension);
};