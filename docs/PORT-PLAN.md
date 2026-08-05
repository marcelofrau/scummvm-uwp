# ScummVM UWP Port — Plan (Route C: RetroArch UWP shell + bundled ScummVM libretro core)

Status: **active**. Author: Marcelo Frau. Last updated: 2026-08-05.

This document is the source of truth for the ScummVM UWP port.

## 1. Goal

Ship a Universal Windows Platform (UWP / Xbox) app that runs ScummVM games,
**reusing RetroArch UWP as the shell** and bundling the ScummVM libretro core
as a DLL inside the appx package.

## 2. Core strategy

### RetroArch UWP as the shell (the "casca")

RetroArch UWP is compiled from source (`pkg/msvc-uwp/`), using the
**XboxEmulationHub/RetroArch** fork (Xbox-focused, already declares
`broadFileSystemAccess`). RetroArch handles everything it already does
correctly on UWP/Xbox:

- D3D rendering (D3D11, ANGLE), XAudio2, frame pacing, vsync
- Gamepad/keyboard/mouse input
- Core loading (`LoadPackagedLibrary`), settings UI
- UWP file picker / content browser

**RetroArch is treated as a pure shell.** We do NOT add custom code to it.
Custom features live inside the ScummVM core (which we build from source
anyway). The only RetroArch-side work is packaging: config (`retroarch.cfg`),
assets/branding, and the appx manifest.

### ScummVM core = libretro DLL

Built from `extern/scummvm` submodule:

```
make platform=windows_msvc2017_uwp_x64 \
  VsInstallRoot="C:/Program Files/Microsoft Visual Studio/2022/<Edition>" \
  WindowsSDKVersion="10.0.XXXXX.0" \
  LITE=1 NO_WIP=1 USE_CURL=0
```

- MSVC toolchain (cl.exe + link.exe + Windows SDK), AppContainer DLL,
  links only `WindowsApp.lib`
- `WINAPI_FAMILY=WINAPI_FAMILY_APP` already defined by the Makefile target
- Output: `scummvm_libretro.dll` + `scummvm.zip` (themes/datafiles)

Launched with no game content: `retro_load_game(NULL)` → `scummvm_main("scummvm")`
→ launcher GUI renders directly into RetroArch's video output. **RetroArch is a
dumb renderer; ScummVM owns its full GUI.**

### Filesystem on UWP — FromApp APIs (KEY ARCHITECTURAL INSIGHT)

ScummVM core's Windows filesystem backend uses plain Win32 APIs that are
**blocked inside the UWP sandbox**:

| ScummVM call | Used in | UWP replacement |
|---|---|---|
| `_wfopen` / `fopen` | `backends/fs/stdiostream.cpp` | `CreateFile2FromAppW` → `_open_osfhandle` → `_fdopen` |
| `GetFileAttributes` | `backends/fs/windows/windows-fs.cpp` | `GetFileAttributesExFromAppW` |
| `FindFirstFile` / `FindNextFile` | `backends/fs/windows/windows-fs.cpp` | `FindFirstFileExFromAppW` / `FindNextFileFromAppW` |
| `GetLogicalDriveStrings` | `backends/fs/windows/windows-fs.cpp` | `GetLogicalDrives` (bitmask) |

The correct way to access the real filesystem from an AppContainer is the
**FromApp API family** in `api-ms-win-core-file-fromapp-l1-1-0.dll`
(header: `<fileapifromapp.h>`, part of the Windows SDK). These APIs respect
the user's granted filesystem access (Settings toggle or FolderPicker grants).

**Reference implementations (battle-tested):**
- `dosbox-pure-unleashed-uwp` (Xbox): `fopen_wrap()` — `CreateFile2FromAppW` +
  `_open_osfhandle` + `_fdopen`, with full mode mapping (w/a/+/b);
  `exists_utf8()` — `GetFileAttributesExFromAppW`. See `docs/FILESYSTEM.md`.
