# Versions

App version = `<ScummVM base>.<build counter>` — e.g. **2026.3.1.7**.
The base always comes from the **shipped** core binary, so a core upgrade is
always visible in the app version.

| Component | Version | Source |
|---|---|---|
| ScummVM core (libretro buildbot binary) | `2026.3.1git` | `cores/scummvm_libretro.info` → `display_version` |
| ScummVM base (normalized, `major.minor.patch`) | `2026.3.1` | derived — strip trailing `git` |
| Core binary build | nightly **2026-10-19**, 137.296.896 bytes | libretro buildbot, windows x86_64 |
| `extern/scummvm` submodule HEAD | `e833307e` (clean, no tag) | `git -C extern/scummvm rev-parse HEAD` |
| App (ScummVM UWP) | `2026.3.1.<N>` | `version.txt` / `Package.appxmanifest` (`build_counter.txt` = N) |
| App build atual | `2026.3.1.154` | último build testado (Release) |
| Release tag | `v2026.3.1.<N>` | git tag → triggers CI |
| `system/scummvm.zip` | sha256 `3816fd…943F` (~76 MB) | LFS-tracked — datafiles + patched themes |

> **Nota:** `extern/retroarch` e `extern/spdlog` são submodules **legados** ainda
> presentes em `.gitmodules` e no disco, mas referenciados por nenhum código ou
> script (o logging usa um shim próprio em `LogHelper.h`). Candidatos a remoção
> na limpeza da Fase 3.

### Why the build date never shows in the version

The libretro buildbot does **not** build from ScummVM master — it builds from the
**frozen 2026.3.x line**. Evidence: the 2026-10-19 nightly reports
`2026.3.1git`, while ScummVM master already reports `2026.4.1git` (bumped
2026-10-10). So `2026.3.1git` means "after release 2026.3.0, before 2026.4.0",
and a much newer binary keeps the same `display_version`. The build date is
recorded as a provenance comment at the top of `cores/scummvm_libretro.info`.

## How the version is computed

`tools/version.ps1` (wired into `scripts/build.ps1`, runs before every build):

1. Reads `display_version` from `cores/scummvm_libretro.info`.
2. Normalizes to `major.minor.patch` (strips `git`).
3. Increments `build_counter.txt`.
4. Rewrites `Package.appxmanifest` (`<Identity Version="…"/>`) and `version.txt`
   with the full `major.minor.patch.build`.

## When the core updates

1. Download the new `cores/scummvm_libretro.dll` from the libretro buildbot
   (`nightly/windows/x86_64/latest/scummvm_libretro.dll.zip`). Keep the previous
   binary as `cores-backup/scummvm_libretro.dll.bak-<date>` for rollback.
2. Update the provenance comment at the top of `cores/scummvm_libretro.info`
   (retrieval date, byte size, reported version).
3. The `display_version` in `.info` becomes the new app base automatically — but
   note it only changes when the upstream line changes (e.g. `2026.3.1git` →
   `2026.4.1git`), **not** per nightly. See "Why the build date never shows"
   above.
4. Only if the base version changed: rebuild `system/scummvm.zip` with the theme
   patch (see `patches/scummvm/README.md`) and commit it — datafiles/themes must
   be consistent with the core version.
5. Test from the **tile** before shipping — a core change can reintroduce the
   `XBOX-TILE-LAUNCH-KILL.md` failure (see `DISCOVERIES.md` §19).
6. Tag `v<new base>.<N>` → `.github/workflows/release.yml` builds, packages and
   publishes the GitHub Release.

Note: `extern/scummvm` is a **reference checkout only** — the shipped core is the
buildbot binary, never a local rebuild. Updating the submodule is optional and
does not change the packaged core.
