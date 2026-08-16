<p align="center">
  <img src="docs/social-preview.jpg" alt="ScummVM UWP — Xbox / Windows" width="800"/>
</p>

<p align="center">
  <strong>Adventure games on Windows and Xbox — a native single-app libretro frontend running ScummVM's own UI.</strong>
</p>

<p align="center">
  <img alt="Status" src="https://img.shields.io/badge/status-in--development-orange?style=for-the-badge">
  <img alt="Platform" src="https://img.shields.io/badge/platform-Windows%20%7C%20Xbox-blue?style=for-the-badge">
  <img alt="Version" src="https://img.shields.io/github/v/release/marcelofrau/scummvm-uwp?style=for-the-badge">
  <img alt="Arch" src="https://img.shields.io/badge/architecture-x64%20only-lightgrey?style=for-the-badge">
  <img alt="License" src="https://img.shields.io/badge/license-GPL--3.0-red?style=for-the-badge">
</p>

---

## What is this?

**ScummVM UWP** is a UWP app for the classic point-and-click adventure engine
[ScummVM](https://www.scummvm.org/) (Monkey Island, Broken Sword, Discworld,
Kyrandia, Simon the Sorcerer…), built for **Windows 11** and **Xbox Series
(Dev Mode)**.

The app **is** a libretro frontend. A single executable
(`ScummVMLauncher.exe`, C++/CoreWindow) loads the **official ScummVM libretro
core** (`scummvm_libretro.dll`, the same binary RetroArch ships — maintained by
the ScummVM team) directly and renders ScummVM's **own launcher GUI and full
options** on screen: gamepad-driven, no external menu, no RetroArch, no second
app, no protocol handoff.

The frontend provides what a core needs — video (D2D/D3D11, RGB565→BGRA8888),
audio (XAudio2), gamepad/keyboard/mouse input, and filesystem access
(`runFullTrust` + FromApp VFS) — all in one process.

---

## Screenshots

<p align="center">
  <img src="docs/snapshots/boot-screen.png" alt="Boot splash" width="420"/>
  <img src="docs/snapshots/main-gui.png" alt="ScummVM launcher GUI" width="420"/>
</p>
<p align="center">
  <img src="docs/snapshots/the-dig-gameplay.png" alt="The Dig gameplay" width="420"/>
  <img src="docs/snapshots/full-throttle-gameplay-1.png" alt="Full Throttle gameplay" width="420"/>
</p>

<details>
  <summary>More screenshots</summary>

<p align="center">
  <img src="docs/snapshots/game-options.png" alt="Game options" width="420"/>
  <img src="docs/snapshots/global-options.png" alt="Global options" width="420"/>
</p>
<p align="center">
  <img src="docs/snapshots/the-dig-gameplay2.png" alt="The Dig gameplay (2)" width="420"/>
  <img src="docs/snapshots/full-throttle-gameplay-2.png" alt="Full Throttle gameplay (2)" width="420"/>
</p>
<p align="center">
  <img src="docs/snapshots/full-throttle-gameplay-3.png" alt="Full Throttle gameplay (3)" width="420"/>
  <img src="docs/snapshots/full-throttle-gameplay-4.png" alt="Full Throttle gameplay (4)" width="420"/>
</p>

</details>

---

## Quick Start

### 1. Install

