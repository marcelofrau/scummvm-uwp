# Xbox tile-launch kill — "works under the VS debugger, closes from the tile"

Status: **RESOLVED — verified on the Xbox, launched from the tile** (2026-10-19).
Fix landed in `58a476e` (code) + `6de71e2` (this doc), merged to `main`.
Owner of this doc: whoever touches app startup, logging, threading or Mesa/GL.
Read this whole file before changing anything in `main.cpp`, `SdlMain.cpp`,
`LogHelper.h` or the boot sequence. Older history: `DISCOVERIES.md` §17
(first occurrence) and §18 (this regression).

---

## 1. TL;DR

- **Resolved (2026-10-19):** verified on the Xbox — launched **from the tile**,
  the app opens, the ScummVM GUI is usable, input and audio work. The fix is
  `58a476e` (code) + `6de71e2` (docs), merged to `main`.
- **Symptom (pre-fix):** the app (v2026.3.1.62, first GPU/Mesa release) started
  and closed right away when launched from the Xbox tile/dashboard. Launched
  with F5 from Visual Studio it ran fine. Same symptom as `DISCOVERIES.md` §17
  (August 2026, software-render frontend). Commit `226d29f` fixed it then.
- **Why the debugger matters:** a debugger switches off at least **three**
  separate mechanisms that can kill the process. Each one alone explains the
  symptom:
  1. **OutputDebugString on the core's libco stack.** Main suspect.
     Details in §3.1.
  2. **Late window activation.** The UI thread is blocked during boot and the
     window is only activated after the whole boot. See §3.2.
  3. **1 GB memory cap for "App"-type packages on Xbox.** The cap does not
     apply under the debugger. See §3.3.
- **What `226d29f` did:** it fixed #1 (a VEH that swallows ODS exceptions) and
  #2 (async boot) **together, in one commit**. Nobody knows which of the two
  mattered.
- **How the GPU rewrite undid it:** `3cfc2d9` moved the app onto
  `SDL_WinRTRunApp(sdl_main)`. `App.cpp`/`ScummVMMain.cpp`, which held both
  fixes, became dead code. Both protections were silently lost. That is the
  regression.
- **What the current change does:** it restores #1 and #2 in the SDL path and
  logs the memory budget, so #3 shows up in the log (§5).
- **If it still fails:** follow §7. Do not repeat what §8 lists.

---

## 2. Facts (verified)

