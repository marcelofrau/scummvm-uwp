# HANDOFF — ScummVM UWP port

Last updated: 2026-07-13. For the next agent/session. Read this + `PORT-PLAN.md`
first. Conversation language: Portuguese. Docs: English.

## Where we are

**Phase 0 (repo scaffold) = DONE and committed.** Route A (libretro, dynamic
DLL). Everything is LOCAL only — no remote, no push (per user). Committing in
THIS repo is authorized (user said so). NEVER `git push` without asking. NEVER
`git config --global` (global must stay `marcelo.frau@adidas.com`).

Repo: `/mnt/d/workspace/_non_work_/scummvm-uwp/`
Git identity (local, this repo + both submodules): `Marcelo Frau <marcelofrau@gmail.com>`

### Commits so far
- `16d017a` chore: scaffold ScummVM UWP from dosbox-uwp shell
- `fde0cfe` chore: add submodules extern/scummvm (shallow) + extern/uwp-xray-depot

### What exists on disk
```
scummvm-uwp/
  .gitignore
  scummvm-uwp.sln            # solution -> scummvm-uwp/scummvm-uwp.vcxproj
  docs/
    PORT-PLAN.md             # full plan (source of truth)
    HANDOFF.md               # this file
  extern/
    scummvm/                 # submodule, upstream master, shallow depth=1
    uwp-xray-depot/          # submodule, diagnostics
    libretro-common/         # VENDORED (not a submodule): VFS uwp impl + headers
  scummvm-uwp/               # the app project (copied from dosbox-uwp shell)
    App.cpp/.h
    dosbox_uwpMain.cpp/.h    # NOT yet renamed; still dosbox-coupled
    dosbox_pure_sta.cpp      # DELETE in Phase 3 (dosbox-only DBPS_* stubs)
    scummvm-uwp.vcxproj      # renamed but STILL lists dosbox sources -> Phase 2 rewrite
    scummvm-uwp.vcxproj.filters
    Package.appxmanifest     # still dosbox branding
    Common/  DeviceResources, DirectXHelper, StepTimer  (generic, drop-in)
    Content/ RetroCore, XAudio2Output, RetroScreenRenderer (reusable core)
             FrontendMenu, SettingsManager, FileBrowser, AboutDialog,
             ConfirmDialog, SdlInput, Sample* (dosbox-flavored UI, adapt later)
    Assets/  (still dosbox branding)
```
`local/` (dosbox core patches) was intentionally NOT copied — core comes from
the ScummVM Makefile build as a DLL.

## KEY finding that drives everything
ScummVM's `extern/scummvm/backends/platform/libretro/Makefile` already has a
UWP target (line 398 `windows_msvc2017`, line 405 handles `uwp` suffix). It
compiles with cl.exe, `WINAPI_FAMILY_APP`, `-APPCONTAINER`, links only
`WindowsApp.lib`, outputs `scummvm_libretro.dll`. Build:
`make platform=windows_msvc2017_uwp_x64`. So we do NOT hand-port sources into
the vcxproj — we build the core with ScummVM's own build system and load the
DLL via `LoadPackagedLibrary`.

Decisions locked: **dynamic DLL** integration; first engines **SCUMM + SKY**
only, `LITE=1 NO_WIP=1`.

## NEXT STEP — Phase 1: build the core DLL (highest risk, do it isolated first)
Goal: produce a `scummvm_libretro.dll` that loads in an AppContainer.

1. Need MSYS2/cygwin shell + `cygpath` (Makefile is unix-shell based) and VS
   build tools. Makefile hardcodes VS2017 paths — override `VsInstallRoot` /
   `WindowsSDKVersion` for the installed VS (2019/2022).
2. From `extern/scummvm/backends/platform/libretro/`:
   `make platform=windows_msvc2017_uwp_x64 LITE=1 NO_WIP=1 USE_CURL=0`
   (start minimal; add engines SCUMM+SKY; disable non-essential USE_* libs to
   shrink forbidden-API surface for AppContainer).
3. Resolve engine-table generation (`detection_table.h`, `plugins_table.h`,
   `engines.mk`) — generated outside `./configure`; may fight MSVC.
4. Also build datafiles: `make datafiles` / `make all` -> `scummvm.zip`
   (must be version-matched to the scummvm checkout).
5. Verify the resulting DLL links only AppContainer-safe imports (no forbidden
   Win32). Audit 3rd-party libs (vorbis/mad/mt32emu) if enabled.

Output to stash for Phase 2: `scummvm_libretro.dll` + `scummvm.zip`
(both are gitignored build artifacts).

## Then Phase 2 — dynamic integration
- Rewrite `scummvm-uwp.vcxproj`: remove ALL dosbox `$(DBPDir)` / `local\dosbox-pure`
  ClCompile entries + include dirs + `DBP_STANDALONE` define. Keep app/bridge
  files + vendored libretro-common VFS. Keep `__LIBRETRO__`.
- `Content/RetroCore.cpp`: replace direct libretro symbol calls with a function
  -pointer table populated by `LoadPackagedLibrary("scummvm_libretro.dll")` +
  `GetProcAddress` (retro_init/run/load_game/set_*_callback/get_system_av_info).
  Currently it links symbols statically (dosbox amalgam). Need `libretro.h` —
  get it from the scummvm libretro backend (the vendored libretro-common here
  does NOT contain libretro.h).
- Package `scummvm_libretro.dll` + `scummvm.zip` into the appx.

## Then Phase 3 — adapt scaffold (details in PORT-PLAN.md §4)
- XAudio2Output: parameterize 44100 -> sample_rate (ScummVM default 48000).
- RetroScreenRenderer: add RGB565 path OR force XRGB8888 via SET_PIXEL_FORMAT.
- Main loop: add QPC pacing — ScummVM does NOT self-pace (dosbox used
  CPU_CycleMax). Honor "Frame rate cap" option.
- Strip dosbox: RetroCore ~4 dosbox option keys + DBPS_ToggleOSD; delete
  dosbox_pure_sta.cpp; rename dosbox_uwpMain -> scummvm_uwpMain; rebrand
  Package.appxmanifest + Assets.
- Rebind input maps for ScummVM controls.

## Phase 4 audio / Phase 5 validation
- Reuse DRC plan from `dosbox-pure-uwp/docs/audio-fix/` (same XAudio2 sink).
- Validate via xray binds (audio_queued/produced/consumed). PASS = SCUMM/SKY
  runs, 0 underruns, no pitch shift, no pinned CPU core.

## Ranked risks (see PORT-PLAN.md §5)
1. 3rd-party libs in AppContainer (forbidden APIs) -> disable non-essential USE_*.
2. Engine-table generation under MSVC.
3. Makefile hardcodes VS2017 -> override toolchain.
4. ScummVM needs its own pacing loop.

## Reference (source of truth reminders)
- Old `dosbox-pure-uwp/docs/` may be STALE; code + git history win.
- dosbox scaffold inventory (generic vs dosbox-specific) is summarized in
  PORT-PLAN.md §3.
