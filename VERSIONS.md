# Versions

App version = `<ScummVM base>.<build counter>` — e.g. **2026.3.1.7**.
The base always comes from the **shipped** core binary, so a core upgrade is
always visible in the app version.

| Component | Version | Source |
|---|---|---|
| ScummVM core (libretro buildbot binary) | `2026.3.1git` | `cores/scummvm_libretro.info` → `display_version` |
| ScummVM base (normalized, `major.minor.patch`) | `2026.3.1` | derived — strip trailing `git` |
| `extern/scummvm` submodule HEAD | `e833307e` (clean, no tag) | `git -C extern/scummvm rev-parse HEAD` |
| App (ScummVM UWP) | `2026.3.1.<N>` | `version.txt` / `Package.appxmanifest` (`build_counter.txt` = N) |
| Release tag | `v2026.3.1.<N>` | git tag → triggers CI |
| `system/scummvm.zip` | sha256 `3816fd…943F` (~76 MB) | LFS-tracked — datafiles + patched themes |

## How the version is computed

`tools/version.ps1` (wired as **PreBuildEvent** in `ScummVMLauncher.csproj`):

1. Reads `display_version` from `cores/scummvm_libretro.info`.
2. Normalizes to `major.minor.patch` (strips `git`).
3. Increments `build_counter.txt`.
4. Rewrites `Package.appxmanifest` (`<Identity Version="…"/>`) and `version.txt`
   with the full `major.minor.patch.build`.

## When the core updates

1. Update the `extern/scummvm` submodule and refresh
   `cores/scummvm_libretro.dll` + `.info` (buildbot).
2. The new `display_version` in `.info` becomes the new app base automatically.
3. Rebuild `system/scummvm.zip` with the theme patch (see
   `patches/scummvm/README.md`) and commit it.
4. Tag `v<new base>.<N>` → `.github/workflows/release.yml` builds, packages and
   publishes the GitHub Release.