**Windows:**
Download the latest `scummvm-uwp_<version>_x64.zip` from
[Releases](https://github.com/marcelofrau/scummvm-uwp/releases). Extract and run
the `.appx` (sideloading requires Developer Mode enabled in Windows Settings).

**Xbox:**
Enable Developer Mode on your Xbox and deploy the `.appx` via the
[Xbox Device Portal](https://learn.microsoft.com/en-us/gaming/gdk/_content/gc/features/live/testing-on-xbox-devkits).

### 2. Add Games

ScummVM ships with its own launcher GUI (the same one from the desktop version),
driven entirely by gamepad or keyboard. On first run it unpacks its data files
and themes (into `E:\scummvm\system`, falling back to `LocalState\system`)
automatically.

Games are added by pointing ScummVM at the folder that contains the game files
(e.g. the Monkey Island / Broken Sword CD folder) or a `.scummvm` file that
describes a game. Supported engines include the classic LucasArts SCUMM games,
Sierra AGI/Sierra games, Broken Sword, Discworld, the Kyrandia trilogy, Simon the
Sorcerer, and many more — see the full list on
[ScummVM.org](https://www.scummvm.org/).

### 3. Play

Connect a gamepad or use a USB keyboard + mouse. See [Controls](#controls).

---

## Controls

The app presents ScummVM through the core's standard RetroPad mapping, so the
launcher and the games are fully navigable with a controller. On the Xbox
controller the RetroPad buttons map as follows:

| Input | ScummVM function |
|-------|------------------|
| **Left Stick / D-Pad** | Move mouse cursor |
| **A** | Space |
| **B** | Enter |
| **X** | F5 (in-game menu) |
| **Y** | Escape |
| **LB** | Left mouse click |
| **RB** | Right mouse click |
| **RT** | Cursor fine control |
| **Select** | Toggle virtual keyboard |
| **Start** | Open ScummVM launcher / GUI |
| **Right Stick** | Arrow keys (↑ ↓ ← →) |
| **LT / L3 / R3** | Unused (no default mapping) |

This is the same mapping the ScummVM libretro core ships upstream — you can
re-bind any of it inside ScummVM (Options → Keymaps). A USB **keyboard** and
**mouse** work as well, with the desktop key bindings (F5 in-game menu,
Ctrl+F5 quick save, etc.). See
[docs/CONTROLS.md](docs/CONTROLS.md) for the full reference, including cursor
behavior options.

---

## Features

| Feature | Status |
|---------|--------|
| ScummVM core (official libretro buildbot binary, no recompile) | ✅ Shipped |
| Own libretro frontend (single app, one process, no RetroArch) | 🚧 In progress |
| ScummVM's own GUI + options (gamepad-driven) | ✅ Works |
| Clean quit to dashboard (`RETRO_ENVIRONMENT_SHUTDOWN`) | 🚧 In progress |
| Data files + patched themes bundled (`system/scummvm.zip`, versioned) | ✅ Done |
| Game Options (Misc tab) crash — fixed (theme version skew) | ✅ Done |
| `gui_scale=150`, English UI, orange `#CC6701` branding | ✅ Done |
| Xbox deploy via Device Portal (coexists with real RetroArch) | ✅ Done |
| Self-signed packaging + GitHub Releases CI (`v*` tag) | ✅ Done |
| Automatic version sync with the bundled core | ✅ Done |
| Filesystem: `runFullTrust` + FromApp VFS (sandbox) | ✅ Done |
| E: staging (data) + LocalState fallback | 🚧 In progress |

---

## Versioning

The app version always mirrors the **shipped core**:

`<ScummVM base>.<build counter>` → e.g. **2026.3.1.7**

- Base comes from `cores/scummvm_libretro.info` (`display_version = "2026.3.1git"`
  → `2026.3.1`), so a core upgrade is always visible in the app version.
- The build counter is bumped on every build (`build_counter.txt`) by
  `tools/version.ps1`, wired as a PreBuildEvent in the launcher project.
- A release is cut by tagging `v2026.3.1.<N>` — the CI builds, packages, and
  publishes it automatically.

See [VERSIONS.md](VERSIONS.md) for the full matrix.

---

## Building from Source

### Prerequisites

- **Visual Studio 2022** Community (found via `vswhere`) with the UWP workload
- **Windows SDK 10.0.26100.0**
- **x64 only** — ARM/ARM64/x86 not supported (Xbox Series is x64)

### Build

```powershell
./scripts/build.ps1 -Configuration Release -Platform x64
```

Restores NuGet packages, then builds `Release\x64`.

### Package (APPX + release zip)

```powershell
./scripts/package.ps1 -Configuration Release -Platform x64
```

Builds (unless `-SkipBuild`), signs the appx with the cert in `certs/`
(`dosbox-uwp.pfx` locally; CI generates its own), and produces:

- `dist\ScummVM.appx`
- `scummvm-uwp_<version>_x64.zip` — the appx + x64 dependencies, ready to share

### Run (Windows)

```powershell
./scripts/run.ps1 -Configuration Release -Platform x64
```

### Deploy (Xbox)

Deploy is **manual** through the Xbox Device Portal (Dev Mode):

1. Open `https://<ip>:11443` in a browser (username/password from Dev Mode).
2. Go to **Apps** → **Add** → upload the `.appx` from `dist\` (or the
   `AppPackages\…_x64_Test\` output).
3. Click **Deploy** and launch the app.

The portal never uninstalls or upgrades an existing RetroArch install on the
console.

### CI/CD

`.github/workflows/release.yml` — triggered by a `v*` tag or manually
(`workflow_dispatch`):

1. Checkout with **recursive submodules + LFS** (the versioned `scummvm.zip`
   lives in LFS).
2. Generate a self-signed cert, build, package (`appx` + deps).
3. Upload artifact and create the GitHub Release with static
   `release_notes.md` body.

---

## Project Structure

```
scummvm-uwp/
├── launcher/ScummVMLauncher/          ← C++/CoreWindow libretro frontend (single app)
│   ├── App.cpp / ScummVMMain.cpp      ← app lifecycle + CoreWindow loop
│   ├── Content/RetroCore.cpp          ← threaded core wrapper + env handler
│   ├── Content/RetroScreenRenderer.*  ← D2D, RGB565→BGRA8888
│   ├── Content/RetroD3D11Renderer.*   ← D3D11 swapchain
│   ├── Content/XAudio2Output.*        ← audio output
│   ├── Content/SdlInput.*             ← gamepad → RetroPad
│   ├── Common/DeviceResources.*       ← D3D11 device resources
│   ├── extern/libretro-common/        ← vendored VFS (vfs_implementation_uwp.cpp)
│   ├── system/scummvm.zip             ← versioned: datafiles + patched themes (LFS)
│   ├── cores/                         ← scummvm_libretro.dll + .info (dll gitignored)
│   └── Package.appxmanifest           ← one app entry; runFullTrust + broadFileSystemAccess
├── extern/
│   ├── scummvm/                       ← Submodule — clean upstream checkout
│   └── retroarch/                     ← Submodule — LEGACY (removed in Fase 3)
├── patches/scummvm/                   0001 theme fix (essential)
├── tools/version.ps1                  ← PreBuildEvent: sync version with the core
├── scripts/                           ← build / package / run / status
├── assets/                            ← Branding sources (splash, tiles, social preview)
├── docs/                              ← Architecture, implementation plan, discoveries, handoff
└── .github/workflows/release.yml      ← CI: build + sign + release on v* tag
```

> `extern/scummvm` is kept **100% upstream** (no fork). Any ScummVM delta lives
> as a patch in `patches/scummvm/`, and the versioned `system/scummvm.zip`
> already ships the patched themes — regenerate it via the workflow in
> `patches/scummvm/README.md` when updating the core.

---

## For Developers

### How It Works

A **single process** plays the whole libretro host:

1. **Launch** — native branded splash holds while the bootstrap unpacks
   `system/scummvm.zip` into `E:\scummvm\system` (idempotent via the
   `.scummvm-ready` version flag; `LocalState\system` is the fallback) and
   writes a minimal `scummvm.ini` (`gui_theme=scummremastered`,
   `gui_scale=150`) if absent.
2. **Core** — `LoadLibrary("cores\\scummvm_libretro.dll")` +
   `GetProcAddress` for the `retro_*` API. The environment handler answers the
   core's queries: `GET_VFS_INTERFACE` (FromApp VFS), `GET_SYSTEM_DIRECTORY`,
   `GET_SAVE_DIRECTORY`, `GET_LIBRETRO_PATH`, logging; returns `false` for
   hardware rendering / MIDI interface / input bitmask (core falls back to
   software RGB565 + internal synth).
3. **Run** — `retro_load_game(NULL)` boots ScummVM's own launcher GUI. The
   emulation thread calls `retro_run`; frames (RGB565) are converted to
   BGRA8888 and presented via D2D/D3D11; audio streams out of XAudio2;
   gamepad events flow in through SdlInput.
4. **Quit** — ScummVM's GUI shutdown fires `RETRO_ENVIRONMENT_SHUTDOWN` → the
   frontend tears down the core and calls `CoreApplication::Exit()` → dashboard.

> **Why our own frontend?** The ScummVM libretro core is maintained by the
> ScummVM team itself — using the buildbot binary means every engine fix and
> update arrives for free. It owns its whole UI (launcher, options, file
> browser), so the app only needs the plumbing: video, audio, input, and
> filesystem. A proven standalone host exists (`dosbox-pure-unleashed-uwp`) and
> is ported here. `runFullTrust` keeps the core's `fopen` working in-process —
> the same conditions RetroArch already exercises on Xbox.

### Key Technical Decisions

| Decision | Choice | Why |
|----------|--------|-----|
| Frontend | Own libretro host (port of dosbox-pure-unleashed-uwp) | Proven standalone UWP host: render, audio, input, VFS — no RetroArch binary |
| Core | Bundled buildbot `scummvm_libretro.dll` (dynamic load) | Same binary RetroArch ships; no local core build |
| Core source | `extern/scummvm` kept clean upstream | Deltas live as patches (`patches/scummvm/`) |
| Boot | `retro_load_game(NULL)` + `SET_SUPPORT_NO_GAME` | ScummVM's own GUI renders directly |
| Pixel format | RGB565 → BGRA8888 (CPU) | Software-mode core output; D2D bitmap is BGRA |
| Filesystem | `runFullTrust` + FromApp VFS | Core `fopen` works; VFS is the core's preferred path |
| MIDI / HW render | env → `false` | Core falls back to internal synth / software rendering |
| Quit | `RETRO_ENVIRONMENT_SHUTDOWN` → `CoreApplication::Exit()` | No second app, no protocol chain |
| Versioning | Derived from the shipped core's `display_version` | App version always tracks the real ScummVM version |
| Theme fix | Patched themes inside the versioned zip | Newer core registers a `GameOptions_Misc` dialog the older themes lacked (fatal tab crash — fixed) |

### Submodule Policy

- **Never commit to `extern/scummvm`** — patches go in `patches/scummvm/`.
- When the core updates: update the submodule, apply `0001`, regenerate the
  themes, **commit the new zip**, revert the submodule. Full workflow in
  `patches/scummvm/README.md`.
- `extern/retroarch` is legacy from the old shell — removed in Fase 3.

### Known Gotchas

| Issue | Detail |
|-------|--------|
| Version skew | The buildbot core can be ahead of the theme sources — patch `0001` fixes `GameOptions_Misc`; regenerate `scummvm.zip` on every core update |
| RGB565 | Software-mode frames are 2 bpp — conversion to BGRA8888 needed in both GUI and game paths |
| Audio sample rate | Core default (44.1k/48k) must match the XAudio2 device format |
| Restore required | Deleting `obj/` without a `/t:Restore` first breaks with WMC1006 — always use `scripts/build.ps1` |
| LFS | `system/scummvm.zip` (~76 MB) is LFS-tracked; CI uses `lfs: true` |
| DLL over limit | `scummvm_libretro.dll` (~124 MB) exceeds GitHub's 100 MB file limit — gitignored, re-download from the buildbot |
| Version downgrade | UWP refuses to install a package with a lower version than the installed one — always bump the counter |
| Coexistence | Never touch the user's real RetroArch package; our app registers no protocols |

---

## Documentation

| Document | Description |
|----------|-------------|
| [Implementation Plan](docs/IMPLEMENTATION-PLAN.md) | **Live checklist** for the single-app migration |
| [Architecture](docs/ARCHITECTURE.md) | Target architecture: components, core contract, bootstrap, quit, build/deploy |
| [Versions](VERSIONS.md) | Version matrix and how the version is computed |
| [Discoveries](docs/DISCOVERIES.md) | Chronological log of every problem found and fixed |
| [Handoff](docs/HANDOFF.md) | Project state for the next agent/session |
| [Port Plan](docs/PORT-PLAN.md) | Design history and the Route D decision |
| [Filesystem](docs/FILESYSTEM.md) | UWP FS strategy (`runFullTrust` + FromApp VFS) |
| [Staging](docs/STAGING.md) | E: staging of ScummVM data (C++) |
| [Controls](docs/CONTROLS.md) | Full controller / keyboard / mouse reference for the ScummVM core |
| [Patch Workflow](patches/scummvm/README.md) | Theme patches + zip regeneration workflow |

---

## Dependencies & Credits

| Project | Role | License |
|---------|------|---------|
| [ScummVM](https://github.com/scummvm/scummvm) | The adventure game engine itself. Shipped as the libretro buildbot binary + included as submodule at `extern/scummvm`. | GPL-3.0 |
| [libretro-common](https://github.com/libretro/libretro-common) | VFS implementation (FromApp) compiled into the frontend | MIT |
| [dosbox-pure-unleashed-uwp](https://github.com/schellingb/dosbox-pure) | Reference standalone UWP libretro host (ported frontend) | GPL-2.0 |

---

## License

**GPL-3.0** — same as ScummVM. See [`LICENSE`](LICENSE) for the full text.

- ScummVM core: [GPL-3.0](https://github.com/scummvm/scummvm)
- Frontend: ported from dosbox-pure-unleashed-uwp (GPL-2.0) under GPL-3.0 terms
