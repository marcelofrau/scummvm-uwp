# DISCOVERIES — what we found and what we did to get ScummVM running

Status: **active log**. Last updated: 2026-08-08.

This is a chronological log of the problems we hit while bringing ScummVM to
Xbox/UWP, the root causes we found in the RetroArch and ScummVM source, and the
fixes we shipped. Read `ARCHITECTURE.md` for how the current system fits
together.

## 1. The quit path was never clean

**Symptom.** Quitting the ScummVM launcher (or exiting a game) dumped the user
into the RetroArch RGUI menu instead of returning to the Xbox dashboard, and a
brief RetroArch "OSD toast" flashed on screen.

**Discovery (dummy core).** When a libretro core requests shutdown via
`RETRO_ENVIRONMENT_SHUTDOWN`, RetroArch sets
`RUNLOOP_FLAG_CORE_SHUTDOWN_INITIATED` (`runloop.c:1978`). The main loop then
checks `load_dummy_on_core_shutdown` (`runloop.c:6420`):

```c
if (settings->bools.load_dummy_on_core_shutdown)
{
   load_dummy_core    = true;
   runloop_st->flags &= ~RUNLOOP_FLAG_SHUTDOWN_INITIATED;  /* aborts shutdown */
}
```

Loading the dummy core **clears the shutdown flag** — so instead of exiting,
RetroArch stays alive and opens the menu. On desktop this is the default
(`retroarch.cfg` ships `load_dummy_on_core_shutdown = "true"`), and it's also
what RetroArch had persisted into the user's config.

**Discovery (OSD toast).** The queued "shutting down" message is only pulled
and rendered if fonts are enabled:

```c
/* gfx/video_driver.c:5211 */
else if (video_info.font_enable)
{
   msg = msg_queue_pull(&runloop_st->msg_queue);
   ...
}
```

**Fix (no RetroArch patch needed).** The launcher's `SeedRetroArchConfig()`
forces two keys on every launch:

- `load_dummy_on_core_shutdown = "false"` → shutdown completes, no RGUI menu.
- `video_font_enable = "false"` → OSD queue never drained, no toast.

Verified working on the Xbox by the user. **We deliberately did not patch
RetroArch source** — the config keys are the supported knobs, and the shell
stays clean upstream.

## 2. RetroArch persists `gl` as the video driver → menu crash risk

**Symptom.** After running a core that uses GL/ANGLE, RetroArch's save-on-
suspend can persist `video_driver = "gl"` into `retroarch.cfg`. With the core
unloaded, the menu then tries to render with a stale `gl` driver — a null-call
crash on Xbox.

**Discovery.** The Xbox shell code already anticipates this: in
`uwp_main.cpp` (`OnActivated`), on Xbox the driver is reset to D3D11 at boot
when no content is initialized:

```c
if (     strcmpi(currentdriver, "gl") == 0
      && !p_content->flags & CONTENT_ST_FLAG_IS_INITED)
   configuration_set_string(settings, settings->arrays.video_driver, "d3d11");
```

But the launcher handoff already passes a full command line, and this only
runs on cold boot. Belt-and-braces: `SeedRetroArchConfig()` also guarantees
`video_driver = "d3d11"` on every launch.

## 3. White flash between apps

**Symptom.** On launch there was a white frame between the launcher splash and
the ScummVM GUI.

**Discovery.** UWP shows a native splash screen for the *activated* app. The
launcher had a `uap:SplashScreen`, but the **RetroArch app did not** — so when
the launcher handed off via `scummvm-core:`, RetroArch's activation used the
default white splash.

**Fix.** Added `<uap:SplashScreen Image="Assets\SplashScreen.png"
BackgroundColor="#CC6701" />` to the RetroArch `Application` in
`Package.appxmanifest`, and set the launcher page background to the same
`#CC6701`. The whole boot sequence is now continuous orange → ScummVM.

**Gotcha found along the way.** The manifest XML is fussy: `uap:VisualElements`
was a self-closing tag before; once it has children it **must** be closed with
`</uap:VisualElements>` *before* `<Extensions>`, or `makeappx` fails with
`APPX1402` ("start tag on line N does not match the end tag"). The `Extensions`
(protocol registrations) are children of `Application`, not of
`VisualElements`.

## 4. Stale / duplicated assets polluted the appx

**Symptom.** The appx contained old, duplicated payload (files with " - Copy"
suffixes), inflating and confusing the package. Asset validation reported 133
entries with duplicates.

