# ScummVM UWP Launcher 2026.3.1.x

**The classic adventure engine, on Xbox and Windows.**

ScummVM lets you play over 300 classic graphic-adventure games — Monkey
Island, Day of the Tentacle, Sam & Max, Curse of Monkey Island, Indiana Jones
and the Fate of Atlantis, Broken Sword, Simon the Sorcerer, the Kyrandia
trilogy and many more — right on your Xbox console or Windows machine, with
full gamepad support.

ScummVM UWP Launcher exposes ScummVM's own launcher GUI and full options,
powered by RetroArch underneath. Uses an installed RetroArch when present;
falls back to a bundled shell otherwise.

## What's New

- **Uses your RetroArch when installed** — the launcher hands off to a real
  RetroArch on the console when it's present, so you keep every RetroArch
  feature and update (core updater, cloud saves, achievements, …).
- **Bundled fallback shell** — no RetroArch installed? The launcher runs the
  same official ScummVM libretro core inside a hidden RetroArch UWP shell; no
  separate RetroArch installation needed.
- **Official ScummVM core** — the libretro core maintained by the ScummVM team
  itself; every engine fix and update arrives for free.
- **Gamepad-first UX** — every menu and game is fully playable with the Xbox
  controller (see Controls below).
- **First-run bootstrap** — the launcher unpacks the datafiles + patched
  themes automatically on first launch, no manual setup.
- **Clean quit to dashboard** — exit returns straight to the Xbox dashboard;
  no RetroArch menus, no OSD toast, no stray menu on core shutdown.
- **Pre-configured RetroArch config** — RGUI menu, D3D11 video driver, log to
  file, correct system directory, and forced options that keep the shutdown
  path crash-free (no dummy-core menu, no OSD, no stale shaders).
- **Bundled game data** — `scummvm.zip` datafiles with the **ScummVM Remastered
  theme** and orange `#CC6701` branding.
- **Game Options fixed** — the "Misc" tab crash caused by theme version skew is
  resolved.
- **Automatic version sync** — app version tracks the shipped core's
  `display_version` on every build.
- **Self-signed CI pipeline** — tag `v*` triggers a GitHub Actions build,
  packages the `.appx` and publishes this release.
- **Full controls reference** — see `docs/CONTROLS.md` in the repository.

## Installation

1. Download `scummvm-uwp_2026.3.1.x_x64.zip`.
2. Extract it.
3. Install the `.appx`:
   - **Windows** — sideloading requires Developer Mode
     (Settings → Privacy & security → For developers).
   - **Xbox** — enable Developer Mode + Device Portal, then install through
     the portal (upload the `.appx` in **Apps** → **Add** → **Deploy**).
4. Launch **ScummVM UWP Launcher**, wait for the first-run setup, add your game
   folder and play.

If you want to use your own RetroArch instead of the bundled shell, install
the ScummVM core inside RetroArch via its core downloader — the launcher will
pick it up automatically.

## Controls (quick reference)

| Input | Function |
|-------|----------|
| **Left Stick / D-Pad** | Move mouse cursor |
| **A** | Space (interact) |
| **B** | Enter (confirm) |
| **X** | F5 in-game menu |
| **Y** | Escape (back / cancel) |
| **LB** | Left mouse click |
| **RB** | Right mouse click |
| **RT** | Cursor fine control |
| **Select** | Virtual keyboard |
| **Start** | ScummVM launcher / GUI |

A USB keyboard and mouse work too, with the standard ScummVM desktop
bindings. Full reference and cursor options: [docs/CONTROLS.md](docs/CONTROLS.md).

## Requirements

- Xbox Series X|S / Xbox One (Developer Mode), or Windows 10/11 (x64 only).
- Game data files (GOG / Steam / original CDs) — ScummVM never ships game data.

## Notes & Limitations

- **x64 only** — ARM/ARM64/x86 not supported (Xbox Series is x64).
- Save / state via ScummVM's own save system; RetroArch save states and
  rewind are not supported by the core.
- The app coexists with a real RetroArch install on the same Xbox — when
  present, the launcher uses it via the standard `retroarch:` protocol (the
  ScummVM core must be installed inside it); otherwise it falls back to its
  bundled shell. It never registers or overrides the `retroarch:` protocol
  itself.

## Links

- [ScummVM](https://www.scummvm.org/)
- [ScummVM core docs (libretro)](https://docs.libretro.com/library/scummvm/)
- [RetroArch UWP shell](https://github.com/XboxEmulationHub/RetroArch)
- [Source & issues](https://github.com/marcelofrau/scummvm-uwp)
