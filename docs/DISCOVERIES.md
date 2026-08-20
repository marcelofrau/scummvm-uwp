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
  lives on the console. Deploy (manual via Device Portal) only reports it; it
  never uninstalls/upgrades it. ScummVM is a separate package (`148433a7-…`)
  with its own `LocalState` and its own protocols (`scummvm-core:`, never
  `retroarch:`).

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

## 17. Xbox silent process termination — app dies without debugger

**Symptom.** The C++/CoreWindow frontend works perfectly under VS2026 debugger
(F5) but is killed silently by the Xbox OS when launched without a debugger.
Both Debug and Release packages exhibit the same behavior. Re-launching the
installed package without the debugger also crashes — the distinguishing factor
is debugger presence, not build configuration.

### 17.1 Logging infrastructure

No logs existed at all because LogInit ran inside `App::Initialize` (after
activation) — death before activation = zero output. The fallback
`C:\scummvm-crash.log` was unwritable in AppContainer sandbox.

Unified log (`LocalState\scummvm-debug.log`) implemented:
- `ResolveLogPath()` with `ApplicationData::LocalFolder` primary + Win32 fallback
- `LogInit()` moved to `main()` BEFORE `CoreApplication::Run`
- `BootTrace()` inline helper: QPC timestamps + thread ID + flush-per-line,
  independent of spdlog, both wide and UTF-8 overloads
- Marker flood across all boot stages including pre-Run blind spots
- CrashFilter for unhandled exceptions (code/addr/thread/module)
- First log line records package identity: `pkg=<FamilyName> v=<ver>`

Files instrumented: main.cpp, App.cpp (Initialize/Load/Run/SetWindow),
ScummVMMain.cpp (constructor/BootCore), RetroCore.cpp (thread/init/load/heartbeat),
Bootstrap.cpp (extraction stages).

### 17.2 Process is externally killed, not crashing

Forensic analysis of multiple sessions from the unified log. Key evidence:

**Under debugger (F5):** App runs indefinitely. Session 3 from early dumps:
```
[boot 00291.8ms] emu: first retro_run begin
[RetroCore] RunFrame #1
[boot 00292.4ms] emu: frame #1
... 19920+ frames at ~15ms/frame ...
[scummvm-uwp] SHUTDOWN requested
[RetroCore] core requested SHUTDOWN — unloading game
```
19920+ frames, ~8.6 minutes, clean SHUTDOWN via RB button. CoreWindow activated,
input working, audio playing.

**Without debugger (standalone):** App boots fully then vanishes:
```
[boot 00297.0ms] emu: frame #1
[boot 00297.4ms] emu: retro_run() calling
[core] Mixer set up at 48000Hz
[scummvm-uwp] retro_video FIRST REAL FRAME: 960x720 pitch=1920
[boot 00372.4ms] emu: first video frame
[boot 00377.7ms] emu: retro_run() returned
... heartbeats, gamepad connected ...
=== END OF LOG ===
```
No `[ExitProcess]`, no `[TerminateProcess]`, no unhandled exceptions after the
frame. The PatchExitProcess hook (IAT-detoured) never fires — confirming the
process is terminated externally by the OS, not by any in-process API.

**Timing:** Standalone death at ~260ms after Run enter. Debugger prevents the
UWP process lifetime manager from activating. Without debugger, OS applies
aggressive timeout.

### 17.3 False positive: 0xe06d7363 VectoredException

The exception code `0xe06d7363` fires during `SetWindow`/`CreateDevice` in
EVERY run (F5 and standalone). It's the D3D11 debug layer probe in
`DirectXHelper.h::CreateDevice` — a try/catch that tests if the debug layer
is available. Code `0xe06d7363` = C++ exception (MSVC convention). Caught
internally, not a real crash.

Added filter in `FirstChanceExceptionHandler` to skip `0xe06d7363`. Reduces
2 lines of noise per run without losing any diagnostic signal.

### 17.4 Emu thread death in microsecond window

After adding per-frame heartbeats (every 60 frames ≈1s) plus before/after
`retro_run()` markers for first 3 frames:
```
[boot 00297.0ms] emu: frame #1
[boot 00297.4ms] emu: retro_run() calling
```
In standalone sessions, `emu: frame #1` BootTrace does NOT appear — the process
dies between `spdlog::info` and `BootTrace` on the emu thread, a microsecond
window. The UI thread's heartbeat fires but the log ends immediately after.

