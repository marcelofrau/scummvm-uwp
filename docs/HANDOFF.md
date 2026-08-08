# HANDOFF — ScummVM UWP port

Last updated: 2026-08-08. For the next agent/session. Read this +
`ARCHITECTURE.md` + `DISCOVERIES.md` + `PORT-PLAN.md` + `FILESYSTEM.md` +
`patches/scummvm/README.md` first. Conversation language: Portuguese.
Docs: English.

## Where we are

**Working app on Xbox.** Launcher (C#) → RetroArch UWP shell (XboxEmulationHub
fork, `AppListEntry="none"`) → bundled buildbot `scummvm_libretro.dll` core
(ScummVM 2026.3.1git). Clean quit to dashboard via 3 forced config keys. English
strings, orange `#CC6701` branding, 4s splash floor, `gui_scale=150` first run.
Game Options (Misc tab) crash — **fixed** (theme version skew, see
`DISCOVERIES.md` ## 15).

## Strategy (final)

**Route C refined: RetroArch UWP shell (compiled from source) + bundled
ScummVM libretro core.**

- RetroArch UWP = shell only (rendering, audio, input, pacing). **No custom
  RetroArch code.**
- Custom work lives **inside the ScummVM core** (from `extern/scummvm`) — but
  for now we ship the **buildbot binary**, not a locally built core.
- Filesystem: the core's Win32 calls are blocked in the UWP sandbox; the plan
  is the **FromApp API family** (`fileapifromapp.h`). See `docs/FILESYSTEM.md`.
  The shipped manifest keeps `MinVersion="10.0.15063.0"`; bump to 17763 when
  the FromApp patch lands.

## Submodule hygiene (IMPORTANT)

`extern/scummvm` and `extern/retroarch` stay **clean upstream checkouts** — no
fork, no local commits. All ScummVM deltas live in **`patches/scummvm/`**:

| Patch | Use | Status |
|---|---|---|
| `0001-gameoptions-misc-theme.patch` | Game Options Misc theme fix | **Applied inside the versioned zip**; reapply when regenerating themes |
| `0002-msvc-libretro-build.patch` | MSVC core build (optional) | Reconstructed 2026-08-08; verify before use |

The submodule HEAD is pinned at `e833307e` (clean).

**`system/scummvm.zip` is versioned** (~76 MB, patched themes + datafiles).
**`cores/scummvm_libretro.dll` stays gitignored** (~124 MB > GitHub 100 MB
limit; re-download from buildbot when needed).

**Future core update workflow** (also in `patches/scummvm/README.md`):
1. Update `extern/scummvm` submodule.
2. Apply `0001` patch (+ `0002` if rebuilding with MSVC).
3. Regenerate themes → rebuild `scummvm.zip` → **commit the new zip**.
4. Revert the submodule to a clean state.

## Dependencies (submodules)

| Path | Source | Status |
|---|---|---|
| `extern/scummvm` | ScummVM upstream master | KEPT — clean, patched via `patches/scummvm` |
| `extern/retroarch` | https://github.com/XboxEmulationHub/RetroArch.git | KEPT — clean UWP shell |
| `extern/spdlog` | https://github.com/gabime/spdlog (v1.17.0) | header-only logging (future core work) |
| `extern/uwp-xray-depot` | — | REMOVED |
| `extern/libretro-common` | — | REMOVED |

## Reference projects (in user's workspace, do NOT move)

- `C:\Users\marcelo\workspace\vs2022\dosbox-pure-unleashed-uwp` — working UWP
  libretro shell + core FS patches (FromApp). The proven pattern.
- `C:\Users\marcelo\workspace\x-files-uwp` — C# UWP file manager; FromApp
  P/Invoke reference.
- `C:\Users\marcelo\workspace\RetroArch` — local clone of the fork used for
  `extern/retroarch`.
- `C:\Users\marcelo\workspace\vs2022\scummvm-uwp` — OLD repo, archive only.

## Key files

- `patches/scummvm/0001-*.patch` / `0002-*.patch` / `README.md` — the theme fix
  + MSVC build patch + regen workflow.
- `launcher/ScummVMLauncher/MainPage.xaml.cs` — bootstrap, `SeedRetroArchConfig`,
  handoff (`scummvm-core:`), 4s splash floor, `gui_scale=150` first run.
- `launcher/ScummVMLauncher/system/scummvm.zip` — **versioned** datafiles +
  patched themes.
- `scripts/deploy-xbox.ps1` — WDP deploy (HTTPS :11443, CSRF dance, coexists
  with the user's real RetroArch install — never touches it).
- `scripts/build.ps1` / `package.ps1` / `clean.ps1` / `rebuild.ps1` — local
  MSBuild + sign (`certs/dosbox-uwp.pfx`, pw `dev`).

## Reference

- `PORT-PLAN.md` = source of truth for architecture decisions.
- `FILESYSTEM.md` = FromApp FS strategy + reference implementations.
- `DISCOVERIES.md` = full chronological log of findings/fixes (16 entries).
- `ARCHITECTURE.md` = current shipped architecture.
