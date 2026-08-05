# HANDOFF — ScummVM UWP port

Last updated: 2026-08-05. For the next agent/session. Read this +
`PORT-PLAN.md` + `FILESYSTEM.md` first. Conversation language: Portuguese.
Docs: English.

## Where we are

**Phase 0 (repo restructure) = in progress.** Phase 1 next.

### Strategy (final, 2026-08-05)

**Route C refined: RetroArch UWP shell (compiled from source) + bundled
ScummVM libretro core.**

- RetroArch UWP is the shell only — rendering, audio, input, pacing. **No custom
  RetroArch code.**
- All custom work lives **inside the ScummVM core** (built from
  `extern/scummvm`): custom file picker, custom About, logging (spdlog).
- Filesystem: ScummVM core's Win32 calls (`_wfopen`, `FindFirstFile`,
  `GetFileAttributes`) are blocked in the UWP sandbox. Fix = **FromApp APIs**
  (`api-ms-win-core-file-fromapp-l1-1-0.dll`, `<fileapifromapp.h>`). See
  `docs/FILESYSTEM.md` for the full plan + proven reference implementations
  (dosbox-pure-unleashed-uwp `fopen_wrap`, x-files `DirectoryScanner.cs`).
- Manifest already has `broadFileSystemAccess` (fork); bump MinVersion to
  17763.

### Dependencies (submodules)

| Path | Source | Status |
|---|---|---|
| `extern/scummvm` | ScummVM upstream master | KEPT — core build |
| `extern/retroarch` | https://github.com/XboxEmulationHub/RetroArch.git | NEW — shell (local clone already at `C:\Users\marcelo\workspace\RetroArch`) |
| `extern/spdlog` | https://github.com/gabime/spdlog (v1.17.0) | NEW — header-only logging |
| `extern/uwp-xray-depot` | — | REMOVED (nested spdlog/json/lua unused) |
| `extern/libretro-common` | — | REMOVED (vendored; core Makefile doesn't use it, RetroArch ships its own) |

### Reference projects (in user's workspace, do NOT move)

- `C:\Users\marcelo\workspace\vs2022\dosbox-pure-unleashed-uwp` — working UWP
  libretro shell + core FS patches (FromApp). The proven pattern.
- `C:\Users\marcelo\workspace\x-files-uwp` — C# UWP file manager; FromApp
  P/Invoke reference (`XFiles\FileSystem\DirectoryScanner.cs`, `XFiles\Log.cs`).
- `C:\Users\marcelo\workspace\RetroArch` — local clone of the XboxEmulationHub
  fork used for `extern/retroarch`.
- `C:\Users\marcelo\workspace\vs2022\scummvm-uwp` — OLD repo, to be archived.
  Do not use.

## NEXT STEP — Phase 1: build the core DLL

1. Need MSYS2/cygwin shell + `cygpath` + VS2022 Build Tools.
2. From `extern/scummvm/backends/platform/libretro/`:
   ```
   make platform=windows_msvc2017_uwp_x64 \
     VsInstallRoot="C:/Program Files/Microsoft Visual Studio/2022/<Edition>" \
     WindowsSDKVersion="10.0.XXXXX.0" \
     LITE=1 NO_WIP=1 USE_CURL=0
   ```
3. Verify DLL loads in AppContainer (audit imports for forbidden Win32 APIs).
4. Build datafiles: `make datafiles` → `scummvm.zip`.

Output: `scummvm_libretro.dll` + `scummvm.zip`.

## Then Phase 2: RetroArch UWP shell

- Build `pkg/msvc-uwp/RetroArch-msvcUWP.sln` (x64) from `extern/retroarch`.
- Package appx: RetroArch + `cores/scummvm_libretro.dll` + `scummvm.zip` +
  `retroarch.cfg` (auto-load core, empty content).
- Bump manifest MinVersion → 10.0.17763.0.
- Test: app launches → ScummVM launcher GUI appears.

## Then Phase 3: custom core work

- FS patch (FromApp): `stdiostream.cpp` + `windows-fs.cpp` (+ helper).
- File picker button in ScummVM launcher → FolderPicker.
- Custom About screen.
- spdlog logging wiring.

## Then Phase 4 (Xbox) + Phase 5 (Validation)

Details in PORT-PLAN.md §4.

## Key files

- `extern/scummvm/backends/platform/libretro/Makefile` — UWP target (~line 400)
- `extern/scummvm/backends/platform/libretro/src/libretro-core.cpp` — core entry
- `extern/scummvm/backends/fs/stdiostream.cpp:190` — `_wfopen` (FS patch point)
- `extern/scummvm/backends/fs/windows/windows-fs.cpp` — Win32 FS (patch point)
- `extern/retroarch/pkg/msvc-uwp/RetroArch-msvcUWP/Package.appxmanifest` —
  has `broadFileSystemAccess`

## Reference

- PORT-PLAN.md = source of truth for architecture decisions.
- FILESYSTEM.md = FromApp FS strategy + reference implementations.
