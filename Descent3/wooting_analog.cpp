/*
 * Descent 3
 * Copyright (C) 2024 Parallax Software
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "wooting_analog.h"

#include "gameloop.h"
#include "mono.h"

#include <algorithm>
#include <stdint.h>
#include <string.h>

#if defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <string>
#endif

bool Wooting_analog_enabled = false;

namespace
{
constexpr int kMaximumAnalogKeys = 256;
constexpr float kAnalogDeadzone = 0.02f;
constexpr unsigned int kScanCodeSet1Mode = 1;

uint16_t Analog_codes[kMaximumAnalogKeys] = {};
float Analog_values[kMaximumAnalogKeys] = {};
int Analog_key_count = 0;
bool Analog_sample_valid = false;

#if defined(_WIN64)
using WootingInitialise = int (__cdecl *)();
using WootingUninitialise = int (__cdecl *)();
using WootingSetKeycodeMode = int (__cdecl *)(unsigned int);
using WootingReadFullBuffer = int (__cdecl *)(unsigned short *, float *, unsigned int);
using WootingVersionSemver = const char *(__cdecl *)();

HMODULE Wooting_module = NULL;
WootingInitialise Wooting_initialise = NULL;
WootingUninitialise Wooting_uninitialise = NULL;
WootingSetKeycodeMode Wooting_set_keycode_mode = NULL;
WootingReadFullBuffer Wooting_read_full_buffer = NULL;
WootingVersionSemver Wooting_version_semver = NULL;
bool Wooting_load_attempted = false;
bool Wooting_initialized = false;
bool Wooting_poll_logged = false;
int Wooting_last_poll_error = 0;

std::wstring WootingApplicationDirectory()
{
	wchar_t path[MAX_PATH];
	const DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
	if (!length || length >= MAX_PATH)
		return std::wstring();

	std::wstring directory(path, length);
	const std::wstring::size_type separator = directory.find_last_of(L"\\/");
	if (separator == std::wstring::npos)
		return std::wstring();
	directory.resize(separator + 1);
	return directory;
}

HMODULE WootingLoadLibraryFrom(const std::wstring& directory, const wchar_t *name)
{
	if (directory.empty())
		return NULL;
	return LoadLibraryW((directory + name).c_str());
}

bool WootingLoadSdk()
{
	if (Wooting_module)
		return true;
	if (Wooting_load_attempted)
		return false;
	Wooting_load_attempted = true;

	const std::wstring application_directory = WootingApplicationDirectory();
	Wooting_module = WootingLoadLibraryFrom(application_directory,
		L"wooting_analog_sdk_dist.dll");
	if (!Wooting_module)
		Wooting_module = WootingLoadLibraryFrom(application_directory,
			L"wooting_analog_sdk.dll");

	if (!Wooting_module) {
		wchar_t program_files[MAX_PATH];
		const DWORD length = GetEnvironmentVariableW(L"ProgramFiles",
			program_files, MAX_PATH);
		if (length && length < MAX_PATH) {
			std::wstring sdk_directory(program_files, length);
			if (!sdk_directory.empty() && sdk_directory.back() != L'\\')
				sdk_directory += L'\\';
			sdk_directory += L"wooting-analog-sdk\\";
			Wooting_module = WootingLoadLibraryFrom(sdk_directory,
				L"wooting_analog_sdk.dll");
		}
	}

	if (!Wooting_module) {
		mprintf((0, "Wooting Analog SDK is not installed; using digital keyboard input.\n"));
		AutomatedCaptureLog("wooting sdk unavailable");
		return false;
	}

	Wooting_initialise = reinterpret_cast<WootingInitialise>(
		GetProcAddress(Wooting_module, "wooting_analog_initialise"));
	Wooting_uninitialise = reinterpret_cast<WootingUninitialise>(
		GetProcAddress(Wooting_module, "wooting_analog_uninitialise"));
	Wooting_set_keycode_mode = reinterpret_cast<WootingSetKeycodeMode>(
		GetProcAddress(Wooting_module, "wooting_analog_set_keycode_mode"));
	Wooting_read_full_buffer = reinterpret_cast<WootingReadFullBuffer>(
		GetProcAddress(Wooting_module, "wooting_analog_read_full_buffer"));
	Wooting_version_semver = reinterpret_cast<WootingVersionSemver>(
		GetProcAddress(Wooting_module, "wooting_analog_version_semver"));

	if (!Wooting_initialise || !Wooting_uninitialise ||
		!Wooting_set_keycode_mode || !Wooting_read_full_buffer) {
		mprintf((0, "Wooting Analog SDK is missing required functions; using digital keyboard input.\n"));
		AutomatedCaptureLog("wooting sdk missing required functions");
		FreeLibrary(Wooting_module);
		Wooting_module = NULL;
		return false;
	}

	return true;
}

bool WootingInitializeSdk()
{
	if (Wooting_initialized)
		return true;
	if (!WootingLoadSdk())
		return false;

	const int device_count = Wooting_initialise();
	if (device_count < 0) {
		mprintf((0, "Wooting Analog SDK initialization failed (%d); using digital keyboard input.\n",
			device_count));
		AutomatedCaptureLog("wooting initialization failed result=%d", device_count);
		return false;
	}

	// Descent 3 key bindings are physical scan-code-set-1 values.
	const int mode_result = Wooting_set_keycode_mode(kScanCodeSet1Mode);
	if (mode_result < 0) {
		mprintf((0, "Wooting Analog SDK rejected scan-code mode (%d); using digital keyboard input.\n",
			mode_result));
		AutomatedCaptureLog("wooting scan-code mode failed result=%d", mode_result);
		Wooting_uninitialise();
		return false;
	}

	Wooting_initialized = true;
	const char *version = Wooting_version_semver ? Wooting_version_semver() : "unknown";
	mprintf((0, "Wooting Analog SDK %s initialized with %d device(s).\n",
		version ? version : "unknown", device_count));
	AutomatedCaptureLog("wooting initialized version=%s devices=%d",
		version ? version : "unknown", device_count);
	return true;
}
#endif

uint16_t WootingScanCode(ubyte key)
{
	// Descent 3 stores scan-code-set-1 extended keys with bit 7 set. The
	// Wooting SDK represents the same E0 prefix in the upper byte.
	if (key & 0x80)
		return static_cast<uint16_t>(0xE000u | (key & 0x7fu));
	return key;
}
}

bool WootingAnalogInitialize()
{
#if defined(_WIN64)
	if (!Wooting_analog_enabled)
		return false;
	return WootingInitializeSdk();
#else
	return false;
#endif
}

bool WootingAnalogDeviceAvailable()
{
#if defined(_WIN64)
	if (!WootingInitializeSdk()) {
		WootingAnalogShutdown();
		return false;
	}

	// A successful read, including zero currently pressed keys, proves that at
	// least one compatible device is connected. NoDevices is a negative result.
	unsigned short code = 0;
	float value = 0.0f;
	const int result = Wooting_read_full_buffer(&code, &value, 1);
	if (result >= 0)
		return true;

	WootingAnalogShutdown();
	return false;
#else
	return false;
#endif
}

void WootingAnalogShutdown()
{
	Analog_key_count = 0;
	Analog_sample_valid = false;

#if defined(_WIN64)
	if (Wooting_initialized && Wooting_uninitialise)
		Wooting_uninitialise();
	Wooting_initialized = false;
	Wooting_poll_logged = false;
	Wooting_last_poll_error = 0;

	if (Wooting_module)
		FreeLibrary(Wooting_module);
	Wooting_module = NULL;
	Wooting_initialise = NULL;
	Wooting_uninitialise = NULL;
	Wooting_set_keycode_mode = NULL;
	Wooting_read_full_buffer = NULL;
	Wooting_version_semver = NULL;
	Wooting_load_attempted = false;
#endif
}

bool WootingAnalogPoll()
{
	Analog_key_count = 0;
	Analog_sample_valid = false;

	if (!Wooting_analog_enabled) {
#if defined(_WIN64)
		if (Wooting_initialized || Wooting_module)
			WootingAnalogShutdown();
#endif
		return false;
	}

#if defined(_WIN64)
	if (!WootingAnalogInitialize())
		return false;

	const int count = Wooting_read_full_buffer(Analog_codes, Analog_values,
		kMaximumAnalogKeys);
	if (count < 0) {
		if (count != Wooting_last_poll_error) {
			mprintf((0, "Wooting analog input unavailable (%d); retaining digital keyboard input.\n",
				count));
			AutomatedCaptureLog("wooting poll failed result=%d", count);
		}
		Wooting_last_poll_error = count;
		return false;
	}

	Wooting_last_poll_error = 0;
	Analog_key_count = std::min(count, kMaximumAnalogKeys);
	Analog_sample_valid = true;
	if (!Wooting_poll_logged) {
		AutomatedCaptureLog("wooting poll active keys=%d", Analog_key_count);
		Wooting_poll_logged = true;
	}
	return true;
#else
	return false;
#endif
}

bool WootingAnalogGetKeyValue(ubyte key, float *value)
{
	if (value)
		*value = 0.0f;
	if (!Analog_sample_valid || !key)
		return false;

	const uint16_t code = WootingScanCode(key);
	float raw_value = 0.0f;
	for (int i = 0; i < Analog_key_count; ++i) {
		if (Analog_codes[i] == code && Analog_values[i] > raw_value)
			raw_value = Analog_values[i];
	}

	if (raw_value <= 0.0f)
		return false;

	if (raw_value > 1.0f)
		raw_value = 1.0f;
	float adjusted = 0.0f;
	if (raw_value > kAnalogDeadzone)
		adjusted = (raw_value - kAnalogDeadzone) / (1.0f - kAnalogDeadzone);
	if (value)
		*value = adjusted;
	return true;
}
