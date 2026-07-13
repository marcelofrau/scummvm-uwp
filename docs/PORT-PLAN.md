# ScummVM UWP Port — Plan (Route A: libretro, dynamic DLL)

Status: **active**. Author: Marcelo Frau. Last updated: 2026-07-13.

This document is the source of truth for the ScummVM UWP port. The older
`dosbox-pure-uwp/docs/` may be stale — code + git history win.

## 1. Goal

Ship a Universal Windows Platform (UWP / Xbox) app that runs ScummVM games,
reusing the working app shell from `dosbox-pure-uwp` and the **in-tree,
maintained** ScummVM `backends/platform/libretro` core.

## 2. Core strategy (why this route)

- ScummVM upstream **removed** its old WinRT/UWP backend from mainline.
- The libretro backend is **in-tree and maintained**, and — critically — its
  Makefile already ships a `windows_msvc2017_uwp` platform target:

  ```
  else ifneq (,$(findstring uwp,$(PlatformSuffix)))
     WinPartition = uwp
     MSVC2017CompileFlags = -DWINAPI_FAMILY=WINAPI_FAMILY_APP -D_WINDLL \
        -D_UNICODE -DUNICODE -D__WRL_NO_DEFAULT_LIB__ -EHsc -FS
     LDFLAGS += -APPCONTAINER -NXCOMPAT -DYNAMICBASE -MANIFEST:NO -LTCG \
        -OPT:REF -SUBSYSTEM:CONSOLE -MANIFESTUAC:NO -OPT:ICF -WINMD:NO
     LIBS += WindowsApp.lib
  ```

  → compiles with `cl.exe`/`link.exe`, `WINAPI_FAMILY_APP`, `-APPCONTAINER`,
  links only `WindowsApp.lib`. Produces `scummvm_libretro.dll` as a UWP
  AppContainer DLL. Build invocation:
  `make platform=windows_msvc2017_uwp_x64`.

**Decision:** do NOT hand-port ScummVM's 200+ source files into a `.vcxproj`
(the dosbox model). Instead let ScummVM's own build system produce the core,
and integrate it into the reused dosbox-uwp app shell.

**Integration decision:** dynamic DLL. App loads `scummvm_libretro.dll` via
`LoadPackagedLibrary` + `GetProcAddress` (AppContainer requires
`LoadPackagedLibrary`, not `LoadLibrary`). This isolates the huge ScummVM build
from the app and keeps the core update-friendly.

**SDL:** NOT needed by the core. libretro UWP `LIBS` = `WindowsApp.lib` only;
the backend implements `OSystem` on the libretro API. SDL (`uwp-dep`) only
matters if the *frontend* wants gamepad input via `SdlInput.cpp` — deferred.

## 3. Reused scaffold (from dosbox-pure-uwp/dosbox-uwp)

| Component | File(s) | Verdict |
|---|---|---|
| libretro bridge | `Content/RetroCore.cpp/.h` | GENERIC — strip ~4 dosbox option keys + DBPS ToggleOSD; switch static symbols → pointer table |
| audio sink | `Content/XAudio2Output.cpp/.h` | GENERIC — parameterize 44100 → sample_rate |
| video renderer | `Content/RetroScreenRenderer.cpp/.h` (Direct2D) | GENERIC — add RGB565 path or force XRGB8888 |
| swapchain | `Common/DeviceResources.cpp/.h`, `StepTimer.h`, `DirectXHelper.h` | GENERIC drop-in |
| app shell | `App.cpp/.h`, `dosbox_uwpMain.cpp/.h` | GENERIC — replace dosbox self-pacing with real QPC pacing |
| input | `Content/SdlInput.cpp/.h` + main VK→RETROK map | GENERIC bridge, rebind maps |
| VFS (sandbox) | `extern/.../vfs_implementation_uwp.cpp` | GENERIC drop-in |
| `.vcxproj` core wiring | `dosbox-uwp.vcxproj` | DOSBOX-SPECIFIC — replaced by dynamic DLL load |
| `dosbox_pure_sta.cpp` | — | DOSBOX-SPECIFIC — delete |

## 4. Phases

### Phase 0 — Repo scaffold
- `scummvm-uwp/` new repo. Copy reusable shell from `dosbox-uwp/`.
- Submodules: `extern/scummvm` (upstream), `extern/uwp-xray-depot` (diagnostics).
  `uwp-dep` (SDL2) deferred.
- Git identity local = personal gmail (`Marcelo Frau <marcelofrau@gmail.com>`).
  NEVER `--global`.

### Phase 1 — Build the core DLL (highest risk, isolated) — **LITE=1 NO_WIP=1, SCUMM + SKY only**
- `make platform=windows_msvc2017_uwp_x64` under MSYS2/cygwin (Makefile needs a
  unix shell + `cygpath`).
- Override VS2017 → 2022 (`VsInstallRoot`, `WindowsSDKVersion`).
- Disable non-essential libs to shrink forbidden-API surface: `USE_CURL=0`,
  minimize `USE_VORBIS/MAD/MT32EMU`.
- Resolve engine-table generation (`detection_table.h`, `plugins_table.h`,
  `engines.mk`).
- **Output:** `scummvm_libretro.dll` that loads in AppContainer, plus
  `scummvm.zip` (datafiles + themes, version-matched).

### Phase 2 — Dynamic integration
- RetroCore: replace direct libretro symbols with a **function-pointer table**
  populated via `LoadPackagedLibrary("scummvm_libretro.dll")` + `GetProcAddress`
  (`retro_init/run/load_game/set_*_callback/get_system_av_info`, ...).
- Package the DLL inside the appx.

### Phase 3 — Adapt scaffold
- XAudio2Output: parameterize sample rate (ScummVM default 48000).
- RetroScreenRenderer: RGB565 path OR force core to XRGB8888 via
  `RETRO_ENVIRONMENT_SET_PIXEL_FORMAT`.
- Main loop: add **QPC pacing** — ScummVM does NOT self-pace (dosbox used
  `CPU_CycleMax`). Honor ScummVM "Frame rate cap" option.
- RetroCore: strip dosbox option keys; delete `dosbox_pure_sta.cpp`.
- VFS: drop-in; ScummVM uses multi-file games → relies on `opendir`/path side
  more than the in-memory `romData` blob.
- Input: rebind key/button maps for ScummVM controls.

### Phase 4 — Audio
- Reuse DRC plan from `dosbox-pure-uwp/docs/audio-fix/` (same XAudio2 sink).
  ScummVM's continuous stream is DRC-friendly.

### Phase 5 — Validation
- xray binds (`audio_queued/produced/consumed`). PASS = SCUMM/SKY runs, zero
  underruns, no pitch shift, no pinned CPU core.

## 5. Ranked risks
1. **3rd-party libs in AppContainer** — vorbis/mad/mt32emu/curl may call
   forbidden APIs. Mitigate: `USE_CURL=0`, disable non-essential `USE_*`.
2. **Engine-table generation** outside `./configure` may break under MSVC.
3. **Makefile hardcodes VS2017** — override for current toolchain.
4. **Pacing** — ScummVM needs its own timing loop (dosbox didn't).

## 6. Decisions log
- Route A (libretro) over Route B (SDL native OSystem_WinRT): libretro backend
  is in-tree/maintained; heavy work (build ScummVM tree in MSVC-UWP
  AppContainer) is common to both; Route B additionally needs a new backend.
- Dynamic DLL over static lib / vcxproj source list.
- First target: SCUMM + SKY (proof-of-pipeline), LITE, NO_WIP.
