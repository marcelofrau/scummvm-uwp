# ScummVM UWP Port — Plan (Route C: RetroArch UWP + bundled core)

Status: **active**. Author: Marcelo Frau. Last updated: 2026-07-27.

This document is the source of truth for the ScummVM UWP port.

## 1. Goal

Ship a Universal Windows Platform (UWP / Xbox) app that runs ScummVM games,
**reusing RetroArch UWP as the frontend** and bundling the ScummVM libretro
core as a DLL inside the appx package.

## 2. Core strategy

### Why RetroArch UWP as base

Previous approach (custom shell from dosbox-uwp scaffold) required reimplementing
rendering, audio, input, pacing, and video sync — all things RetroArch already
does correctly on UWP/Xbox. The ScummVM libretro core already has its own full
GUI launcher (game list, settings, browser) that renders into the libretro video
output buffer. This means:

- **RetroArch** handles: D3D rendering, XAudio2, gamepad/keyboard/mouse input,
  core loading (`LoadPackagedLibrary`), frame pacing, vsync, settings UI.
- **ScummVM core** handles: game detection, its own GUI launcher, in-game
  rendering, audio mixing, input mapping, save/load.
- **We handle**: app packaging, file picker integration for UWP sandbox, and
  bootstrapping RetroArch to auto-load the ScummVM core.

### Key architectural insight

When ScummVM's libretro core starts with no game content:
1. `retro_load_game(NULL)` is called
2. Core runs `scummvm_main("scummvm")`
3. No active domain → `launcherDialog()` fires
4. **ScummVM's full built-in GUI renders** (game list, options, browser)
5. RetroArch just displays the video frames — it's a dumb renderer

This means the ScummVM GUI works end-to-end without any custom bridge code.

### Build strategy for the core DLL

The ScummVM libretro core is built separately via its own Makefile:

```
make platform=windows_msvc2017_uwp_x64 \
  VsInstallRoot="C:/Program Files/Microsoft Visual Studio/2022/<Edition>" \
  WindowsSDKVersion="10.0.XXXXX.0" \
  LITE=1 NO_WIP=1 USE_CURL=0
```

- Requires MSYS2/cygwin (Makefile uses unix shell + `cygpath`)
- Requires VS2022 Build Tools (cl.exe + link.exe + Windows SDK)
- `VsInstallRoot` and `WindowsSDKVersion` are overridable via `?=` in Makefile
- Output: `scummvm_libretro.dll` (AppContainer DLL, links only `WindowsApp.lib`)

Alternative: use a **pre-built DLL** from CI or a release, skipping local build
entirely. Risk: must confirm CI builds with UWP target to avoid forbidden APIs.

### No custom rendering/audio/input needed

The old plan required custom code for:
- ~~Video rendering (Direct2D)~~ → RetroArch handles D3D11
- ~~Audio sink (XAudio2)~~ → RetroArch handles XAudio2
- ~~Input bridge (VK→RETROK)~~ → RetroArch handles gamepad/keyboard
- ~~Frame pacing (QPC loop)~~ → RetroArch handles vsync/pacing
- ~~Pixel format negotiation~~ → RetroArch + core negotiate automatically
- ~~VFS UWP implementation~~ → ScummVM core uses WindowsFilesystemFactory

## 3. What we build

| Component | Source | Notes |
|---|---|---|
| RetroArch UWP binary | RetroArch release/CI build | Frontend — handles all rendering, audio, input |
| scummvm_libretro.dll | ScummVM Makefile `platform=windows_msvc2017_uwp_x64` | Core — bundled in appx |
| scummvm.zip | ScummVM `make datafiles` | Themes + datafiles, version-matched to core |
| App bootstrap | New minimal UWP app | Launches RetroArch core, custom file picker |
| retroarch.cfg | Config file | Auto-load ScummVM core, skip RetroArch menu |

### Appx package structure

