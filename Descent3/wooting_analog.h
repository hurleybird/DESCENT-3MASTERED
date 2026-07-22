/*
 * Descent 3
 * Copyright (C) 2024 Parallax Software
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef WOOTING_ANALOG_H
#define WOOTING_ANALOG_H

#include "pstypes.h"

extern bool Wooting_analog_enabled;

// Initializes the optional Wooting SDK when support is enabled. Failure is
// non-fatal; the normal digital keyboard path remains active.
bool WootingAnalogInitialize();
void WootingAnalogShutdown();

// Probes for a currently connected compatible analog device. This is used by
// the configuration UI even when native analog input is switched off.
bool WootingAnalogDeviceAvailable();

// Polls all analog keys once for the current gameplay frame.
bool WootingAnalogPoll();

// Returns true when this physical Descent 3 scan-code binding is currently
// active on an analog keyboard. value receives its deadzone-adjusted 0..1
// travel value.
bool WootingAnalogGetKeyValue(ubyte key, float *value);

#endif