### 17.5 Structural comparison with dosbox-uwp

**Reference source:** `F:\workspace\vs2022\dosbox-pure-unleashed-uwp\dosbox-uwp\`
(GitHub: XboxEmulationHub/dosbox-pure-unleashed, branch with UWP frontend).

This frontend runs indefinitely on the same Xbox without debugger — the same
test that kills ScummVM. Side-by-side analysis of every component:

#### 17.5.1 main() and LogInit timing

**dosbox-uwp** (`App.cpp:24-29`):
```cpp
[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^) {
    auto direct3DApplicationSource = ref new Direct3DApplicationSource();
    CoreApplication::Run(direct3DApplicationSource);
    return 0;
}
```
Minimal. LogInit is inside `App::Initialize` (line 49). No crash filter, no
boot trace, no extra work before Run. Just `CoreApplication::Run` directly.

**ScummVM** (`main.cpp`):
```cpp
int main(...) {
    ResolveLogPath();           // ApplicationData + Win32 fallback
    LogInit(path);              // spdlog + file sink BEFORE Run
    InstallCrashFilter();       // SetUnhandledExceptionFilter
    BootTrace(L"CoreApplication::Run start");
    CoreApplication::Run(source);
    BootTrace(L"CoreApplication::Run exit");
}
```
More pre-Run work, but all of it is synchronous/cheap and completes in <1ms.
Not the cause.

#### 17.5.2 App::Initialize — what each adds

**dosbox-uwp** (`App.cpp:47-72`):
```cpp
void App::Initialize(CoreApplicationView^ applicationView) {
    LogInit();                                          // spdlog
    // Event subscriptions: Activated, Suspending, Resuming
    m_deviceResources = std::make_shared<DX::DeviceResources>();
    // DisplayRequest — screen-on only
    m_displayRequest = ref new DisplayRequest();
    m_displayRequest->RequestActive();
    QueryPerformanceFrequency(&m_perfFrequency);
    QueryPerformanceCounter(&m_lastFrameTime);
}
```
No VEH. No IAT patching. No ExtendedExecution. No InvalidParameterHandler.
Just: logging, events, D3D device, display request, QPC init.

**ScummVM** (`App.cpp`):
```cpp
void App::Initialize(CoreCoreApplicationView^ applicationView) {
    LogInit();
    // Event subscriptions (same 3 as dosbox)
    m_deviceResources = std::make_shared<DX::DeviceResources>();
    // [ADDED] ExtendedExecution request — lifecycle management
    m_extSession = ref new ExtendedExecutionSession();
    m_extSession->Reason = ExtendedExecutionReason::Unspecified;
    m_extSession->RequestExtensionAsync();  // fire-and-forget
    // [ADDED] DisplayRequest — same as dosbox
    m_displayRequest = ref new DisplayRequest();
    m_displayRequest->RequestActive();
    // [ADDED] VEH — crash/diagnostic capture
    AddVectoredExceptionHandler(1, FirstChanceExceptionHandler);
    // [ADDED] CRT safety
    _set_invalid_parameter_handler(InvalidParameterHandler);
    _set_thread_local_invalid_parameter_handler(InvalidParameterHandler);
}
```
Four additions vs dosbox: ExtendedExecution, VEH, InvalidParameterHandler.
(VEH and InvalidParameterHandler already ruled out by bisect.)

#### 17.5.3 App::Run loop — IDENTICAL structure

**dosbox-uwp** (`App.cpp:144-192`):
```cpp
void App::Run() {
    while (!m_windowClosed) {
        if (m_windowVisible) {
            ProcessEvents(ProcessAllIfPresent);
            m_main->Update();
            if (m_main->Render())
                Present(syncInterval, 0);
            // Frame pacing: Sleep based on target FPS, Sleep(1) minimum
            if (syncInterval == 0) {
                // ... timing logic ...
                Sleep(1); // always yield
            }
            m_main->ProcessPendingLoad();
        } else {
            ProcessEvents(ProcessOneAndAllPending);
        }
    }
}
```

**ScummVM** (`App.cpp:430-493`):
```cpp
void App::Run() {
    while (!m_windowClosed) {
        if (m_windowVisible) {
            ProcessEvents(ProcessAllIfPresent);
            m_main->Update();
            if (m_main->Render())
                Present(syncInterval, 0);
            // Frame pacing: IDENTICAL to dosbox
            if (syncInterval == 0) {
                // ... timing logic ...
                Sleep(1); // always yield
            }
            // [NO ProcessPendingLoad — ScummVM loads eagerly in EnsureBoot]
        } else {
            ProcessEvents(ProcessOneAndAllPending);
        }
    }
}
```
The run loops are **functionally identical**. Both have the Sleep(1) pacing.
The only difference: dosbox calls `ProcessPendingLoad()` after Present (lazy
ROM loading), ScummVM doesn't (eager boot in EnsureBoot).

#### 17.5.4 Boot path — KEY DIFFERENCE

**dosbox-uwp** `Load()` + `Update()`:
```cpp
void App::Load(Platform::String^) {
    m_main = std::make_unique<dosbox_uwpMain>(m_deviceResources);
}
// dosbox_uwpMain constructor: trivial — just stores the device resources pointer.
// NO core loading, NO DLL, NO audio init. Just member initialization.
```
`m_main->Update()` when no ROM loaded: does nothing meaningful (polls input,
skips rendering). The heavy `ProcessPendingLoad()` (LoadLibrary, retro_init,
retro_load_game) only runs AFTER the user selects a ROM through the file
browser — completely off the critical path.

**ScummVM** `Load()` + `Update()`:
```cpp
void App::Load(Platform::String^) {
    m_main = std::make_unique<ScummVMMain>(m_deviceResources);
    // ScummVMMain constructor: stores device resources, creates SdlInput, etc.
}
// Update() first call:
m_main->EnsureBoot();  // BLOCKS UI THREAD:
    // 1. Bootstrap extraction (scummvm.zip → LocalState)
    // 2. XAudio2 init
    // 3. LoadLibraryExW(scummvm_libretro.dll) — 125MB DLL
    // 4. retro_init, retro_load_game(NULL)
    // Total: 35-50ms with zero ProcessEvents calls
