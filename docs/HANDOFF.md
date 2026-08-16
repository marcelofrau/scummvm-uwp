# HANDOFF — ScummVM UWP

Last updated: 2026-08-14. For the next agent/session. Read this +
`ARCHITECTURE.md` + `IMPLEMENTATION-PLAN.md` (live checklist) + `PORT-PLAN.md` +
`DISCOVERIES.md` + `FILESYSTEM.md` + `patches/scummvm/README.md` first.
Conversation language: Portuguese. Docs: English (execution notes may be PT).

## Where we are

**Mid-migration to a single-app, own-frontend architecture (Route D).** Fase 0
(docs) done; Fase 1 (frontend port, dynamic core load, ScummVM GUI on screen)
in progress — see `IMPLEMENTATION-PLAN.md` for the checkbox state.

The old architecture (C# launcher → RetroArch shell, 2 apps) is **obsolete** and
being removed: protocols, `retroarch.cfg` seeding, core-missing detection,
E: staging of RA config all die. The RetroArch **real** install of the user is
untouched at all times.

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
  real; `GET_MIDI_INTERFACE`/`SET_HW_RENDER`/`GET_INPUT_BITMASKS` → false.
- Pixel format: **RGB565** (software mode) → convert to BGRA8888 for D2D.
- Quit: core fires `RETRO_ENVIRONMENT_SHUTDOWN` → `CoreApplication::Exit()`.

## Submodule hygiene (IMPORTANT)

`extern/scummvm` stays a **clean upstream checkout** — no fork, no local
commits. All ScummVM deltas live in `patches/scummvm/`:

| Patch | Use | Status |
|---|---|---|
| `0001-gameoptions-misc-theme.patch` | Game Options Misc theme fix | **Applied inside the versioned zip**; reapply when regenerating themes |
| `0002-msvc-libretro-build.patch` | MSVC core build | **OBSOLETE** — we load the buildbot DLL, never rebuild the core |

`system/scummvm.zip` is versioned (patched themes + datafiles).
`cores/scummvm_libretro.dll` stays gitignored (~124 MB; re-download from
buildbot when needed — see `scripts/fetch-payload.ps1`).

**Future core update workflow** (also in `patches/scummvm/README.md`):
1. Update `extern/scummvm` submodule.
2. Apply `0001` patch only (no MSVC build).
3. Regenerate themes → rebuild `scummvm.zip` → **commit the new zip**.
4. Revert the submodule to a clean state.

## Dependencies (submodules)

| Path | Source | Status |
|---|---|---|
| `extern/scummvm` | ScummVM upstream master | KEPT — clean; source of the core, patches only |
| `extern/retroarch` | https://github.com/XboxEmulationHub/RetroArch.git | **REMOVED in Fase 3** — was the old shell; no longer referenced |
| `extern/spdlog` | gabime/spdlog | REMOVED in Fase 3 (no RA shell) |
| `extern/libretro-common` | (vendored into the app from dosbox repo) | VFS sources compiled into the frontend |
| `extern/uwp-xray-depot` | — | REMOVED (already) |

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
- `launcher/ScummVMLauncher/` — will hold the C++ CoreWindow frontend
  (replaces the XAML app). Old C# files (`MainPage.xaml.cs`,
  `FromAppFile.cs`, `App.xaml.cs`, `SeedRetroArchConfig`) die in Fase 2/3.
- `launcher/ScummVMLauncher/system/scummvm.zip` — versioned datafiles +
  patched themes (unchanged).
- `scripts/build.ps1` / `package.ps1` / `clean.ps1` / `rebuild.ps1` — MSBuild +
  sign (`certs/dosbox-uwp.pfx`, pw `dev`). `build.ps1` runs `/t:Restore` first.
- `scripts/fetch-payload.ps1` — downloads core DLL + zip; RA payload removed in
  Fase 3.
- Xbox deploy — **manual** via Device Portal (`https://<ip>:11443`, port
  11443, CSRF via `/api/os/info`, field `InstalledPackages`). Never touches the
  user's real RetroArch.

## Reference

- `IMPLEMENTATION-PLAN.md` = live task tracker.
- `PORT-PLAN.md` = architecture decisions (Route D log).
- `ARCHITECTURE.md` = target architecture (single-app frontend).
- `FILESYSTEM.md` = FS strategy (runFullTrust + FromApp VFS).
- `DISCOVERIES.md` = full chronological log (append as you find/fix).