```
scummvm-uwp.appx
├── scummvm-uwp.exe          # Bootstrap app (or RetroArch.exe itself)
├── RetroArch-msvc*.dll      # RetroArch runtime (if not merged into exe)
├── cores/
│   └── scummvm_libretro.dll # Bundled core
├── scummvm.zip              # Themes + datafiles
├── retroarch.cfg            # Pre-configured to auto-load core
└── Assets/                  # Icons, splash, etc.
```

## 4. Phases

### Phase 0 — Repo scaffold (DONE)
- `scummvm-uwp/` repo. Submodules: `extern/scummvm`, `extern/uwp-xray-depot`.
- Current shell from dosbox-uwp is legacy — will be replaced by RetroArch base.

### Phase 1 — Build the core DLL
- `make platform=windows_msvc2017_uwp_x64` under MSYS2/cygwin.
- Override VS2017 → VS2022 (`VsInstallRoot`, `WindowsSDKVersion`).
- Start with `LITE=1 NO_WIP=1 USE_CURL=0`, engines SCUMM + SKY only.
- Verify DLL loads in AppContainer (no forbidden Win32 APIs).
- **Output:** `scummvm_libretro.dll` + `scummvm.zip`.

### Phase 2 — RetroArch UWP integration
- Obtain RetroArch UWP build (binary release or build from source).
- Create appx package with RetroArch + scummvm core + config.
- Configure `retroarch.cfg`: set `libretro = "cores/scummvm_libretro.dll"`,
  `libretro_content = ""` (empty = launch core without content → ScummVM GUI).
- Verify: app launches → ScummVM launcher GUI appears → game list works.

### Phase 3 — File picker + UWP sandbox
- ScummVM's built-in browser uses `WindowsFilesystemFactory` on Windows.
- On UWP, game files must be in app-accessible locations (LocalFolder, or
  user-picked via `Windows::Storage::Pickers::FolderPicker`).
- Implement a thin UWP file picker bridge: user picks game folder → files
  copied/mapped to LocalFolder → ScummVM detects them via Mass Add or
  manual addition in its GUI.
- Test: pick a SCUMM game folder → appears in ScummVM launcher → runs.

### Phase 4 — Xbox-specific adaptations
- Xbox has restricted filesystem — all games must come through file picker
  or be pre-packaged in appx.
- Controller mapping: ScummVM core already maps RetroPad buttons, but
  ScummVM's GUI needs mouse emulation (right stick → mouse pointer).
- Test on Xbox Dev Mode.

### Phase 5 — Validation + packaging
- SCUMM + SKY engines run correctly.
- Audio: no underruns, correct sample rate, no pitch shift.
- Video: correct resolution, vsync, no tearing.
- Input: gamepad works in-game, GUI navigation works.
- Package as .appx/.appxbundle for sideloading or store.

## 5. Risks

1. **RetroArch UWP binary availability** — need to confirm a UWP build exists
   or build from source. RetroArch's own UWP port may lag behind mainline.
2. **3rd-party libs in AppContainer** — vorbis/mad/mt32emu may call forbidden
   APIs. Mitigate: `USE_CURL=0`, disable non-essential `USE_*` at build time.
3. **Engine-table generation** outside `./configure` may break under MSVC.
4. **Makefile hardcodes VS2017** — override via `VsInstallRoot ?=` on cmdline.
5. **UWP file sandbox** — games must be accessible. User picks folders via
   `FolderPicker`, files copied to LocalFolder.
6. **Xbox restrictions** — tighter sandbox, no arbitrary filesystem access.

## 6. Decisions log

- **2026-07-13**: Route A (libretro) over Route B (SDL native OSystem_WinRT).
  Dynamic DLL over static lib. First engines: SCUMM + SKY.
- **2026-07-27**: **Route C (RetroArch UWP as base)** over Route A (custom shell).
  RetroArch already handles rendering, audio, input, pacing correctly on UWP.
  ScummVM core has its own GUI launcher. No need to reimplement bridge code.
  Only custom work: file picker + UWP sandbox integration + bootstrap config.