**Discovery.** Incremental builds were reusing stale `AppPackages/`, `bin/`,
`obj/` outputs; renamed/leftover asset copies were being picked up.

**Fix.** `scripts/clean.ps1` does `msbuild /t:Clean` **plus** deletes
`AppPackages`, `bin`, `obj`. A clean rebuild produced exactly 133 entries with
no duplicates (verified by listing the zip).

## 5. APPX1619 — asset sizes wrong for their roles

**Symptom.** Build/validation warnings about tile image dimensions
(APPX1619).

**Discovery.** UWP validates logo dimensions:

- `Wide310x150Logo` must be 310×150 → was 620×300 (must resize, not rely on
  scale variant).
- `StoreLogo.scale-200` must be 100×100 (200 scale of 50×50) → was 48×48.

**Fix.** Resized both to the required dimensions.

## 6. Native splash bitmaps are size-locked by scale

**Discovery.** The native `SplashScreen` bitmap is bound to its scale asset
(`SplashScreen.scale-200.png` = 1240×600 for a 1080p screen). You cannot point
a single `<uap:SplashScreen>` at an arbitrary 1920×1080 image.

**Fix.** Two-tier branding:
- Native splash keeps `SplashScreen.scale-200.png` (image untouched); only the
  `BackgroundColor` was changed to `#CC6701`.
- The launcher's *own* page uses a dedicated `Assets/splash-1920x1080.png`
  (renamed from the old `splash.png`) rendered with `Stretch="Uniform"`.

## 7. Strings were Portuguese; ship English

