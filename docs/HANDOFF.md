# HANDOFF — ScummVM UWP port

Last updated: 2026-07-27. For the next agent/session. Read this + `PORT-PLAN.md`
first. Conversation language: Portuguese. Docs: English.

## Where we are

**Phase 0 (repo scaffold) = DONE and committed.** Phase 1 next.

### Strategy change (2026-07-27)

**Route C: RetroArch UWP as base + bundled scummvm core.**

Previous Route A (custom shell from dosbox-uwp) was abandoned. RetroArch UWP
already handles rendering, audio, input, pacing, core loading. ScummVM's
libretro core has its own full GUI launcher. Only custom work needed:
file picker for UWP sandbox + bootstrap config.

### Commits so far
- `16d017a` chore: scaffold ScummVM UWP from dosbox-uwp shell
- `fde0cfe` chore: add submodules extern/scummvm (shallow) + extern/uwp-xray-depot

### What exists on disk
```
scummvm-uwp/
  .gitignore
  scummvm-uwp.sln
  docs/
    PORT-PLAN.md             # source of truth (Route C as of 2026-07-27)
    HANDOFF.md               # this file
  extern/
    scummvm/                 # submodule, upstream master, shallow depth=1
    uwp-xray-depot/          # submodule, diagnostics
    libretro-common/         # VENDORED: VFS uwp impl + headers
  scummvm-uwp/               # app project (copied from dosbox-uwp shell)
    App.cpp/.h
    dosbox_uwpMain.cpp/.h    # LEGACY — will be replaced by RetroArch base
    dosbox_pure_sta.cpp      # LEGACY — delete
    scummvm-uwp.vcxproj      # LEGACY — rewrite for Route C
    Content/                  # LEGACY bridge code — most files no longer needed
    Common/                   # Generic D3D helpers — may be kept
    Assets/                   # Still dosbox branding
```

The dosbox-uwp scaffold files are LEGACY. Route C uses RetroArch UWP as the
frontend, so most of the custom bridge code (RetroCore, RetroScreenRenderer,
XAudio2Output, SdlInput) is no longer needed.

## NEXT STEP — Phase 1: build the core DLL

Same as before — this step is unchanged from Route A.

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

## Then Phase 2: RetroArch UWP integration

- Obtain RetroArch UWP build (release binary or build from source).
- Create appx: RetroArch + scummvm core + `retroarch.cfg` (auto-load core).
- Test: app launches → ScummVM launcher GUI appears → game list works.

## Then Phase 3: File picker + UWP sandbox

- UWP FolderPicker → user picks game folder → files accessible to ScummVM.
- Test: pick SCUMM game → appears in ScummVM launcher → runs.

## Then Phase 4: Xbox + Phase 5: Validation

Details in PORT-PLAN.md §4.

## Key files (if still relevant)

- `extern/scummvm/backends/platform/libretro/Makefile` — UWP target at line 398
- `extern/scummvm/backends/platform/libretro/src/libretro-core.cpp` — core entry
- `extern/scummvm/backends/platform/libretro/src/libretro-os-base.cpp` — OSystem init
- `extern/scummvm/base/main.cpp:755` — launcherDialog() trigger

## Reference

- PORT-PLAN.md is source of truth for architecture decisions.
- Old dosbox-uwp docs may be STALE; code + git history win.
