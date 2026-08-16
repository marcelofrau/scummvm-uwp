# FILESYSTEM — Accessing the real filesystem from the UWP sandbox

Why: the ScummVM libretro core makes file calls that, in a plain AppContainer,
would be blocked on arbitrary paths (E: drive, user folders). This document
records how the frontend handles filesystem access for the core and for
bootstrap/staging, and the reference implementations we reuse.

## The two mechanisms

### 1. `runFullTrust` (primary — why it just works)

The app manifest declares `<rescap:Capability Name="runFullTrust"/>`. The
process runs full-trust, so **plain Win32/CRT file APIs work** — `fopen`,
`_wfopen`, `GetFileAttributes`, `FindFirstFile`, `CreateFile`, `GetLogicalDrives`,
etc. This is exactly why the core works today under RetroArch (also
`runFullTrust`): the ScummVM core's `StdioStream` (`fopen`/`fread`) succeeds
in-process without any patching.

### 2. FromApp API family (VFS for the core + safety net)

The core prefers the libretro VFS: when the frontend answers
`RETRO_ENVIRONMENT_GET_VFS_INTERFACE`, all the core's `filestream_open` /
`retro_dirent` / `retro_stat` calls route through **our** implementation. We
compile `libretro-common/vfs/vfs_implementation_uwp.cpp`, which uses the
sandbox-aware **FromApp** APIs — correct even for narrower grants, and it is
the canonical dosbox-uwp pattern.

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
plain `kernel32.dll` calls and are allowed. DLL:
`api-ms-win-core-file-fromapp-l1-1-0.dll`; header: `<fileapifromapp.h>`.

## Where this lands in the frontend (not in the core)

| Piece | What uses it |
|---|---|
| `libretro-common/vfs/vfs_implementation_uwp.cpp` | Compiled **into the frontend app**; served to the core via `GET_VFS_INTERFACE` (retro_vfs v3) |
| Core's own `filestream`/`dirent`/`stat` | Routed through the frontend VFS → FromApp |
| Core's `StdioStream` (`fopen`) | Works because the process is `runFullTrust` |
| Bootstrap/staging (C++) | E: staging + zip extraction; plain `fopen` (full-trust), FromApp helpers reused from the VFS file |
| Browse E: for games (ScummVM GUI) | Via VFS `retro_opendir`/`FindFirstFileExFromAppW` |

The core is **not patched**. This differs from the old plan (patching
`stdiostream.cpp` / `windows-fs.cpp`), which is obsolete — the buildbot binary
stays untouched.

## Reference implementation — dosbox-pure-unleashed-uwp (C++, proven on Xbox)

Location: `F:\workspace\vs2022\dosbox-pure-unleashed-uwp`
Files:
- `extern\libretro-common\vfs\vfs_implementation_uwp.cpp` — the VFS (FromApp)
  compiled into the app and handed to the core.
- `dosbox-uwp\Content\RetroCore.cpp` — `GET_VFS_INTERFACE` wiring.

The `fopen_wrap` / `exists_utf8` pattern in the dosbox core source is no longer
needed for us (we don't recompile the core), but remains a good reference for
any FromApp C usage:

```c
HANDLE h = CreateFile2FromAppW(wpath, access, share, creation, NULL);
int fd  = _open_osfhandle((intptr_t)h, crtFlags);
FILE *f = _fdopen(fd, fdopenMode);
```

## Reference implementation — x-files-uwp (C#, directory enumeration)

Location: `F:\workspace\x-files-uwp\XFiles\FileSystem\`
(`Win32FileStream.cs`, `FileOperations.cs`). `FindFirstFileExFromAppW` (with
`FIND_FIRST_EX_LARGE_FETCH`) + `FindNextFileW` + `FindClose`; drive enumeration
via `GetLogicalDrives` bitmask.

## Access grants

- `broadFileSystemAccess` is declared in our manifest
  (`<rescap:Capability Name="broadFileSystemAccess"/>`) — full filesystem once
  granted in Settings → Privacy → File system.
- Requires `MinVersion >= 10.0.17763.0` (already satisfied).
- FolderPicker grants remain an option for future per-folder access; not needed
  for the current flow.
