#ifndef D3_MULTI_INSTANCE_H
#define D3_MULTI_INSTANCE_H

#include <string>
#include <stdio.h>

// A process-scoped, crash-safe exclusive file lock. The file may remain on
// disk forever; ownership is represented only by the live OS handle.
class MultiInstanceFileLock
{
public:
	MultiInstanceFileLock();
	~MultiInstanceFileLock();
	MultiInstanceFileLock(const MultiInstanceFileLock&) = delete;
	MultiInstanceFileLock& operator=(const MultiInstanceFileLock&) = delete;
	MultiInstanceFileLock(MultiInstanceFileLock&& other) noexcept;
	MultiInstanceFileLock& operator=(MultiInstanceFileLock&& other) noexcept;

	bool TryAcquire(const char* path);
	void Release();
	bool IsHeld() const;
	const std::string& Path() const { return path_; }

private:
#ifdef WIN32
	void* handle_;
#else
	int descriptor_;
#endif
	std::string path_;
};

bool MultiInstanceAtomicReplace(const char* temporary_path, const char* destination_path,
	const char* backup_path = nullptr);
unsigned long MultiInstanceProcessId();
bool MultiInstanceFlushFileToDisk(FILE* file);
void MultiInstanceCleanupStaleProcessDirectories(const char* parent_directory,
	const char* instance_prefix);

#endif