| Fact | Where |
|---|---|
| Process entry: `main()` → `SDL_WinRTRunApp(sdl_main)`; `App`/`ScummVMMain` are never instantiated | `launcher/ScummVMLauncher/main.cpp` |
| SDL2 calls `sdl_main` **synchronously on the UI thread** from `IFrameworkView::Run` | SDL2 `src/core/winrt/SDL_winrtapp_direct3d.cpp`, `SDL_WinRTApp::Run()` |
| SDL2 calls `CoreWindow::Activate()` **only** in `OnAppActivated`. That runs only when events are pumped (`SDL_PumpEvents` → `Dispatcher->ProcessEvents`) | same file, `OnAppActivated`, `PumpEvents` |
| Before this fix, `sdl_main` did the whole boot before its first event pump: Bootstrap (zip), SDL_Init, Mesa GL context, XAudio2, `LoadLibrary` of the core (~125 MB), `retro_init`, `retro_load_game` | `SdlMain.cpp` (pre-fix, commit `d59f7c7`) |
| Bootstrap re-extracts `system/scummvm.zip` (~76 MB) **on every new app version** (flag `.scummvm-ready` holds the version) | `Bootstrap.cpp` `StageIfNeeded` |
| `scripts/build.ps1` **bumps the version on every build** unless `SCUMMVM_DONT_INCREMENT=1`, so every local build forces a re-extraction on next launch | `scripts/build.ps1`, `tools/version.ps1` |
| The buildbot Windows core is MinGW-built with `USE_LIBCO ?= 1`. MinGW + x86_64 selects libco `amd64.c` | ScummVM `backends/platform/libretro/Makefile.common`; libretro-common `libco/libco.c` |
| libco `amd64.c` on Windows: the stack is `malloc`'d (ScummVM: 1 MB, no guard page) and context switches **do not update the TEB/NT_TIB StackBase/StackLimit** | libretro-common `libco/amd64.c`; ScummVM `libretro-threads.cpp` (`EMU_THREAD_STACK_SIZE`) |
| ScummVM issues its GL calls from that emu cothread, so Mesa code runs on the libco stack | ScummVM `libretro-core.cpp` comments ("ScummVM issues its GL calls from the emulation thread") |
| The Mesa we ship (SternXD `mesa-uwp`, branch `uwp-26.2.0`) calls `OutputDebugStringA(message)` **unconditionally** for every log message on Windows/Xbox | `src/util/os_misc.c` `os_log_message` |
| Our own logger (`LogHelper.h` `spdlog::ods`) called `OutputDebugStringW` for **every** line **before** writing the file, including `[core]` lines from the core's log callback | `LogHelper.h` (pre-fix) |
| Vectored handlers run "after the debugger gets a first chance notification, but before the system begins unwinding the stack" | MS Learn, *Vectored Exception Handling* |
| Xbox UWP memory: **Apps 1 GB, Games 5 GB**; "When running your app or game from the Visual Studio debugger, these memory constraints do not apply." Xbox One Apps: only DX11 FL10.1 (no D3D12); Series X\|S Apps: DX12 FL11.0 | MS Learn, *System resources for UWP apps and games on Xbox* |
| "Visual Studio prevents Windows from suspending an app that is attached to the debugger." Launch work must finish "within a few seconds or the system may think your app is unresponsive and terminate it" | MS Learn, *UWP app lifecycle* |

---

## 3. The three debugger-dependent kill mechanisms

### 3.1 ODS → SEH dispatch on the libco stack (PRIMARY suspect)

How it happens, step by step:

1. `OutputDebugStringA/W` always calls `RaiseException(DBG_PRINTEXCEPTION_C
   0x40010006 / _WIDE_C 0x4001000A)` inside its own `__try/__except`.
2. **With a debugger:** the kernel sends the first-chance exception to the
   debugger, which prints the string and returns `DBG_CONTINUE`. Execution
   resumes. No user-mode SEH dispatch happens.
3. **Without a debugger:** user-mode dispatch runs. Vectored handlers run first,
   then the frame-based walk (`RtlDispatchException`). Each establisher frame
   is checked against `TEB->NtTib.StackLimit/StackBase`.
4. When the call happens on the libco `amd64.c` stack (heap memory, TIB not
   updated), the first frame is "out of bounds". The walk sets
   `EXCEPTION_STACK_INVALID` and stops (Wine reproduces this check in
   `call_seh_handlers`). The exception is never handled, and the process is
   terminated.
5. `SetUnhandledExceptionFilter`/`CrashFilter` **never runs**. That filter is
   called from the thread-start frame's handler, and the walk never reaches it.
   Result: **silent death, no `CRASH` line**. This is exactly the "process
   killed externally" pattern in §17.2.

Who raises ODS on that stack:
- **Mesa:** any warning or info it logs, via `os_log_message`.
- **Our logger:** the core's `retro_log` callback runs on the coroutine →
  `spdlog::info` → `OutputDebugStringW`.

Evidence from §17 that fits:
- §17.4: "dies between `spdlog::info` and `BootTrace`, a microsecond window".
  The logger did ODS *before* the file write. **The line whose ODS killed the
  process never reaches the log.**
- Bisect test 1 (VEH removed) still died. That fits too: at that time the VEH
  did not consume ODS. It started consuming ODS in `226d29f`, and that is when
  the app survived.
- §17.11 had already listed "H-Struct-B: OutputDebugString without debugger".
  The *conclusion* was right; the *explanation* ("unhandled first-chance") was
  not. ODS without a debugger is normally harmless. It is fatal here only
  because of the libco stack.

