# ScummVM UWP Port — Plan (Route D: frontend libretro próprio, single-app)

Status: **active**. Author: Marcelo Frau. Last updated: 2026-08-14.

Design history e decisões. O checklist operacional vive em
`IMPLEMENTATION-PLAN.md`; a arquitetura-alvo em `ARCHITECTURE.md`.

## 1. Goal

Ship a **single** UWP app (Xbox / Windows) that runs ScummVM games by embedding
the official **libretro core** (`scummvm_libretro.dll`, buildbot binary) directly
into our own frontend — no RetroArch binary, no second app, no protocol
handoff. The frontend implements what RetroArch does for a core (video, audio,
input, filesystem) natively.

## 2. Core strategy

### Route D — own libretro frontend (chosen 2026-08-14)

- One package, one process, one app.
- Frontend = port of **dosbox-pure-unleashed-uwp** (`vs2022` workspace): a
  proven standalone UWP libretro host (threaded `RetroCore` wrapper, D2D/D3D11
  presentation, XAudio2, gamepad input, FromApp VFS). ScummVM core replaces the
  dosbox core.
- Core is loaded **dynamically** (`LoadLibrary("cores\\scummvm_libretro.dll")`
  + `GetProcAddress` on the `retro_*` symbols) — unlike dosbox-uwp, which
  statically links its core. **ScummVM is never recompiled.**
- The ScummVM core runs **without content** (`retro_load_game(NULL)` via
  `RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME`) → its own launcher GUI renders
  directly into our video output. ScummVM owns its full UI; we own the plumbing.
- Filesystem: the core's file calls go through libretro VFS
  (`GET_VFS_INTERFACE` → `vfs_implementation_uwp.cpp`, FromApp APIs), and the
  process is `runFullTrust`, so the core's `fopen` also works in-process.
  No core patching.

### Why not keep RetroArch (Route C)

- Two apps + two processes + protocol handoff (`scummvm-core:`,
  `scummvm-launcher:`) was fragile and confusing.
- Detect/launch of the user's **real** RetroArch had a core-missing fallback
  that was one run behind and depended on parsing a log marker.
- `retroarch.cfg` seeding/rewriting and `system_directory` tricks were needed
  just to point the shell at our ScummVM system dir.
- All of that disappears when the frontend is ours.

### Why the core stays a libretro DLL

- The ScummVM team maintains the libretro core; using the buildbot binary gets
  every engine fix for free and avoids a local MSVC core build.
- Memory/threading/rendering is exactly what RetroArch already exercised with
  this binary on Xbox — same conditions, our process.

## 3. What we build

| Component | Source | Notes |
|---|---|---|
| Frontend | port of `vs2022\dosbox-pure-unleashed-uwp\dosbox-uwp` | `RetroCore`, renderers, `XAudio2Output`, `SdlInput`, `Common\DeviceResources`, CoreWindow `App` |
| `libretro.h` + libretro-common VFS | vendor from dosbox repo's `extern\libretro-common` | `vfs.h`, `vfs_implementation.h`, `vfs_implementation_uwp.cpp` |
| `cores/scummvm_libretro.dll` | libretro buildbot (official) | LoadLibrary'd, not recompiled |
| `system/scummvm.zip` | generated + versioned | datafiles + patched themes (patch `0001`) |
| Bootstrap/staging (C++) | rewrite of the old C# logic | zip → `LocalState\system`, `scummvm.ini`, version flag |

**Not ported** from dosbox-uwp (ScummVM has its own GUI): `FileBrowser`,
`FrontendMenu`, `AboutDialog`, `ConfirmDialog`, `SettingsManager`.

## 4. Phases (detalhe operacional em IMPLEMENTATION-PLAN.md)

1. **Docs** — rewrite architecture/plan/handoff/README for single-app.
2. **Frontend skeleton** — vendor libretro-common, port frontend, dynamic core
   load, ScummVM env handler (SYSTEM/SAVE/LIBRETRO_PATH real; MIDI/HW_RENDER/
   BITMASKS → false), RGB565 frame path, `retro_load_game(NULL)` → ScummVM GUI.
3. **Bootstrap + staging** — C++ reimplementation, version-gated E: staging
   with LocalState fallback.
4. **Manifest + cleanup** — single app entry, drop protocols, keep
   `runFullTrust` + `broadFileSystemAccess`, remove RetroArch payload,
   `SHUTDOWN → CoreApplication::Exit`.
5. **Xbox verification** — deploy via Device Portal; GUI/themes/games
   (SCUMM+SKY), audio/video/saves, quit; per-game pass/fail.
6. **Release** — final docs + green CI with single app.

## 5. Risks

1. **Core threading** — ScummVM core spawns its own emu thread
   (`retro_init_emu_thread`); RetroCore's threaded model must accommodate it.
   Verify early.
2. **RGB565** — software-mode frames are 2 bpp; the D2D bitmap is BGRA8888 →
   conversion needed. Low risk, must be in both GUI and game paths.
3. **Audio sample rate** — core default (44.1k/48k) must match XAudio2 device.
4. **DLL import surface** — buildbot DLL imports regular Win32 APIs; safe under
   `runFullTrust` (same as RetroArch today), but verify it loads cleanly.
5. **MIDI** — frontend returns `false` for `GET_MIDI_INTERFACE`; core falls back
   to its internal synth. Confirm MT-32/AdLib paths still produce audio.
6. **Bootstrap rewrite** — E: staging moves from C# FromApp P/Invoke to C++.
   Full-trust allows plain `fopen`; FromApp kept for parity.

## 6. Decisions log

- **2026-07-13**: Route A (libretro) over Route B (SDL OSystem_WinRT).
- **2026-07-27**: Route C (RetroArch UWP as base) over Route A. RetroArch
  already handles rendering/audio/input/pacing on UWP.
- **2026-08-05**: Refined Route C — RetroArch compiled from source
  (XboxEmulationHub fork) as a pure shell; custom work (filepicker, About,
  logging) lives in the ScummVM core via patches; FS via FromApp.
- **2026-08-14**: **Route D** over Route C. Own libretro frontend (port of
  dosbox-uwp), single app, dynamic load of the buildbot core, no RetroArch
  binary, no protocols, no config rewriting. No ScummVM recompilation. E:
  staging kept (ScummVM data only) + LocalState fallback. `runFullTrust` kept.
