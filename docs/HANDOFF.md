# HANDOFF — ScummVM UWP

Last updated: 2026-10-19. For the next agent/session. Read this +
`ARCHITECTURE.md` + `IMPLEMENTATION-PLAN.md` (live checklist) + `PORT-PLAN.md` +
`DISCOVERIES.md` + `FILESYSTEM.md` + `XBOX-TILE-LAUNCH-KILL.md` +
`patches/scummvm/README.md` first.
Conversation language: Portuguese. Docs: English (execution notes may be PT).

## ✅ Resolved blocker (2026-10-19)

v2026.3.1.62 closed immediately when launched from the Xbox tile (worked under
F5). **Fixed and verified from the tile** — fix `58a476e` + docs `6de71e2`,
merged to `main`. Root cause, resolution and do-not list:
`docs/XBOX-TILE-LAUNCH-KILL.md`. Note: parts of this file predate the GPU path
(`SdlMain.cpp`, SDL2 + Mesa, `SET_HW_RENDER` accepted); the doc above describes
the current launch path.

## Latest change (2026-10-19) — core updated to buildbot nightly

`cores/scummvm_libretro.dll` replaced with the libretro buildbot nightly of
**2026-10-19** (137.296.896 bytes; previous 2026-08-03 binary kept at
`cores-backup/scummvm_libretro.dll.bak-20260803`, gitignored). Still reports
`ScummVM 2026.3.1git` → app base unchanged (`2026.3.1`). Full record:
`DISCOVERIES.md` §19. The new core doubles the emu cothread stack to 1 MB
(`5f69c64d`), relevant to the tile-kill mechanism — see
`XBOX-TILE-LAUNCH-KILL.md` §10c. Build produced `2026.3.1.154` (Release +
Debug). **Not yet verified from the tile.**

## Where we are

**Single-app, own-frontend architecture (Route D) shipped and running.** The
frontend is its own libretro host: it loads the buildbot `scummvm_libretro.dll`
via `LoadLibrary` + `GetProcAddress` and boots it with `retro_load_game(NULL)`,
so the core's own GUI renders directly. Launch path is `SDL_WinRTRunApp`
(`SdlMain.cpp`), SDL owns the CoreWindow/event loop, **Mesa WGL OpenGL** renders
hardware (SW/GL mode is selected by the `scummvm_video_hw_acceleration` core
option; GL is the shipped/default path). See `IMPLEMENTATION-PLAN.md` for the
checkbox state and `ARCHITECTURE.md` §10 for the GL path.