Why it is *the* prime suspect for the GPU release: GPU mode puts **Mesa**
(which always calls ODS) on the coroutine stack. The VEH that neutralised ODS
lived in `App.cpp`, which is now dead code.

How to confirm independently: on a Windows PC, install the appx and launch
from the Start menu (no debugger). A PC has neither the Xbox 1 GB cap nor the
same PLM timing. If it dies there too, without a `CRASH` line, mechanism 3.1 is
confirmed.

### 3.2 Window activated only after the whole boot (UI thread blocked)

- SDL2 only calls `CoreWindow::Activate()` once events are pumped. The old
  `sdl_main` did not pump until the core was fully loaded.
- On a fresh install or a new version, that window includes the 76 MB zip
  extraction (seconds), Mesa/D3D12 device creation, the 125 MB DLL load and
  core GL init.
- MS docs: launch must complete "within a few seconds" or the app may be
  terminated as unresponsive. Under the debugger, PLM/suspend handling is
  disabled.
- Caveat: §17's "~250 ms activation watchdog" is an **inference from log
  timestamps, not a documented value**. Do not build on that number. §17's very
  early deaths (~225 ms) actually fit 3.1 better than 3.2.

### 3.3 1 GB memory cap for "App"-type packages (not enforced under the debugger)

> **Status: unlikely for this user.** Their console is a Series X and the app is
> already set to **Game** (5 GB, full GPU, D3D12). Keep the `[mem]` logging to
> confirm. Do not spend time here unless the log shows `limit` ≈ 1024 MB or
> `usage` approaching `limit`.

- A sideloaded UWP defaults to type **App**: 1 GB, a shared share of the GPU,
  and on **Xbox One** only DX11 FL10.1, so **no D3D12 and no Mesa d3d12**.
- **Game** type: 5 GB, full GPU, D3D12.
- The cap is lifted under the VS debugger (MS docs, quoted in §2).
- On Xbox, GPU memory is unified with RAM. The GL path adds Mesa/D3D12 (~25 MB
  of DLLs plus heaps), a window-size FBO (4K RGBA8 ≈ 33 MB) and swap-chain
  buffers, on top of the 125 MB core and ScummVM.
- Exceeding the cap causes allocation failures. In Mesa or the core these may
  `abort()` → `__fastfail`, which **also bypasses `CrashFilter`**.
- The package identity was renamed `ScummVMFrontend` → `ScummVMLauncher`. Any
  per-app "Game" setting made for the old identity does **not** carry over.
- How to check: the new `[mem] … limit=NNNNMB` log line. ~1024 means App type;
  ~5120 means Game type.
- How to set it: in Dev Home, highlight the app → **View** button → *View
  details* / *Change view* → **App type: Game**. This is the
  community-documented path used by RetroArch and other emulators; the exact
  menu text varies by OS version. Verify on the console.

---

## 4. History (so you don't redo it)

| When | Commit | What |
|---|---|---|
| 2026-08-16 | `36fe1ad` | Own C++/CoreWindow frontend (software render, threaded `RetroCore`) |
| 2026-08-20 | — | §17: dies from the tile only. Bisected: VEH, IAT patch, ExtendedExecution, OnSuspending deferral — **none was the trigger** |
| 2026-08-20 | `226d29f` | "Xbox survival": async boot **+** VEH consuming ODS **+** DXGI mode guard + diagnostics, all in one commit. It worked. Which change mattered was never isolated |
| 2026-08-21/22 | `4643a75`…`46ef795`…`89b8093`… | GL attempts: Mesa WGL in App, EGL, SDL thread split. Deadlocks between the D3D11 swap chain and SDL/Mesa, and WrongThreadException |
| 2026-08-22 | `3cfc2d9` | "SDL_WinRTRunApp GL path — full lifecycle rewrite": everything in `sdl_main`, synchronous. **Both §17 protections lost here** |
| 2026-09-10 | `48d5379` | Release v2026.3.1.62 (broken from the tile) |
| 2026-10-19 | `58a476e` + `6de71e2` | This fix (§5) — **verified from the tile, merged to `main`** |

