# ScummVM UWP — Architecture

Status: **implementado e funcionando no Xbox**. Última atualização: 2026-10-19.

Esta documenta a arquitetura **single-app**: um único appx, um único processo,
um frontend libretro próprio (sem RetroArch) que carrega o core oficial
`scummvm_libretro.dll` direto. Substitui a arquitetura "launcher C# + RetroArch
shell (2 apps)".

---

## 1. Visão geral

Um único executável `ScummVMLauncher.exe` (CoreWindow, C++/WinRT, sem XAML)
que **é** um frontend libretro: renderiza, toca áudio, lê o gamepad e fornece
filesystem ao core. O core `scummvm_libretro.dll` (buildbot oficial, inalterado)
é carregado via `LoadLibrary` no processo e roda o GUI do ScummVM direto na
tela. Sem protocolos, sem segundo app, sem cadeia de handoff.

O lifecycle é hospedado por `SDL_WinRTRunApp(sdl_main)` (`main.cpp` +
`SdlMain.cpp`); o caminho C++/CX `App.cpp`/`ScummVMMain.cpp` continua compilado
mas é bypassado.

```
┌──────────────────────────────────────────────────────────┐
│ ScummVMLauncher.exe (1 App, package 148433a7...)          │
│                                                          │
│  SDL_WinRTRunApp → sdl_main (UI thread)                  │
│  ├── Mesa WGL context + swapchain SDL (D3D12)            │
│  ├── RenderPresentLoop   blit FBO → tela, ~8ms cadence   │
│  ├── BootWorker (async)  bootstrap + LoadLibrary          │
│  ├── XAudio2Output        áudio (WASAPI/XAudio2)         │
│  ├── SdlInput             gamepad Xbox → RetroPad        │
│  └── VFS (libretro-common vfs_implementation_uwp.cpp)    │
│                          │                                │
│  CoreThreadMain (worker): HGLRC próprio (wglShareLists)   │
│  ┌───────────────────────┴──────────────────────────┐    │
│  │ cores/scummvm_libretro.dll (LoadLibrary)         │    │
│  │  → retro_init / retro_load_game(NULL) / retro_run│    │
│  │  → GUI do ScummVM renderizado via callbacks      │    │
│  └──────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────┘
```

### Componentes

| Componente | Origem | Papel |
|---|---|---|
| `ScummVMLauncher.exe` | port de `vs2022\dosbox-pure-unleashed-uwp\dosbox-uwp` | Frontend libretro completo |
| `cores/scummvm_libretro.dll` | libretro buildbot nightly (`2026.3.1git`, 2026-10-19) | Core ScummVM (buildbot, **não recompilado**) |
| `system/scummvm.zip` | gerado + versionado | Datafiles + temas (patch `0001`) |
| `mesa/*.dll` | Mesa D3D12 WGL (`opengl32`/`libgallium_wgl`/…) | Backend GL |
| `retro_vfs_interface` | `libretro-common/vfs/vfs_implementation_uwp.cpp` (FromApp) | FS do core no sandbox |

O RetroArch real do usuário (`1e4cf179-…`) continua intocado; nosso package
não registra `retroarch:` nem `scummvm-core:`.

---

## 2. Contrato do core ScummVM (o que o frontend precisa prover)

O core ScummVM (`backends/platform/libretro`) é um core libretro padrão com
algumas exigências específicas:

| Requisito | Como o frontend atende |
|---|---|
| `retro_set_environment` — `RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME` | retornar `true`; core roda sem conteúdo e abre o próprio GUI |
| `retro_load_game(NULL)` | boot direto no launcher do ScummVM |
| `RETRO_ENVIRONMENT_GET_VFS_INTERFACE` | fornecer `retro_vfs_interface` v3 com implementação FromApp |
| `GET_SYSTEM_DIRECTORY` | `E:\scummvm\system` (ou `LocalState\system` fallback) |
| `GET_SAVE_DIRECTORY` | `LocalState` (saves do ScummVM) |
| `GET_LIBRETRO_PATH` | caminho absoluto do `cores\scummvm_libretro.dll` |
| `GET_LOG_INTERFACE` | ponte `retro_log_printf` → log do app |
| `GET_LANGUAGE` | `ENGLISH` |
| `GET_PLAYLIST_DIRECTORY` | `LocalState` (playlists opcionais) |
| `RETRO_ENVIRONMENT_SET_PIXEL_FORMAT` | aceitar **RGB565** (modo software) ou **XRGB8888** (modo GL) |
| `RETRO_ENVIRONMENT_SET_HW_RENDER` | aceitar **quando** `scummvm_video_hw_acceleration=enabled` — frontend fornece GL context via Mesa WGL (ver seção 10). É o caminho default. |
| `GET_MIDI_INTERFACE` | retornar `false` (o core usa fallback próprio) |
| `GET_INPUT_BITMASKS` | retornar `false` — preserva o caminho de polling que o D-pad precisa |
| `SET_CORE_OPTIONS_V2*` / `SET_VARIABLES` / `GET_VARIABLE` / `GET_VARIABLE_UPDATE` | armazenar os defaults **declarados pelo core** e respondê-los; opções novas são atendidas automaticamente |

