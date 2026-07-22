/*
* Descent 3
* Copyright (C) 2024 Parallax Software
* DESCENT 3MASTERED branding and version definitions
* Copyright (C) 2026 Jeff Graw
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#define ENGINE_NAME "DESCENT 3MASTERED"
#define ENGINE_NAME_NO_SPACE "Descent3Mastered"
#define ENGINE_EXECUTABLE_NAME ENGINE_NAME_NO_SPACE ".exe"

// This is the product version of DESCENT 3MASTERED.  Keep it independent of
// D3_MAJORVER/D3_MINORVER: those values describe the legacy Descent 3 protocol
// and data compatibility level and must not be repurposed as release branding.
#define ENGINE_VERSION_MAJOR 0
#define ENGINE_VERSION_MINOR 9
#define ENGINE_VERSION_PATCH 0
#define ENGINE_VERSION_STRING "0.9"

// Read-only migration aliases.  New files and settings must use ENGINE_NAME.
#define ENGINE_LEGACY_NAME "Piccu Engine"
#define ENGINE_LEGACY_NAME_NO_SPACE "PiccuEngine"

#define ENGINE_ENV_CAPTURE_LOG "DESCENT3MASTERED_CAPTURE_LOG"
#define ENGINE_ENV_FRAME_PERF_LOG "DESCENT3MASTERED_FRAME_PERF_LOG"
#define ENGINE_ENV_FRAME_PACING_TELEMETRY "DESCENT3MASTERED_FRAME_PACING_TELEMETRY"
#define ENGINE_LEGACY_ENV_CAPTURE_LOG "PICCU_CAPTURE_LOG"
#define ENGINE_LEGACY_ENV_FRAME_PERF_LOG "PICCU_FRAME_PERF_LOG"
#define ENGINE_LEGACY_ENV_FRAME_PACING_TELEMETRY "PICCU_FRAME_PACING_TELEMETRY"