The old architecture (C# launcher → RetroArch shell, 2 apps) is **obsolete and
removed**: protocols, `retroarch.cfg` seeding, core-missing detection, E:
staging of RA config. The RetroArch **real** install of the user is untouched at
all times.

## Strategy (final)

**Route D: own libretro frontend (single app).** Port the proven standalone
UWP libretro host from `dosbox-pure-unleashed-uwp` (threaded `RetroCore`,
D2D/D3D11, XAudio2, gamepad, FromApp VFS), load the **official buildbot
`scummvm_libretro.dll`** via `LoadLibrary` + `GetProcAddress`, and boot it with
`retro_load_game(NULL)` so the core's own GUI renders directly. **ScummVM is
never recompiled.** No RetroArch binary anywhere.

- Process is `runFullTrust` (manifest) → core `fopen`/`StdioStream` work
  in-process; VFS FromApp (`vfs_implementation_uwp.cpp`) is the core's
  preferred file path and the safety net.
- Core env: `GET_SYSTEM_DIRECTORY`/`GET_SAVE_DIRECTORY`/`GET_LIBRETRO_PATH`
  real; `GET_MIDI_INTERFACE`/`GET_INPUT_BITMASKS` → false;
  `SET_HW_RENDER` **accepted** (Mesa WGL context provided) so the core runs the
  GL path.
- Pixel format: GL mode uses a **SHARED FBO** (core thread creates the context,
  render thread blits FBO → screen); SW mode is RGB565 → BGRA8888.
- Quit: core fires `RETRO_ENVIRONMENT_SHUTDOWN` →
  `s_exitRequested` → `CoreApplication::Exit()`.

## Submodule hygiene (IMPORTANT)

`extern/scummvm` stays a **clean upstream checkout** — no fork, no local
commits. It is a **reference only**: the shipped core is the **buildbot binary**,
never a local rebuild. All ScummVM deltas live in `patches/scummvm/`:

| Patch | Use | Status |
|---|---|---|
| `0001-gameoptions-misc-theme.patch` | Game Options Misc theme fix | **Applied inside the versioned zip**; reapply when regenerating themes |
| `0002-msvc-libretro-build.patch` | MSVC core build | **OBSOLETE** — we load the buildbot DLL, never rebuild the core |

`system/scummvm.zip` is versioned (patched themes + datafiles).
`cores/scummvm_libretro.dll` stays gitignored (~137 MB; download the nightly
from the libretro buildbot when updating).

**Core update workflow** (see `VERSIONS.md` and `DISCOVERIES.md` §19):
1. Download the new buildbot nightly → `cores/scummvm_libretro.dll`; back up the
   old one to `cores-backup/`.
2. Update the provenance comment in `cores/scummvm_libretro.info`.
3. Rebuild `scummvm.zip` **only if the base version changed** (datafiles/themes
   must match the core version).
4. Test from the **tile** before shipping.

## Dependencies (submodules)

| Path | Source | Status |
|---|---|---|
| `extern/scummvm` | ScummVM upstream master | KEPT — clean reference checkout; the shipped core is the buildbot binary |
| `extern/libretro-common` | (vendored under `launcher/ScummVMLauncher/extern/`) | VFS sources compiled into the frontend |
| `extern/retroarch` | XboxEmulationHub/RetroArch.git | **LEGACY — unused.** Still on disk + `.gitmodules` but referenced by no code/script. Flagged for removal (Fase 3 cleanup not completed). |
| `extern/spdlog` | gabime/spdlog.git | **LEGACY — unused.** `LogHelper.h` is a self-contained spdlog-compatible shim ("No fmt/spdlog dependency"); nothing includes real spdlog. Flagged for removal. |

**Caveat:** `scripts/fetch-payload.ps1` is **stale** — it still downloads the old
RetroArch UWP shell (`RetroArch-SeriesConsoles.appx` / `RetroArch-msvcUWP.exe`).
That path is dead; only its ScummVM-core download step is still relevant. Do not
run it as-is; fetch the core DLL manually from the libretro buildbot.

## Reference projects (in user's workspace, do NOT move)

- `F:\workspace\vs2022\dosbox-pure-unleashed-uwp\dosbox-uwp` — **the port
  source**: `Content/RetroCore.*` (threaded core wrapper), `RetroD3D11Renderer`,
  `RetroScreenRenderer`, `XAudio2Output`, `SdlInput`, `Common/DeviceResources`,
  `App.cpp`/`dosbox_uwpMain.cpp`, plus `extern\libretro-common\vfs\*.cpp`
  (FromApp VFS). Do NOT port `FileBrowser`/`FrontendMenu`/`SettingsManager`
  (ScummVM has its own GUI).
- `F:\workspace\x-files-uwp` — C# UWP file manager; FromApp P/Invoke reference.
- `F:\workspace\vs2022\scummvm-uwp` — OLD repo, archive only (never consult).
- `F:\workspace\scummvm-uwp` — this repo (active).

## Key files

- `docs/IMPLEMENTATION-PLAN.md` — **live checklist**; tick as work lands.
- `launcher/ScummVMLauncher/` — the C++ frontend. Current layout: `main.cpp`
  (entry + ODS VEH + LogInit), `SdlMain.cpp` (SDL_WinRTRunApp lifecycle, Mesa GL,
  core thread + render thread), `Content/RetroCore.*` (threaded core wrapper),
  `Content/SdlInput.*`, `LogHelper.h` (logging shim), `Bootstrap.cpp` (staging),
  `cores/` (core DLL — gitignored), `mesa/` (GL DLLs), `system/scummvm.zip`.
- `launcher/ScummVMLauncher/system/scummvm.zip` — versioned datafiles +
  patched themes.
- `launcher/ScummVMLauncher/cores/scummvm_libretro.info` — carries the core
  provenance header (source, date, size) **and** the `display_version` that
  drives the app version.
- `scripts/build.ps1` / `package.ps1` / `clean.ps1` / `rebuild.ps1` / `release.ps1`
  — MSBuild + sign (`certs/dosbox-uwp.pfx`, pw `dev`). `build.ps1` runs
  `/t:Restore` first; use `$env:SCUMMVM_DONT_INCREMENT=1` for test builds.
- `scripts/fetch-payload.ps1` — **stale** (downloads the dead RetroArch shell);
  fetch the core DLL from the libretro buildbot manually.
- Xbox deploy — **manual** via Device Portal (`https://<ip>:11443`, port
  11443, CSRF via `/api/os/info`, field `InstalledPackages`). Never touches the
  user's real RetroArch.

## Reference

- `IMPLEMENTATION-PLAN.md` = live task tracker.
- `PORT-PLAN.md` = architecture decisions (Route D log).
- `ARCHITECTURE.md` = target architecture (single-app frontend).
- `FILESYSTEM.md` = FS strategy (runFullTrust + FromApp VFS).
- `VERSIONS.md` = version scheme + core-update workflow.
- `XBOX-TILE-LAUNCH-KILL.md` = tile-launch survival (read before touching startup).
- `DISCOVERIES.md` = full chronological log (append as you find/fix).