---

## 5. The fix (landed in `58a476e`, verified from the tile)

| File | Change | Covers |
|---|---|---|
| `main.cpp` | `ConsumeDebugPrintException` VEH, registered **first** (`AddVectoredExceptionHandler(1, …)`) in `main()` before `SDL_WinRTRunApp`. Returns `EXCEPTION_CONTINUE_EXECUTION` for `0x40010006/0x40010007/0x4001000A` (same codes as `226d29f`) | 3.1 |
| `LogHelper.h` | `spdlog::ods` calls `OutputDebugStringW` only if `IsDebuggerPresent()`. No ODS from our logger when launched from the tile | 3.1 |
| `main.cpp` | `WinRT_ActivateWindow()` bridge: `CoreWindow::GetForCurrentThread()->Activate()`. Called first thing in `sdl_main`. Deliberately **no** `ProcessEvents` there, so SDL handlers don't run before `SDL_Init` | 3.2 |
| `SdlMain.cpp` | `BootWorker()` on `std::async`: `Bootstrap_Run` + data paths + `LoadCoreDll` (no GL). The UI thread meanwhile does SDL/GL/FBO/XAudio2 init, then pumps events + `PresentBlank()` until the worker finishes. `retro_init`/`retro_load_game` stay on the UI thread (GL context is current there) | 3.2 |
| `main.cpp` + `SdlMain.cpp` | `WinRT_LogMemory(tag)` → `[mem] <tag> usage=…MB limit=…MB` after the GL context, after the core DLL, after `retro_load_game`, and every 300 frames | 3.3 (diagnosis) |
| `main.cpp` + `SdlMain.cpp` | `BootTrace` stage markers (see §6). `main()` also logs `debugger: PRESENT/absent` | diagnosis |

Expected boot trace from the tile (order may interleave between the UI and
worker threads):

```
main enter
VEH: ODS consumer installed
debugger: absent
SDL_WinRTRunApp start
sdl_main entered
CoreWindow activated            <- must be within a few ms of sdl_main entered
boot worker launched
worker: begin / bootstrap: begin / (extract start … extract ok on new version)
GL context created
[mem] after GL context usage=… limit=…
GL ready — waiting for boot worker
first frame presented
worker: bootstrap + paths done / worker: core DLL loaded
[sdl] boot worker done after N wait frames
[mem] after core DLL load …
retro_init done
retro_load_game done
[mem] after retro_load_game …
entering main loop
[sdl] retro_run #300 … / [mem] main loop … (every 300 frames)
```

---

## 6. Test procedure (exact)

The log is at `LocalState\scummvm-debug.log` of package `ScummVMLauncher_…`
(Device Portal → File explorer → LocalAppData → package → LocalState). The
last 5 sessions are kept: `.1` plain, `.2`–`.4` gzip.

1. Build **without bumping the version**: in PowerShell,
   `$env:SCUMMVM_DONT_INCREMENT=1; .\scripts\build.ps1 -Configuration Debug`
   (or build in VS). Fix any compile errors first. This code has not been
   compiled yet.
2. F5 on the Xbox. The GUI, input and audio must still work (regression check).
   The log must say `debugger: PRESENT`.
3. Stop debugging. Launch **from the tile**. Wait 60 s and use the GUI.
4. Pull the log. Check for `debugger: absent`, `CoreWindow activated`,
   `entering main loop`, growing `retro_run #…` counts, and the `[mem]` limit.
5. First-run path: uninstall **only** `ScummVMLauncher` in Device Portal (never
   the RetroArch package `1e4cf179-…`). Reinstall and launch from the tile. The
   zip extraction now happens with the window already active.
6. Release appx: `scripts/package.ps1`, install via Device Portal, launch from
   the tile.
7. Optional, strongly recommended: run steps 3–4 on a **Windows PC** too (Start
   menu launch, no debugger). It separates 3.1 from 3.3.