- `x-files-uwp` (C#): `FileSystem/DirectoryScanner.cs` — full directory
  enumeration via `FindFirstFileExFromAppW` + `FindNextFileW` + `FindClose`,
  drive enumeration via `GetLogicalDrives`.

The appx manifest already declares `broadFileSystemAccess` (rescap), so once
the user grants file access (Settings → Privacy → File system, or via
FolderPicker) the FromApp calls succeed on arbitrary paths. **MinVersion must
be bumped to 10.0.17763.0** (minimum build that supports `broadFileSystemAccess`).

### Custom work lives in the ScummVM core

| Feature | Where | Notes |
|---|---|---|
| Custom file picker | ScummVM launcher GUI | Button → `FolderPicker` (WinRT) → storage access grant → FromApp browser works |
| Custom About screen | ScummVM launcher About | Branding ScummVM UWP |
| Logging | Custom core code | spdlog (submodule), header-only, wired into core Makefile |

## 3. What we build

| Component | Source | Notes |
|---|---|---|
| RetroArch UWP shell | `extern/retroarch` (XboxEmulationHub fork), `pkg/msvc-uwp` | Compiled with VS2022/MSBuild |
| scummvm_libretro.dll | `extern/scummvm` Makefile `windows_msvc2017_uwp_x64` | Bundled in appx `cores/` |
| scummvm.zip | ScummVM `make datafiles` | Themes + datafiles |
| Custom core code | `extern/scummvm` patches | Filepicker, About, logging |
| retroarch.cfg | config | Auto-load ScummVM core, skip RetroArch menu |
| spdlog | `extern/spdlog` (submodule, v1.17.0) | Header-only logging for custom code |

### Appx package structure (target)

```
scummvm-uwp.appx
├── RetroArch-msvcUWP.exe
├── cores/
│   └── scummvm_libretro.dll
├── scummvm.zip
├── retroarch.cfg
└── Assets/
```

## 4. Phases

### Phase 0 — Repo restructure (IN PROGRESS)
- Remove legacy dosbox-uwp scaffold (`scummvm-uwp/` app project, old .sln/props)
- Remove `extern/uwp-xray-depot` (nested spdlog/json/lua unused; spdlog promoted to top-level)
- Remove `extern/libretro-common` (vendored; core Makefile does not use it,
  RetroArch ships its own)
- Add submodules: `extern/retroarch` (XboxEmulationHub), `extern/spdlog` (v1.17.0)
- Docs updated (PORT-PLAN, HANDOFF, FILESYSTEM)

### Phase 1 — Build the core DLL
- `make platform=windows_msvc2017_uwp_x64` (MSYS2/cygwin + VS2022 Build Tools)
- `LITE=1 NO_WIP=1 USE_CURL=0`
- Verify DLL loads in AppContainer (audit imports for forbidden Win32 APIs)
- **Output:** `scummvm_libretro.dll` + `scummvm.zip`

### Phase 2 — RetroArch UWP shell + appx
- Build `RetroArch-msvcUWP.sln` (x64; ARM64 later) from `extern/retroarch/pkg/msvc-uwp`
- Package appx: RetroArch + `cores/scummvm_libretro.dll` + `scummvm.zip` +
  `retroarch.cfg` (auto-load core, no content)
- Bump manifest MinVersion to 10.0.17763.0
- Verify: app launches → ScummVM launcher GUI appears (render/audio/input/pacing)

### Phase 3 — Custom core work
- **FS patch (FromApp):** `stdiostream.cpp` + `windows-fs.cpp` (+ helper),
  guarded by `WINAPI_FAMILY == WINAPI_FAMILY_APP`
- **File picker:** launcher GUI button → `FolderPicker` via
  `CoreWindow::GetForCurrentThread()`; grant persists → FromApp enumeration
- **About custom:** launcher About screen branding
- **spdlog:** logging in custom code (include path + Makefile wiring)

### Phase 4 — Xbox-specific
- Restricted filesystem — all games via picker or pre-packaged
- Controller navigation + mouse emulation (right stick → pointer) in ScummVM GUI
- Test on Xbox Dev Mode

### Phase 5 — Validation + packaging
- SCUMM + SKY engines run; audio/video/input correct
- Package .appx/.appxbundle for sideload / Store

## 5. Risks

1. **FolderPicker from core GUI thread** — WinRT async marshaling from the
   libretro video thread; needs `CoreWindow::GetForCurrentThread()` + correct
   dispatch. Trickiest part of Phase 3.
2. **Drive root on UWP** — `GetLogicalDriveStrings` blocked; use
   `GetLogicalDrives` bitmask (x-files pattern).
3. **`broadFileSystemAccess` min build 17763** — bump manifest MinVersion.
4. **Fork lag** — XboxEmulationHub/RetroArch may lag libretro upstream.
5. **Engine-table generation** outside `./configure` may break under MSVC.
6. **spdlog in Makefile build** — header-only, but must add include path and
   any needed flags to the libretro Makefile.

## 6. Decisions log

- **2026-07-13**: Route A (libretro) over Route B (SDL native OSystem_WinRT).
  Dynamic DLL over static lib. First engines: SCUMM + SKY.
- **2026-07-27**: **Route C (RetroArch UWP as base)** over Route A (custom shell).
  RetroArch already handles rendering, audio, input, pacing correctly on UWP.
  ScummVM core has its own GUI launcher. No need to reimplement bridge code.
- **2026-08-05**: **Refined Route C.** RetroArch compiled from source
  (XboxEmulationHub fork, `pkg/msvc-uwp`) as a pure shell — no custom RetroArch
  code. Custom work (filepicker, About, logging/spdlog) moves **into the ScummVM
  core** via patches. Filesystem solved with **FromApp APIs**
  (`fileapifromapp.h`), proven on Xbox by dosbox-pure-unleashed-uwp and
  x-files-uwp. spdlog promoted from xray's nested copy to a top-level submodule
  (v1.17.0). `uwp-xray-depot` + vendored `libretro-common` removed.
