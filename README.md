# ScummVM UWP — ScummVM on Xbox / Windows

Run **ScummVM** on Xbox Series (and Windows 10/11 Universal) using a
**RetroArch UWP shell** + the **ScummVM libretro core**. A thin C# launcher
drives the whole UX: splash screen, first-run bootstrap, config seeding, and a
clean handoff that returns the console to the dashboard when you quit a game.

## What it does

- One appx package, **two UWP applications**:
  - **ScummVMLauncher** (C#, visible tile "ScummVM") — splash + bootstrap.
  - **RetroArch UWP** (hidden, `AppListEntry="none"`) — rendering, audio,
    input, frame pacing. No custom RetroArch code.
- ScummVM runs as a **libretro core** (`scummvm_libretro.dll`, buildbot
  binary) started with no game content → ScummVM's own launcher GUI.
- Clean quit: ScummVM exit → RetroArch shutdown → launcher `cmd=exit` →
  Xbox dashboard. No RGUI menu, no OSD toast.
- English UI, orange `#CC6701` branding, 4 s splash floor, `gui_scale=150`
  on first run.

## Docs

| Doc | What it covers |
|---|---|
| `docs/ARCHITECTURE.md` | Current shipped architecture (components, protocols, flows, build/deploy) |
| `docs/DISCOVERIES.md` | Chronological log of every problem found and fixed (16 entries) |
| `docs/HANDOFF.md` | State of the project for the next agent/session |
| `docs/PORT-PLAN.md` | Design history and the Route C decision |
| `docs/FILESYSTEM.md` | UWP sandbox FS strategy (FromApp API) |
| `patches/scummvm/README.md` | ScummVM submodule patches + theme regen workflow |

## Repository layout

```
├── launcher/ScummVMLauncher/   C# UWP launcher (UI, bootstrap, handoff)
│   ├── system/scummvm.zip      Versioned: datafiles + patched themes (~76 MB)
│   └── cores/                  Core DLL lives here (gitignored, re-downloadable)
├── extern/scummvm              Submodule — clean upstream checkout
├── extern/retroarch            Submodule — clean upstream (XboxEmulationHub fork)
├── patches/scummvm/            0001 theme fix (essential), 0002 MSVC build (optional)
├── scripts/                    build / package / deploy / status helpers
├── assets/                     Branding sources (splash .pdn, Numix icons)
└── docs/                       Architecture, discoveries, handoff, plans
```

> `extern/scummvm` and `extern/retroarch` are kept **100% upstream** (no fork).
> Any delta lives as a patch in `patches/scummvm/`. The versioned
> `system/scummvm.zip` already contains the patched themes — regenerate it
> via the workflow in `patches/scummvm/README.md` when updating the core.

## Build & package (Windows)

Requires VS2022 Community (found via `vswhere`) and the Windows SDK.

```powershell
./scripts/build.ps1          # Restore + build Release\x64
./scripts/package.ps1        # Build + sign (certs/dosbox-uwp.pfx) → dist\ScummVM.appx
./scripts/install.ps1        # Add-AppxPackage locally
./scripts/run.ps1            # Install + launch (scummvm-launcher:)
./scripts/clean.ps1 / rebuild.ps1
./scripts/status.ps1         # Git/submodule/version/package state
```

Signing uses `certs/dosbox-uwp.pfx` (test/dev cert, password `dev`). Replace
with your own cert for real distribution.

## Deploy to Xbox (Developer Mode)

`scripts/deploy-xbox.ps1` deploys over the **Xbox Device Portal** (HTTPS
`:11443`, CSRF handshake). Copy `.env.example` → `.env` and set your Xbox IP /
credentials. It **coexists** with any existing RetroArch install on the console
— it never uninstalls, upgrades, or registers the `retroarch:` protocol.

```powershell
Copy-Item .env.example .env   # then edit XBOX_IP / XBOX_USER / XBOX_PASS
./scripts/deploy-xbox.ps1
```

## License

**GPL-3.0** — same as ScummVM. See `LICENSE`.

ScummVM core: [GPL-3.0](https://github.com/scummvm/scummvm) — RetroArch shell:
[XboxEmulationHub/RetroArch](https://github.com/XboxEmulationHub/RetroArch)
(GPL-3.0) — launcher UI icons: [Numix icon
theme](https://github.com/numixproject/numix-icon-theme) (GPL-3.0) — shell
base: [dosbox-uwp](https://github.com/marcelofrau/dosbox-uwp).