---

## 7. If it still dies — decision tree by the LAST line in the log

Remember: **the fatal line itself may be missing from the log.** Read the last
line as "died after this".

| Last line(s) | Meaning | Next action |
|---|---|---|
| No `main enter` at all (no new session) | Died before `main`. Most likely the loader could not resolve a static import (e.g. `SDL2.dll`) | List the appx contents: the root must contain `SDL2.dll`, `opengl32.dll`, `libgallium_wgl.dll`, `libglapi.dll`, `dxil.dll`, `spirv_to_dxil.dll`, `z-1.dll`, plus `cores\scummvm_libretro.dll`. Compare VS-deployed Debug with the CI Release appx |
| `VEH: ODS consumer FAILED` | VEH not installed | Fix that first; nothing else matters |
| No `CoreWindow activated` | Bridge threw or ran on the wrong thread | Check the log message variant; must run on the UI thread inside `sdl_main` |
| Ends inside `bootstrap: extract start` | Killed during extraction | Look at `[mem]`; check whether it really runs on the worker (`tid=` differs from `sdl_main entered`) |
| Ends after `GL context created` / before `first frame presented` | Mesa/D3D12 init or first swap | Check `[mem] limit` (App vs Game; Xbox One App = no D3D12). Set **App type = Game** and retest |
| Ends between `retro_init done` and `retro_load_game done` | Core GL init on the coroutine (shader compile through Mesa) | Most likely 3.1 (another exception type on the libco stack) or 3.3 (memory). Check `[mem]`; try Game type |
| Ends after `entering main loop`, `[mem] usage` rising toward `limit` | Memory cap (3.3) | Set Game type. Measure; reduce the FBO to core size instead of window size |
| `CRASH code=0x…` present | A real crash on a normal stack | `mod=` names the DLL. 0xC0000005 = AV, 0x80000003 = breakpoint (assert/DebugBreak), 0xC0000409 = fastfail/GS |
| Ends anywhere with no pattern, only from the tile, PC also dies | Another SEH exception (not ODS) on the libco stack | See "last resort" below |

Ordered follow-ups, if the decision tree does not settle it (console is a
Series X with Game type, so 3.3 is already deprioritised):
1. Confirm `[mem] limit` ≈ 5120 MB in the log (Game type really applied).
2. Run the Windows PC Start-menu test (§6 step 7) to isolate 3.1.
3. **Debug-from-tile vs Release-from-tile.** Deploy Debug with VS, stop, launch
   from the tile. Then do the same with the Release appx. If only Release
   fails, diff the packaging (appx contents) and the Release config
   (`WholeProgramOptimization`, CRT).
4. Attach without launching: VS → Debug → Other Debug Targets → *Debug
   Installed App Package* → pick the package → check *"Do not launch, but debug
   my code when it starts"*, then launch from the tile. Note that this attaches
   a debugger and therefore **masks 3.1 and 3.3**. Use it only to catch a
   real crash (`CRASH` case).
5. **Last resort for 3.1 (non-ODS exceptions on the libco stack):** this needs
   the core to run on fibers or real threads. The core is a buildbot binary
   (we never rebuild it; patch `0002` is obsolete). Options, all heavy:
   - ask upstream for a Windows build that uses `fiber.c` or threads;
   - build the core ourselves with `USE_LIBCO=0` (MinGW).
   Do **not** hack the TEB stack limits around `retro_run` (see §8).

---

## 8. Do NOT do these (and why)

- **Never** uninstall, update or touch the user's RetroArch (`1e4cf179-…`).
  Uninstall only `ScummVMLauncher` when you need a fresh install.
- **Do not remove the ODS VEH, the `IsDebuggerPresent` gate in `LogHelper.h`,
  the early `Activate()` or the boot worker** because "they look unrelated" or
  "it works without them under F5". F5 hides all three mechanisms. That is
  exactly how `3cfc2d9` lost the §17 fix.