**Discovery.** All launcher strings/logs/manifest descriptions were PT-BR
(and the manifest descriptions even mixed PT text on top of a "ScummVM on
Xbox" claim).

**Fix.** Converted every user-visible string to English:
- `MainPage.xaml`: "Preparing ScummVM..."
- `MainPage.xaml.cs`: logs/status ("Bootstrap started", "Extracting",
  "Launching scummvm-core: protocol...", "Preparation failed", ...).
- `Package.appxmanifest`: descriptions "ScummVM on Xbox Series." (launcher)
  and "RetroArch frontend with ScummVM." (RetroArch app).

## 8. The `scummvm-core:` protocol / handoff contract

**Discovery.** RetroArch UWP (XboxEmulationHub fork) already supports a
frontend-friendly protocol (`uwp_main.cpp`, `ParseProtocolArgs`):

```
retroarch:?cmd=<CLI>&launchOnExit=<uri>&forceExit
```

- `cmd` → RetroArch argv. Tokenized with `std::quoted(s, '"', (char)0)` so
  quoted paths keep backslashes (`-L cores\scummvm_libretro.dll` works).
- `launchOnExit` → stored on the `App` singleton; fired from
  `App::Uninitialize()` (after `main_exit`) via `LaunchUriAsync`. The flag
  `m_launchOnExitShutdown` makes RetroArch exit cleanly in
  `OnEnteredBackground` instead of hanging.
- `forceExit` → immediate `CoreApplication::Exit()`.

**Our wiring.** The launcher registers `scummvm-launcher:`; RetroArch registers
`scummvm-core:` (hidden app, `AppListEntry="none"`). The launcher hands off
with `cmd=retroarch -v --log-file=… -L cores\scummvm_libretro.dll` and
`launchOnExit=scummvm-launcher:?cmd=exit`. When the user quits ScummVM, the
loop closes: RA exits → protocol-activates the launcher with `cmd=exit` →
launcher calls `Application.Current.Exit()` (no UI rendered) → dashboard.
Two apps, one package, zero shared code between them.

## 9. Bootstrap must be idempotent and fast

**Discovery.** Extracting `scummvm.zip` on every boot was wasteful and slow;
the native splash could not be stretched to hold the latency.

**Fix.** `Bootstrap()`:
- Extracts `system/scummvm.zip` → `LocalState\system` only when the
  `.scummvm-ready` flag is absent; writes the flag when done.
- Writes a minimal `scummvm.ini` (`gui_theme=scummremastered`).
- Runs on a background thread (`Task.Run`), and the launcher enforces a
  **4-second floor** on the branded splash so the heavy work (dezip + DLL
  load) happens behind a stable screen.

## 10. Build-system traps

- **`/t:Restore` before build.** Deleting `obj/` without re-restoring breaks
  the build with `WMC1006`. `build.ps1` always restores first.
- **MSBuild discovery.** Via `vswhere -latest -requires
  Microsoft.Component.MSBuild`, falling back to the Community install path.
- **Signing.** `package.ps1` auto-picks the newest Windows SDK `signtool`
  (`10.0.x.x\bin\x64\signtool.exe`), signs with `certs/dosbox-uwp.pfx`
  (password `dev`), copies to `dist\ScummVM.appx`.

## 11. Xbox Device Portal (WDP) deploy details

- Endpoint `https://<ip>:11443` (**not** 10343), `Authorization: Basic
  base64(user:pass)`.
- **CSRF:** first `GET /api/os/info` → `Set-Cookie: CSRF-Token=…`; every
  POST/DELETE needs `X-CSRF-Token: <token>` + the cookie jar.
- Package list field is **`InstalledPackages`**, not `Packages`.
- Install: `POST /api/app/packagemanager/package?package=<file>` with
  multipart `-F "package=@file"` → 202.
- Launch: `POST /api/taskmanager/app` with JSON
  `{ AppId: "App", PackageFamilyName: … }`.
- **Coexistence:** a real RetroArch install (`1e4cf179-f3c2-404f-b9f3-cb2070a5aad8`)
  lives on the console. `deploy-xbox.ps1` only reports it; it never
  uninstalls/upgrades it. ScummVM is a separate package (`148433a7-…`) with its
  own `LocalState` and its own protocols (`scummvm-core:`, never `retroarch:`).

## 12. Appx contents (current, verified)

133 entries, ~190 MB. Key paths:

```
ScummVMLauncher.exe          Application Id="App"        (visible tile)
RetroArch-msvcUWP.exe        Application Id="RetroArch"  (AppListEntry="none")
cores/scummvm_libretro.dll
cores/scummvm_libretro.info
system/scummvm.zip
Assets/…                    logos/tiles/splash
<RetroArch runtime DLLs>     Qt5*, SDL2, ANGLE, ffmpeg, etc.
```

## 13. RetroArch exit crash on PC (`0xC0000005` executing location `0x0`)

**Symptom.** Quitting the ScummVM launcher via its Quit menu on a Windows PC
produced a crash dialog in `RetroArch-msvcUWP.exe`: *"Access violation
executing location 0x0000000000000000"*. Seen once; not yet reproduced.

**Forensics (from `LocalState\retroarch.log`, PC run).** The log shows a clean
shutdown through the final playlist writes (`CMD_EVENT_HISTORY_DEINIT`,
`retroarch.c:4419-4451`) — core unloaded, options saved, then *silence*. The
null call is therefore in the tail of `main_exit` (`retroarch.c:6258`).

**What we ruled out (single `main_exit`).** The UWP build defines `HAVE_MAIN`
(`RetroArch-msvcUWP.vcxproj`), so `rarch_main` is init-only; the runloop runs
in `App::Run()` (`uwp_main.cpp:339`) and `main_exit(NULL)` runs **once** from
`App::Uninitialize()` (`uwp_main.cpp:376`). No double-free.

**What we ruled out (frontend wrappers).** `frontend_driver_deinit`, `_shutdown`,
`_exitspawn` all NULL-guard (`frontend/frontend_driver.c:360-382`); the UWP
frontend's null `deinit`/`shutdown`/`exitspawn` slots are simply skipped.

**Remaining suspects** (in `main_exit` tail, after the last log line):
`retroarch_ctl(RARCH_CTL_MAIN_DEINIT)` remainder, `driver_uninit(…, 0)`
(`retroarch.c:6308`), `retroarch_config_deinit`, `task_queue_deinit`,
`ui_companion_driver_deinit`. No WER report / Event Log entry was captured to
symbolicate the faulting offset.

**Resolution (debugger-only artifact).** Follow-up testing showed the AV only
ever fired while Visual Studio was attached to the `RetroArch-msvcUWP.exe`
process (native symbol resolution disabled → bare "executing location 0x0"
dialog). More than a dozen runs launched from the Start menu with VS fully
closed completed a clean quit every time — zero WER reports, zero dumps. The
`retroarch.log` tail is identical in both crash and clean runs (stops at the
final playlist writes; that is normal, not a crash marker), so the AV is a
latent exit-path race that the debugger's timing/heap perturbation exposes,
not a real-world bug. Packaged Xbox usage is unaffected. **No fix shipped —
none needed for production.**

## 14. Default GUI scale = 150% on first run

**Discovery.** The ScummVM launcher GUI scale is the integer percent config key
`gui_scale` (`gui/gui-manager.cpp:128`):

```c
if (ConfMan.hasKey("gui_scale"))
   _scaleFactor *= ConfMan.getInt("gui_scale") / 100.f;
```

`gui_scale=150` = 150% (default is 100%).

**Fix.** The first-run `scummvm.ini` seeded by the launcher's `Bootstrap()`
(`MainPage.xaml.cs`) now writes `gui_scale=150`. Only applies on first run
(no `.scummvm-ready` flag); an existing `scummvm.ini` is left untouched.

## 15. Game Options crash — version skew between core binary and bundled themes

**Symptom.** Opening a game's Game Options and clicking the **Misc** tab killed
the ScummVM launcher instantly. Early debugging misread it as heap corruption
(the crash dialog and the fact that it only appeared from inside the game
options dialog), which sent us chasing the emu-thread/audio paths for a while.

**Discovery (root cause).** Version skew. The core **binary** ships
ScummVM **2026.3.1git** (buildbot build) while the bundled **themes** were
generated from the `extern/scummvm` submodule source at an older HEAD. The
newer core registers a `GameOptions_Misc` dialog (hotspot-marker options);
the older theme sources never defined that layout. When the GUI opens the Misc
tab, the theme parser looks up the dialog by name and fails → ScummVM calls
`error()` (`gui/object.cpp:79`), which is **fatal** on this build.

**Fix.** Added the missing layout blocks (`GameOptions_Misc` +
`GameOptions_Misc_Container`: `EnableHotspots`, `HotspotMarkerPopup`,
`ShowHotspotText`) to the 4 theme files, regenerated the theme zips inside
`system/scummvm.zip`, redeployed. Works on Xbox.

**Submodule hygiene that came out of this.** The theme edit was only ever
meant as a local patch — but it lives inside the `extern/scummvm` submodule,
which we want to keep 100% upstream (no fork). The fix is now captured as
**`patches/scummvm/0001-gameoptions-misc-theme.patch`** and the submodule is
reverted to a clean `e833307e`. See ## 16.

## 16. Submodule hygiene + versioned `scummvm.zip`

We decided to keep `extern/scummvm` (and `extern/retroarch`) as **clean
upstream checkouts**. All deltas live as patches in `patches/scummvm/`
(see `patches/scummvm/README.md`):

| Patch | Applies to | Why |
|---|---|---|
| `0001-gameoptions-misc-theme.patch` | 4 theme `.stx` files | Game Options Misc crash (## 15) — **essential** |
| `0002-msvc-libretro-build.patch` | libretro Makefile/Makefile.common/libretro-fs.h | MSVC core build support — **optional** (we ship the buildbot binary). Reconstructed 2026-08-08 (original diff lost) — verify before use. |

**`scummvm.zip` is now versioned.** It was gitignored (with
`scummvm_libretro.dll`) as a "core build artifact". Reality: it is the
bundle of datafiles **plus the patched themes** (## 15) — the single artifact
that makes the appx actually work. It's 76 MB (< GitHub's 100 MB per-file
limit), so it's now committed. Each future theme/core-data update will add a
~76 MB blob — if it ever grows past 100 MB, switch to Git LFS.

**`scummvm_libretro.dll` stays gitignored** — it's ~124 MB, over the GitHub
limit, and it's a pure buildbot binary we can re-download.

**Why the DLL is not in git.** >100 MB → GitHub refuses. If we ever need it
reproducible, options are LFS or a release-asset download step in
`build.ps1`/`package.ps1`.

**Junk removed.** Stray `Assets\Assets\` (duplicate of the real `Assets\`),
empty leftover dirs under `system\` (AppxMetadata, Assets, cores, system),
and MSVC build droppings (`nul`, `config.h.engines`, `config.mk.engines`)
inside the submodule.

**Workflow for a future core update:**
1. Update `extern/scummvm` submodule to the new upstream HEAD.
2. `git -C extern/scummvm apply ../patches/scummvm/0001-*.patch` (+ 0002 if
   rebuilding the core with MSVC).
3. Regenerate themes → rebuild `scummvm.zip` → **commit the new zip**.
4. Revert the submodule (`git -C extern/scummvm checkout -- .`).
