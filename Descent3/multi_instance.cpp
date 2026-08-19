#include "multi_instance.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef WIN32
#include <windows.h>
#include <io.h>
#include <cctype>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>
#endif

MultiInstanceFileLock::MultiInstanceFileLock()
#ifdef WIN32
	: handle_(INVALID_HANDLE_VALUE)
#else
	: descriptor_(-1)
#endif
{
}

MultiInstanceFileLock::~MultiInstanceFileLock()
{
	Release();
}

MultiInstanceFileLock::MultiInstanceFileLock(MultiInstanceFileLock&& other) noexcept
#ifdef WIN32
	: handle_(other.handle_)
#else
	: descriptor_(other.descriptor_)
#endif
	, path_(other.path_)
{
#ifdef WIN32
	other.handle_ = INVALID_HANDLE_VALUE;
#else
	other.descriptor_ = -1;
#endif
	other.path_.clear();
}

MultiInstanceFileLock& MultiInstanceFileLock::operator=(MultiInstanceFileLock&& other) noexcept
{
	if (this == &other)
		return *this;
	Release();
#ifdef WIN32
	handle_ = other.handle_;
	other.handle_ = INVALID_HANDLE_VALUE;
#else
	descriptor_ = other.descriptor_;
	other.descriptor_ = -1;
#endif
	path_ = other.path_;
	other.path_.clear();
	return *this;
}

bool MultiInstanceFileLock::TryAcquire(const char* path)
{
	Release();
	if (!path || !path[0])
		return false;

#ifdef WIN32
	handle_ = CreateFileA(path, GENERIC_READ, 0, nullptr, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle_ == INVALID_HANDLE_VALUE)
		return false;
#else
	descriptor_ = open(path, O_RDONLY | O_CREAT, 0600);
	if (descriptor_ < 0)
		return false;
	if (flock(descriptor_, LOCK_EX | LOCK_NB) != 0)
	{
		close(descriptor_);
		descriptor_ = -1;
		return false;
	}
#endif
	path_ = path;
	return true;
}

void MultiInstanceFileLock::Release()
{
#ifdef WIN32
	if (handle_ != INVALID_HANDLE_VALUE)
	{
		CloseHandle((HANDLE)handle_);
		handle_ = INVALID_HANDLE_VALUE;
	}
#else
	if (descriptor_ >= 0)
	{
		flock(descriptor_, LOCK_UN);
		close(descriptor_);
		descriptor_ = -1;
	}
#endif
	path_.clear();
}

bool MultiInstanceFileLock::IsHeld() const
{
#ifdef WIN32
	return handle_ != INVALID_HANDLE_VALUE;
#else
	return descriptor_ >= 0;
#endif
}

bool MultiInstanceAtomicReplace(const char* temporary_path, const char* destination_path,
	const char* backup_path)
{
#ifdef WIN32
	if (GetFileAttributesA(destination_path) != INVALID_FILE_ATTRIBUTES)
	{
		const char* usable_backup = backup_path;
		if (usable_backup && GetFileAttributesA(usable_backup) != INVALID_FILE_ATTRIBUTES &&
			!DeleteFileA(usable_backup))
			usable_backup = nullptr;
		if (ReplaceFileA(destination_path, temporary_path, usable_backup,
			REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE)
			return true;
		return MoveFileExA(temporary_path, destination_path,
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
	}
	return MoveFileExA(temporary_path, destination_path, MOVEFILE_WRITE_THROUGH) != FALSE;
#else
	(void)backup_path;
	return rename(temporary_path, destination_path) == 0;
#endif
}

unsigned long MultiInstanceProcessId()
{
#ifdef WIN32
	return (unsigned long)GetCurrentProcessId();
#else
	return (unsigned long)getpid();
#endif
}

bool MultiInstanceFlushFileToDisk(FILE* file)
{
	if (!file || fflush(file) != 0)
		return false;
#ifdef WIN32
	const intptr_t os_handle = _get_osfhandle(_fileno(file));
	return os_handle != -1 && FlushFileBuffers((HANDLE)os_handle) != FALSE;
#else
	return fsync(fileno(file)) == 0;
#endif
}

void MultiInstanceCleanupStaleProcessDirectories(const char* parent_directory,
	const char* instance_prefix)
{
#ifdef WIN32
	if (!parent_directory || !instance_prefix || !parent_directory[0] || !instance_prefix[0])
		return;
	char search_path[MAX_PATH * 2];
	snprintf(search_path, sizeof(search_path), "%s\\%s*", parent_directory, instance_prefix);
	WIN32_FIND_DATAA entry;
	HANDLE find = FindFirstFileA(search_path, &entry);
	if (find == INVALID_HANDLE_VALUE)
		return;
	do
	{
		if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			continue;
		const size_t prefix_length = strlen(instance_prefix);
		if (strncmp(entry.cFileName, instance_prefix, prefix_length) != 0)
			continue;
		const char* pid_text = entry.cFileName + prefix_length;
		if (!pid_text[0])
			continue;
		bool numeric = true;
		for (const char* c = pid_text; *c; ++c)
			numeric = numeric && std::isdigit((unsigned char)*c) != 0;
		if (!numeric)
			continue;
		const DWORD pid = (DWORD)strtoul(pid_text, nullptr, 10);
		if (!pid || pid == GetCurrentProcessId())
			continue;

		bool process_is_alive = true;
		HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
		if (process)
		{
			process_is_alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
			CloseHandle(process);
		}
		else if (GetLastError() == ERROR_INVALID_PARAMETER)
		{
			process_is_alive = false;
		}
		// Access denied or any ambiguous result fails closed and leaves harmless
		// disk clutter instead of risking another process's files.
		if (process_is_alive)
			continue;

		char directory[MAX_PATH * 2];
		snprintf(directory, sizeof(directory), "%s\\%s", parent_directory, entry.cFileName);
		char contents_search[MAX_PATH * 2];
		snprintf(contents_search, sizeof(contents_search), "%s\\*", directory);
		WIN32_FIND_DATAA child;
		HANDLE child_find = FindFirstFileA(contents_search, &child);
		if (child_find != INVALID_HANDLE_VALUE)
		{
			do
			{
				if ((child.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
					!strcmp(child.cFileName, ".") || !strcmp(child.cFileName, ".."))
					continue;
				char child_path[MAX_PATH * 2];
				snprintf(child_path, sizeof(child_path), "%s\\%s", directory, child.cFileName);
				DeleteFileA(child_path);
			} while (FindNextFileA(child_find, &child));
			FindClose(child_find);
		}
		RemoveDirectoryA(directory);
	} while (FindNextFileA(find, &entry));
	FindClose(find);
#else
	(void)parent_directory;
	(void)instance_prefix;
#endif
}