### Vídeo

- Modo GL (default): o core usa `SET_HW_RENDER`; o frontend fornece contexto Mesa
  WGL e um FBO compartilhado entre core thread e render thread. O core renderiza
  direto na GPU.
- Modo software (fallback): o core pede `RGB565`; `retro_video_refresh_t` recebe
  `pitch = width * 2`. O frontend converte 565→BGRA8888 (CPU; resoluções
  320×200–640×400 são triviais) e faz upload pro
  bitmap D2D (`DXGI_FORMAT_B8G8R8A8_UNORM`).

### Áudio

- Core entrega via `retro_audio_sample_batch_t` (estéreo 16-bit). Sample rate
  default do core (44.1k/48k — conferir na Fase 1) define o formato do
  `XAudio2Output`.

### Input

- `retro_input_state_t` + `retro_input_poll_t`. `SdlInput` mapeia gamepad Xbox →
  RetroPad → o core traduz pra ScummVM (ver `CONTROLS.md`). Teclado/mouse USB
  funcionam (eventos CoreWindow).

### Filesystem

- O core usa `filestream_open`/`retro_dirent`/`retro_stat` (libretro-common) —
  que, quando o frontend entrega `GET_VFS_INTERFACE`, passam pela VFS do
  frontend. A VFS UWP usa a família **FromApp**
  (`CreateFile2FromAppW`, `FindFirstFileExFromAppW`, …) — acessa E: respeitando
  `broadFileSystemAccess`.
- **Importante**: o processo é `runFullTrust` (manifest), então o `fopen`/`fread`
  do `StdioStream` do ScummVM também funciona em processo — a VFS FromApp é a
  camada preferencial e a rede de segurança em um só lugar. Sem patch no core.

---

## 3. Startup flow

```
main()  [main.cpp]
  ├─ LogInit() → LocalState\scummvm-debug.log
  ├─ AddVectoredExceptionHandler(1, ConsumeDebugPrintException)   ← FIRST (ODS safety)
  └─ SDL_WinRTRunApp(sdl_main, nullptr)   ← SDL owns CoreWindow + event loop
       │
       └─ sdl_main()  [SdlMain.cpp, UI thread]
          ├─ WinRT_ActivateWindow()   ← activate BEFORE the long boot (PLM watchdog)
          ├─ std::async(BootWorker):
          │    Bootstrap (extrair system/scummvm.zip → LocalState\system,
          │    flag de versão; scummvm.ini se ausente)
          │    → DataPaths → LoadLibrary(cores\scummvm_libretro.dll)
          ├─ UI thread pumps events + PresentBlank() while boot runs
          └─ Mesa GL context (SDL swapchain) → core thread
               CoreThreadMain (worker): HGLRC próprio, wglShareLists, context_reset,
                 retro_init → retro_load_game(NULL) → bounded retro_run
               RenderPresentLoop (UI): blit FBO compartilhado → tela, ~8ms cadence
```

Detalhes de sobrevivência ao tile (VEH, activate antecipado, boot async):
`XBOX-TILE-LAUNCH-KILL.md`. Modelo de threads GL: `ARCHITECTURE.md` §10.

---

---

## 4. Quit flow

1. Usuário sai do GUI do ScummVM → core chama `RETRO_ENVIRONMENT_SHUTDOWN`.
2. Frontend encerra a emu thread, destroi o core (`retro_unload_game` +
   `retro_deinit` + `FreeLibrary`) e chama `CoreApplication::Exit()`.
3. Dashboard. **Sem** RA, **sem** protocolo, **sem** `launchOnExit`.

---

## 5. Filesystem / layout