- **Do not trust F5 as verification.** Every startup change must be verified
  by a launch **from the tile** (and the log must say `debugger: absent`).
- **Do not read "no `CRASH` line" as "killed externally by the OS".** Both
  SEH-on-libco-stack failures and `__fastfail`/`abort` bypass
  `SetUnhandledExceptionFilter`.
- **Do not log, allocate or take locks inside the VEH.** It runs on the core's
  1 MB guard-less libco stack, possibly under the loader lock or the heap lock.
- **Do not call `ProcessEvents` in `WinRT_ActivateWindow`** (before
  `SDL_Init`). SDL's WinRT handlers would run before SDL video exists.
- **Do not move the GL context or `retro_run` to another thread.** That was
  tried (`f9de77b`, `89b8093`, `7a5081d`, `46ef795`) and produced deadlocks and
  WrongThreadException. GL stays on the UI thread. Only non-GL work goes to the
  worker.
- **Do not revive `App.cpp`/`ScummVMMain.cpp`/`DeviceResources` alongside
  SDL.** The D3D11 swap chain on the same CoreWindow deadlocked with Mesa/SDL.
  If you clean up, *delete* the dead code. Do not wire it back in.
- **Do not re-run the §17 bisects as "fixes":** ExtendedExecution, IAT
  ExitProcess patching, OnSuspending deferral/Trim and DisplayRequest were all
  proven irrelevant.
- **Do not hack `TEB->NtTib.StackBase/StackLimit` around `retro_run`.** The
  core switches stacks internally. A wrong limit breaks stack probing
  (`__chkstk`) and guard-page growth on the real thread stack.
- **Do not run `scripts/build.ps1` without `SCUMMVM_DONT_INCREMENT=1` for test
  builds.** It bumps the version and dirties the manifest, `version.txt` and
  `build_counter.txt`. The new version also forces a 76 MB re-extraction on the
  next launch, which changes timing between test runs.
- **Do not build on §17's "~250 ms watchdog" number.** It is not documented
  anywhere. It was inferred from timestamps.
- **Do not rebuild the ScummVM core with MSVC** to "fix" this (patch `0002` is
  obsolete; the repo policy is the buildbot binary).

---

## 9. Reference

- Package: `ScummVMLauncher` (publisher `CN=Marcelo Frau`). LocalState is
  scoped to it.
- Device Portal: `https://<xbox-ip>:11443`, Basic auth, CSRF from
  `/api/os/info`. Install: `POST /api/app/packagemanager/package`. Packages:
  `GET /api/app/packagemanager/packages` → `InstalledPackages` (see
  `AGENTS.md`).
- Log helpers: `BootTrace` (QPC ms since the first call + `tid`, flush per
  line) and `spdlog::*` (shim, file append per line), both in `LogHelper.h`.
  `CrashFilter` is in `main.cpp`.
- Core: `cores/scummvm_libretro.dll` (buildbot, `display_version 2026.3.1git`,
  gitignored). Mesa/SDL DLLs: `launcher/ScummVMLauncher/mesa/` (SternXD
  `uwp-dep` / `mesa-uwp` `uwp-26.2.0`).

## 10. Open questions (answer them in this doc when known)

- Console model: **Xbox Series X** (user, 2026-10-19).
- App type for `ScummVMLauncher`: **Game** (user, 2026-10-19). Confirm with the
  `[mem] … limit=` line in the next log; it should be ~5120 MB. If it shows
  ~1024 MB, the setting did not apply to this package identity.
- Does the Release appx contain all 7 Mesa/SDL DLLs at the package root?
- Does a Windows PC Start-menu launch of the old v2026.3.1.62 die too? (That
  would be a clean confirmation of 3.1.) — **moot now that the tile works.**
- **Which mechanism was it?** With all three protections in place the app
  survives from the tile, but the fix landed all three at once (exactly the
  mistake `226d29f` made), so the culprit was never isolated. Optional bisect,
  *only if a regression reappears*: revert the VEH + the logger gate alone and
  test from the tile. Keep all protections in the shipped code regardless.

