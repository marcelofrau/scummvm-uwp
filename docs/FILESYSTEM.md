# FILESYSTEM — Accessing the real filesystem from the UWP sandbox

Why: the ScummVM core (and any libretro core) running inside an AppContainer
cannot use plain Win32 file APIs on arbitrary paths — the sandbox blocks them.
This document records the correct approach (the "lib certa") and the reference
implementations we reuse.

## The FromApp API family

DLL: `api-ms-win-core-file-fromapp-l1-1-0.dll`
Header: `<fileapifromapp.h>` (Windows SDK, UWP builds)

These are sandbox-aware equivalents of the classic file APIs. They succeed when
the app has been granted filesystem access by the user (Settings → Privacy →
File system for `broadFileSystemAccess`, or a folder grant from a picker).

| FromApp API | Replaces | Purpose |
|---|---|---|
| `CreateFile2FromAppW` | `CreateFile2` / `CreateFile` | Open/create file |
| `CreateFileFromAppW` | `CreateFile` | Open/create file (simple) |
| `GetFileAttributesExFromAppW` | `GetFileAttributesEx` | Stat a path |
| `FindFirstFileExFromAppW` | `FindFirstFileEx` | Directory enumeration start |
| `FindNextFileFromAppW` | `FindNextFile` | Directory enumeration next |
| `CreateDirectoryFromAppW` | `CreateDirectory` | Create directory |
| `CopyFileFromAppW` | `CopyFile` | Copy file |
| `MoveFileFromAppW` | `MoveFile` | Move/rename file |
| `DeleteFileFromAppW` | `DeleteFile` | Delete file |

Note: `FindNextFileW`, `FindClose`, `GetLogicalDrives`, `CloseHandle` remain
plain `kernel32.dll` calls and are allowed.

## Reference implementation 1 — dosbox-pure-unleashed-uwp (C++, proven on Xbox)

Location: `C:\Users\marcelo\workspace\vs2022\dosbox-pure-unleashed-uwp`
Files: `dosbox-uwp/local/dosbox-pure/dosbox_pure_libretro.cpp`
(`fopen_wrap`, `exists_utf8`).

### `fopen_wrap` — replace `fopen`/`_wfopen`

```c
FILE* fopen_wrap(const char* path, const char* mode)
{
#ifdef WIN32
	// Use CreateFile2FromAppW for broadFileSystemAccess on UWP Xbox.
	// Standard _wfopen / fopen may fail even with runFullTrust on Xbox device.
	wchar_t *wpath = AllocUTF8ToUTF16(path);
	if (!wpath) return NULL;

	bool isWrite  = (strchr(mode, 'w') != NULL);
	bool isAppend = (strchr(mode, 'a') != NULL);
	bool isPlus   = (strchr(mode, '+') != NULL);

	DWORD access = GENERIC_READ;
	DWORD creation = OPEN_EXISTING;
	int   crtFlags = _O_RDONLY;
	const char* fdopenMode = "rb";

	if (isWrite) {
		access = isPlus ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_WRITE;
		creation = CREATE_ALWAYS;
		crtFlags = isPlus ? _O_RDWR : _O_WRONLY;
		fdopenMode = isPlus ? "w+b" : "wb";
	} else if (isAppend) {
		access = isPlus ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_WRITE;
		creation = OPEN_ALWAYS;
		crtFlags = isPlus ? _O_RDWR : _O_WRONLY;
		fdopenMode = isPlus ? "a+b" : "ab";
	} else if (isPlus) {
		access = GENERIC_READ | GENERIC_WRITE;
		crtFlags = _O_RDWR;
		fdopenMode = "r+b";
	}

	HANDLE h = CreateFile2FromAppW(wpath, access,
		(isWrite || isAppend) ? 0 : FILE_SHARE_READ, creation, NULL);
	free(wpath);
	if (h == INVALID_HANDLE_VALUE) return NULL;

	int fd = _open_osfhandle((intptr_t)h, crtFlags);
	if (fd == -1) { CloseHandle(h); return NULL; }

	return _fdopen(fd, fdopenMode);
#else
	return fopen(path, mode);
#endif
}
```

Key ideas: map the CRT mode string to `access`/`creation` flags, open via
`CreateFile2FromAppW`, then convert the HANDLE back to a CRT `FILE*` with
`_open_osfhandle` + `_fdopen`. The rest of the core keeps using `FILE*`.

### `exists_utf8` — replace `stat`

```c
#ifdef WIN32
	wchar_t *wpath = AllocUTF8ToUTF16(path);
	WIN32_FILE_ATTRIBUTE_DATA fad{};
	bool retval = GetFileAttributesExFromAppW(wpath, GetFileExInfoStandard, &fad) != FALSE;
	if (out_is_dir) *out_is_dir = retval && !!(fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
	return retval;
#else
	struct stat test;
	if (stat(path, &test)) return false;
	...
#endif
```

## Reference implementation 2 — x-files-uwp (C#, directory enumeration)

Location: `C:\Users\marcelo\workspace\x-files-uwp\XFiles\FileSystem\DirectoryScanner.cs`

- `FindFirstFileExFromAppW` (with `FIND_FIRST_EX_LARGE_FETCH`) + `FindNextFileW`
  + `FindClose` for listing a directory.
- `CreateFile2FromAppW` → wrap HANDLE in `SafeFileHandle` + `FileStream` for
  reading.
- Root scan: `GetLogicalDrives()` bitmask builds `"C:\"`-style entries, plus the
  app's `LocalFolder`.

## Where this lands in ScummVM core

| File (extern/scummvm) | Today | Patch to |
|---|---|---|
| `backends/fs/stdiostream.cpp` (~line 190) | `_wfopen(wPath, L"rb"/L"wb")` | `CreateFile2FromAppW` → `_open_osfhandle` → `_fdopen` (fopen_wrap pattern) |
| `backends/fs/windows/windows-fs.cpp` | `GetFileAttributes`, `FindFirstFile/FindNextFile`, `CreateDirectory`, `GetLogicalDriveStrings` | FromApp equivalents + `GetLogicalDrives` bitmask |
| Build | — | guard with `#if defined(WIN32) && WINAPI_FAMILY==WINAPI_FAMILY_APP` (already defined by Makefile target) |

## Access grants

1. `broadFileSystemAccess` is already declared in the RetroArch fork manifest
   (`<rescap:Capability Name="broadFileSystemAccess"/>`) — user toggles in
   Settings → Privacy → File system, or
2. FolderPicker grant (persists per-folder; the custom picker button we add to
   the ScummVM launcher uses this).

Requires `MinVersion >= 10.0.17763.0` in the manifest.