| Local | Conteúdo |
|---|---|
| Package install dir | `ScummVMLauncher.exe`, `cores\scummvm_libretro.dll`, `system\scummvm.zip`, `Assets\` |
| `LocalState\system\` | `scummvm.zip` extraído (~89 MB) + `scummvm.ini` + `.scummvm-ready` |
| `LocalState\saves\` | save_directory do core |
| `LocalState\` | logs (crash.log, scummvm-frontend.log) |

Staging idempotente e gated por versão: flag `.scummvm-ready` == versão do app →
skip; versão diferente → delete-first + re-extração (preserva `scummvm.ini`).
Detalhe em `STAGING.md`.

---

## 6. Manifest / capabilities

- **1** Application entry (`Id=App`, AppListEntry normal). Entry RetroArch e
  protocolos removidos.
- **Manter** (essencial):
  - `<rescap:Capability Name="runFullTrust" />` — é o que permite o fopen do
    core e o acesso direto ao FS em processo.
  - `<rescap:Capability Name="broadFileSystemAccess" />` — acesso a E: via
    FromApp (browse de jogos no GUI do ScummVM).
  - `<rescap:Capability Name="expandedResources" />` (inalterado).
- `MinVersion` mantido >= 10.0.17763.0 (mínimo p/ `broadFileSystemAccess`).

---

## 7. Logging

| Caminho | Conteúdo |
|---|---|
| `LocalState\crash.log` | marcadores diretos de boot (ground truth) |
| `LocalState\scummvm-frontend.log` | log do app (`OutputDebugStringA` + arquivo, rotação) |

---

## 8. Build / deploy

- MSBuild VS2022 (`scripts/build.ps1`, roda `/t:Restore` primeiro — obj/ sem
  restore quebra WMC1006).
- Appx em `launcher\ScummVMLauncher\AppPackages\ScummVMLauncher_<ver>_x64_Test\`.
- Assinar: signtool + `certs\dosbox-uwp.pfx` (`dev`).
- Deploy Xbox: manual via Device Portal (`https://<ip>:11443`, porta 11443;
  CSRF: GET `/api/os/info` → cookie + `X-CSRF-Token`; campo
  `InstalledPackages`; install `POST /api/app/packagemanager/package` multipart;
  launch `POST /api/taskmanager/app`). **Nunca** desinstalar RA real.
- `scripts/fetch-payload.ps1`: baixa só `scummvm_libretro.dll` + `scummvm.zip`
  (sem payload RetroArch).

---

## 9. Diferenças vs arquitetura antiga (2 apps)

| Aspecto | Antiga (RA shell) | Nova (frontend próprio) |
|---|---|---|
| Apps no package | 2 (Launcher + RetroArch hidden) | 1 |
| Processos | launcher C# → RA (protocolo) | 1 processo |
| Render/áudio/input | RetroArch | nosso (D2D/XAudio2/SdlInput) |
| Core | `-L cores\scummvm_libretro.dll` via RA | `LoadLibrary` direto |
| Quit | `launchOnExit` → launcher → Exit | `SHUTDOWN` → `CoreApplication::Exit` |
| retroarch.cfg | seed/rewrite/system_directory | inexistente |
| Core-missing detection | log marker | inexistente (core é nosso) |
| E: staging | system + cfg RA real | só dados do ScummVM |

---

## 10. OpenGL mode (Mesa WGL)

O frontend suporta dois modos de rendering, determinados pelo core option
`scummvm_video_hw_acceleration` lido durante `retro_init()`. **GL é o caminho
embarcado/default** (o core declara `enabled` com `USE_OPENGL`); SW é o fallback.

| Modo | Rendering | Apresentação | Swap chain |
|------|-----------|-------------|------------|
| GL (default) | Core: OpenGL FBO na GPU. Render thread faz blit FBO → tela | Mesa: swap no render thread | D3D12 (Mesa) |
| Software (fallback) | Core: RGB565 em RAM. Frontend: conversão CPU → BGRA8888 | D2D bitmap → D3D11 `Present()` | D3D11 |

**Restrição:** D3D11 e D3D12 não compartilham swap chain no mesmo
CoreWindow. O modo é decidido após `retro_init()` quando o core chama
`SET_HW_RENDER`.

**Modelo de threads (RetroArch-style):** o render thread roda no thread
principal/UI (dono do swapchain SDL/Mesa) e apresenta a ~8ms de cadence; o
**core thread** cria seu próprio HGLRC, compartilha com o do SDL via
`wglShareLists` (deixa o FBO do core + texturas visíveis pro render thread), e
roda `context_reset`/`retro_init`/`retro_load_game`/`retro_run`. `retro_video_cb`
(nosso core thread) só marca `g_frameReady`; quem faz o blit e o swap é o render
thread. Motivo: o primeiro frame do Mesa D3D12 compila GLSL→DXIL por ~2.4s sob
lock de processo — o render thread não pode ficar preso nisso (`DISCOVERIES.md`
§19).

**DLLs necessárias** (no package, na raiz):
`opengl32.dll` + `libgallium_wgl.dll` + `libglapi.dll` + `spirv_to_dxil.dll` +
`dxil.dll` + `z-1.dll` + `SDL2.dll`.

**Contexto GL:** WGL via Mesa (`wglCreateContext`/`wglMakeCurrent`).
Sem SetPixelFormat manual, sem D3D device manual — Mesa cria tudo
internamente via D3D12.

**Detalhes completos:** `docs/opengl-plan/`