## 10b. Resolution (2026-10-19)

Verified on the Xbox, launched **from the tile** (no debugger):

- The app opens and stays up; the ScummVM GUI is usable; input and audio work.
- The fix is `58a476e` (code) + `6de71e2` (docs), fast-forward merged to `main`.

**What was done to fix it** (all three, in the SDL path — see §5 for the exact
edits):

1. **ODS VEH** (`main.cpp`): `AddVectoredExceptionHandler(1, …)` registered
   *first*, before `SDL_WinRTRunApp`, returning `EXCEPTION_CONTINUE_EXECUTION`
   for `0x40010006/0x40010007/0x4001000A`. Vectored handlers run *before* the
   frame-based SEH walk, so the ODS exception raised on the core's libco stack
   (outside the TEB limits) is consumed instead of terminating the process.
2. **Logger gate** (`LogHelper.h`): `spdlog::ods` only calls
   `OutputDebugStringW` when `IsDebuggerPresent()`. Removes our own ODS traffic
   on the tile (the core's `retro_log` callback runs on the coroutine).
3. **Early activation + async boot** (`main.cpp` + `SdlMain.cpp`):
   `WinRT_ActivateWindow()` activates the CoreWindow as the first thing in
   `sdl_main` (SDL would only do it on the first event pump, after the whole
   boot); the non-GL boot work (zip staging, data paths, 125 MB core DLL load)
   moved to `BootWorker()` on `std::async`, while the UI thread pumps events and
   `PresentBlank()`s black frames. GL stays on the UI thread.

**Lessons — do not repeat these:**

- The regression happened because `3cfc2d9` replaced the lifecycle without
  re-checking §17. Whenever the launch path changes, re-verify from the tile.
- Do not ship a fix that bundles several independent protections with no way to
  tell which one mattered (as `226d29f` did). Ideally each fix is bisectable.
- `scripts/build.ps1` bumps the app version on every build, which forces the
  76 MB zip re-extraction on the next launch and changes boot timing. Use
  `SCUMMVM_DONT_INCREMENT=1` for test builds.

## 11. Sources

- SDL2 WinRT app: <https://github.com/libsdl-org/SDL/blob/SDL2/src/core/winrt/SDL_winrtapp_direct3d.cpp>
- UWP app lifecycle (debugger prevents suspend; launch "within a few seconds"): <https://learn.microsoft.com/en-us/windows/uwp/launch-resume/app-lifecycle>
- Xbox UWP resources (App 1 GB / Game 5 GB, not enforced under the debugger, DX levels): <https://learn.microsoft.com/en-us/windows/uwp/xbox-apps/system-resource-allocation>
- Vectored exception handling order: <https://learn.microsoft.com/en-us/windows/win32/debug/vectored-exception-handling>
- OutputDebugString: <https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-outputdebugstringw>
- libco implementation selection: <https://github.com/libretro/libretro-common/blob/master/libco/libco.c>
- libco amd64 (no TIB update, malloc stack): <https://github.com/libretro/libretro-common/blob/master/libco/amd64.c>
- Boost.Context Win64 switch (saves/restores TIB StackBase/StackLimit — what a SEH-safe switch must do): <https://github.com/boostorg/context/blob/develop/src/asm/jump_x86_64_ms_pe_masm.asm>
- Wine x64 SEH dispatch frame-bounds check (`is_valid_frame` → `EXCEPTION_STACK_INVALID`): <https://github.com/wine-mirror/wine/blob/master/dlls/ntdll/signal_x86_64.c>
- ScummVM libretro threading (`USE_LIBCO`, 1 MB stack, GL on emu thread): <https://github.com/scummvm/scummvm/tree/master/backends/platform/libretro>
- Mesa `os_log_message` → unconditional `OutputDebugStringA`: <https://github.com/SternXD/mesa-uwp/blob/uwp-26.2.0/src/util/os_misc.c>