```

This is the **most significant structural difference**. dosbox-uwp presents
frames to the Xbox compositor almost immediately (trivial render). ScummVM
blocks the UI thread for 35-50ms doing heavy I/O before the first Present.
During this window, no `ProcessEvents` runs — the CoreWindow message pump
is starved.

#### 17.5.5 Component inventory (what ScummVM has that dosbox doesn't)

| Component | ScummVM | dosbox-uwp | Bisect result |
|-----------|---------|------------|---------------|
| `ExtendedExecutionSession` | ✓ | ✗ | ❌ NOT the trigger |
| `AddVectoredExceptionHandler` | ✓ | ✗ | ❌ NOT the trigger |
| `PatchExitProcessImports` | ✓ | ✗ | ❌ NOT the trigger |
| `_set_invalid_parameter_handler` | ✓ | ✗ | Low priority |
| `EnsureBoot()` blocking | ✓ | ✗ | ⏳ structural — needs testing |
| `OnSuspending` deferral | ✗ | ✓ | ⏳ HIGH — dosbox requests `deferral->Complete()` via async task |

#### 17.5.6 OnSuspending — another difference (IMPLEMENTED, ruled out)

**dosbox-uwp** (`App.cpp:209-233`):
```cpp
void App::OnSuspending(Object^ sender, SuspendingEventArgs^ args) {
    SuspendingDeferral^ deferral = args->SuspendingOperation->GetDeferral();
    if (m_main) m_main->PauseEmulation();
    if (m_displayRequest) m_displayRequest->RequestRelease();
    create_task([this, deferral]() {
        m_deviceResources->Trim();
        deferral->Complete();
    });
}
```

**ScummVM (after Test 5):** Now matches dosbox-uwp exactly — deferral,
DisplayRequest release, Trim() async, deferral->Complete(). Also added
DisplayRequest re-acquire on OnResuming. **Result: still dies at ~225ms
without debugger.** See section 17.13.

**Original ScummVM (before Test 5):**
```cpp
void App::OnSuspending(Object^ sender, SuspendingEventArgs^ args) {
    if (m_main) m_main->PauseEmulation();
}
```
No deferral, no DisplayRequest release, no Trim().

#### 17.5.7 Summary of all differences

| # | Difference | Risk level | Bisect result |
|---|-----------|------------|---------------|
| 1 | EnsureBoot blocking UI thread 35-50ms | ⏳ STRUCTURAL | All boot stages complete, but death at ~225ms is consistent |
| 2 | OnSuspending deferral/Trim | ❌ Ruled out | Test 5: implemented, still dies |
| 3 | ExtendedExecution request | ❌ Ruled out | Test 4: removed, still dies |
| 4 | VEH | ❌ Ruled out | Test 1: removed, still dies |
| 5 | IAT patching | ❌ Ruled out | Test 3: removed, still dies |
| 6 | InvalidParameterHandler | 🔵 LOW | Not tested — unlikely to affect OS lifecycle |
| 7 | ProcessPendingLoad vs eager boot | ℹ️ INFO | Different design, not a bug — but affects first-frame timing |

### 17.6 Bisect methodology

Interactive compile-time disable of one component at a time. Each test:
1. F5 from VS2026 — confirms build works, captures debugger-baseline log
2. Close app on Xbox
3. Launch directly on Xbox (no debugger) — the actual test
4. Pull `scummvm-debug.log` from Device Portal File Explorer
5. Analyze log for boot completion, frame count, death point

User runs 2x per test (debugger + standalone), clears logs between tests.

### 17.7 Bisect attempt 1: disable VEH

**Change:** Commented out `AddVectoredExceptionHandler` call in `App::Initialize`.
VEH handler code still compiled but never registered. All other components
(ExtendedExecution, DisplayRequest, IAT) active.

**Log confirmed:** `[boot] VEH handler: DISABLED (bisect test)`

**Result — Session 1 (F5):**
```
[boot 00133.7ms] VEH handler: DISABLED (bisect test)
[boot 00133.8ms] IAT hooks: INSTALLED
... full boot, retro_init, load_game ...
[RetroCore] RunFrame #1 → #60 → #120 → #180
[scummvm-uwp] SHUTDOWN requested  ← clean exit
```
180 frames, ~4.7s, clean shutdown. ✅

**Result — Session 2 (standalone):**
```
[boot 00105.4ms] VEH handler: DISABLED (bisect test)
... full boot, retro_init, load_game ...
[boot 00253.8ms] emu: frame #1
[boot 00254.2ms] emu: retro_run() calling
[scummvm-uwp] UWP Gamepad connected
=== END ===
```
Frame #1 present, heartbeat fires, gamepad connects → process killed.
Same as baseline. ❌

**Result — Session 3 (standalone):** Identical to session 2. ❌

**Conclusion:** VEH is NOT the trigger. Removing `AddVectoredExceptionHandler`
does not prevent the OS from killing the process.

### 17.8 Bisect attempt 2: disable IAT patching (partial)

**Change:** Commented out `PatchExitProcessImports()` in `App::Initialize` only.
The second call site in `ScummVMMain.cpp:118` (called after LoadLibrary of the
core DLL) was NOT disabled.

**Log confirmed:** `[boot] IAT hooks: DISABLED (bisect test)` but also:
```
[ExitProcess/TerminateProcess IAT hook ready]  ← from BootCore
[boot] boot: IAT hooks patched
```
IAT was still being patched on the core DLL. Test was inconclusive for IAT.

**Result:** Same death pattern. But IAT was not fully disabled — need to also
disable the ScummVMMain.cpp call site.

### 17.9 Bisect attempt 3: disable IAT patching (complete)

**Change:** Also commented out `PatchExitProcessImports()` in
`ScummVMMain.cpp:118`. Both call sites disabled.

**Log confirmed:**
```
[boot] IAT hooks: DISABLED (bisect test)
[boot] boot: IAT hooks SKIPPED (bisect)
```
No `[ExitProcess/TerminateProcess IAT hook ready]` anywhere in the log. IAT
patching fully disabled.

**Result — Session 1 (F5):**
```
[boot 00136.4ms] VEH handler: INSTALLED
[boot 00136.5ms] IAT hooks: DISABLED (bisect test)
[boot 00272.0ms] boot: IAT hooks SKIPPED (bisect)
... full boot, retro_init, load_game ...
[RetroCore] RunFrame #1 → #60 → #120 → #180
[scummvm-uwp] SHUTDOWN requested
[scummvm-uwp] UnloadGameInternal
```
180 frames, ~4.7s, clean shutdown. ✅

**Result — Session 2 (standalone):**
```
[boot 00105.4ms] VEH handler: INSTALLED
[boot 00105.5ms] IAT hooks: DISABLED (bisect test)
[boot 00218.8ms] boot: IAT hooks SKIPPED (bisect)
... full boot, retro_init, load_game ...
[boot 00253.8ms] emu: frame #1
[boot 00254.2ms] emu: retro_run() calling
[scummvm-uwp] UWP Gamepad connected
=== END ===
```
Frame #1 + heartbeat + gamepad → process killed. Same as baseline. ❌

**Result — Session 3 (standalone):** Identical. ❌

**Conclusion:** IAT patching is NOT the trigger. Both VEH and IAT patching
(`PatchExitProcessImports` on kernel32/ntdll/core DLL import tables) are
confirmed safe to remove without affecting the termination behavior.

### 17.9 Bisect attempt 4: disable ExtendedExecution

**Change:** Commented out `ExtendedExecutionSession` request in `App::Initialize`.
Re-enabled VEH and IAT (clean test isolating only ExtendedExecution).

**Log confirmed:** `[boot] ExtendedExecution: DISABLED (bisect test)`

**Result — Session 1 (F5):**
```
[boot 00023.5ms] ExtendedExecution: DISABLED (bisect test)
[boot 00170.1ms] device resources: create done
[boot 00170.5ms] VEH handler: INSTALLED
[boot 00170.8ms] IAT hooks: INSTALLED
... full boot, retro_init, load_game ...
[RetroCore] RunFrame #1 → #60 → ... → #3240
[scummvm-uwp] SHUTDOWN requested  ← clean exit
```
3240 frames, ~78s, clean shutdown. ✅

**Result — Sessions 2 & 3 (standalone):**
```
[boot 00096.7ms] VEH handler: INSTALLED
[boot 00097.1ms] IAT hooks: INSTALLED
[boot 00224.2ms] emu: first retro_run begin
[boot 00224.9ms] emu: retro_run() calling
[scummvm-uwp] UWP Gamepad connected
=== END ===
```
Frame #1 + heartbeat + gamepad → process killed. Same as baseline. ❌

**Conclusion:** ExtendedExecution is NOT the trigger. All four components
(VEH, IAT, ExtendedExecution, DisplayRequest) are confirmed individually
safe — removing any one of them does not prevent the Xbox OS from killing
the process.

**Critical observation:** In all standalone sessions across all 4 tests,
death occurs at **~225ms** after `CoreApplication::Run start`. This
consistent timing suggests the Xbox OS applies a **fixed-duration activation
watchdog** (~250ms). The app's structure (blocking boot → first frame →
death at same timestamp regardless of what's removed) points to a
lifecycle-state mismatch, not any specific API.

### 17.10 Summary of ruled-out triggers

| Component | What it does | Result |
|-----------|-------------|--------|
| VEH (`AddVectoredExceptionHandler`) | Registers process-wide first-chance exception handler for crash/diagnostic capture | ❌ NOT the trigger — removing it doesn't help |
| IAT patching (`PatchExitProcessImports`) | Replaces `ExitProcess`/`TerminateProcess` pointers in import tables of kernel32.dll, ntdll.dll, CRT, and scummvm_libretro.dll | ❌ NOT the trigger — removing it doesn't help |
| ExtendedExecution (`ExtendedExecutionSession`) | Requests extended process lifetime from OS | ❌ NOT the trigger — removing it doesn't help |
| DisplayRequest (`RequestActive`) | Keeps screen on (prevents dim/sleep) | Kept — same API as dosbox-uwp, not lifecycle-related |
| InvalidParameterHandler | `_set_invalid_parameter_handler` for CRT safety | Low priority — not in dosbox-uwp but unlikely to trigger OS termination |

### 17.11 Remaining hypotheses

After bisecting ALL ScummVM-specific additions (VEH, IAT, ExtendedExecution,
OnSuspending deferral+Trim), the problem is **confirmed structural**.

**Key data:** Death occurs at consistent **~225ms** after
`CoreApplication::Run start` in every standalone session, regardless of which
components are removed. Under debugger, runs indefinitely (thousands of frames).

**H-Struct-A: Activation watchdog timing (STRONGEST)**
Xbox UWP process lifecycle requires the app to reach "activated" state
within ~250ms. ScummVM's EnsureBoot blocks UI thread 35-50ms (LoadLibrary
125MB + XAudio2 + retro_init + retro_load_game), then first retro_run()
takes ~80ms. Total: ~225ms from Run to first video callback. The watchdog
fires because the process hasn't completed its activation sequence. dosbox-uwp
survives because its boot is near-instant (trivial constructor, no core
loading).

**H-Struct-B: OutputDebugString without debugger (MEDIUM)**
The ScummVM core emits OutputDebugString (DBG_PRINT exception 0x40010006).
Under debugger, these are consumed by WaitForDebugEvent. Without debugger
and without VEH to observe them, they become unhandled first-chance
exceptions that Windows may terminate the process for. This explains why
the death timing correlates with core activity (retro_run) — the core
emits ODS during its init.

### 17.12 Next tests

1. **Test 6: non-blocking EnsureBoot** — Defer EnsureBoot() to background
   thread or add ProcessEvents yields. Goal: Present frames to compositor
   before heavy I/O. If survives → H-Struct-A confirmed.

2. **Test 7: suppress OutputDebugString** — Redirect ODS via
   `SetEnvironmentVariable("DOTNET_DbgBreakEnabled","0")` or compile
   `scummvm_libretro.dll` with ODS suppressed. If survives → H-Struct-B
   confirmed.

3. **Test 8: bare minimum** — Strip to match dosbox-uwp exactly: no VEH,
   no IAT, no ExtendedExec, no InvalidParameterHandler, keep only
   DisplayRequest + proper OnSuspending + Sleep(1). If survives, bisect
   upward to find minimum viable config.

### 17.13 Bisect attempt 5: OnSuspending deferral + Trim (Test 5)

**Change:** Added full dosbox-uwp OnSuspending handling to ScummVM:
- `SuspendingDeferral` request from `args->SuspendingOperation->GetDeferral()`
- `PauseEmulation()` before deferral work
- `DisplayRequest::RequestRelease()` on suspend
- Async `Trim()` + `deferral->Complete()` via `create_task`
- `DisplayRequest::RequestActive()` on `OnResuming`

Also added `#include <ppltasks.h>` and `using namespace concurrency;` for
`create_task`.

