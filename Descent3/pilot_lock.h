#ifndef D3_PILOT_LOCK_H
#define D3_PILOT_LOCK_H

void PilotBuildLockPath(const char* pilot_filename, char* lock_path);
bool PilotAcquireCurrentFileLock(const char* pilot_filename);
bool PilotCurrentFileLockMatches(const char* pilot_filename);
void PilotReleaseCurrentFileLock();

#endif