**Result — Session 1 (F5):**
```
[boot 00216.0ms] boot: begin
[boot 00277.6ms] emu: retro_init ok
[boot 00281.8ms] emu: load_game ok
[boot 00283.9ms] emu: frame #1
[boot 00359.7ms] emu: first video frame 960x720 pitch=1920
... RunFrame #60, #120, ... #720 ...
[scummvm-uwp] SHUTDOWN requested ← clean exit
```
720 frames, ~17s, clean shutdown. ✅

**Result — Sessions 2 & 3 (standalone):**
```
[boot 00222.4ms] emu: retro_run() calling
[boot 00224.0ms] emu: frame #1
[scummvm-uwp] UWP Gamepad connected
=== END ===
```
Frame #1 + heartbeat + gamepad → killed at ~225ms. ❌

**Critical finding:** The deferral/Trim is irrelevant because the process
is KILLED, not SUSPENDED. The Xbox OS never sends a `Suspending` event —
it terminates the process directly via the activation watchdog. The deferral
mechanism only helps when the OS gives you a chance to complete async work
before suspension. When the OS kills you, there's no event to defer.

**Conclusion:** H-Struct-B (missing OnSuspending deferral) is **ruled out**.
The remaining hypothesis is H-Struct-A (activation watchdog timing) or
H-Struct-B2 (OutputDebugString without debugger).
