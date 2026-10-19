# ScummVM UWP — Plano de Implementação (single-app, frontend libretro próprio)

Status: **em execução**. Author: Marcelo Frau. Última atualização: 2026-10-19.

Checklist mestre da migração: de "launcher C# + RetroArch shell (2 apps)" para
**um único appx com frontend libretro próprio** que carrega `scummvm_libretro.dll`
direto. Marca cada item com `[x]` conforme for concluído. Design e justificativas
em `PORT-PLAN.md`; arquitetura-alvo em `ARCHITECTURE.md`.

> **Estado (2026-10-19):** o app está **funcionando no Xbox** — GUI do ScummVM na
> tela, GL (Mesa WGL) renderizando, input e áudio OK, boot pelo tile OK. O
> caminho de lifecycle mudou de `App.cpp`/`ScummVMMain.cpp` para
> `SDL_WinRTRunApp`/`SdlMain.cpp` (ver `ARCHITECTURE.md` §10 e
> `XBOX-TILE-LAUNCH-KILL.md`). Itens de Fase 1/2 abaixo foram implementados no
> novo caminho; alguns nomes de arquivo do plano estão desatualizados.
> Pendências reais: limpeza do workspace (payload RetroArch morto ainda no disco,
> ver Fase 3) e verificação sistemática por jogo (Fase 4).

> Regra de ouro (não muda): RetroArch REAL do usuário nunca é tocado. O
> protocolo `retroarch:` nem é registrado por nós. Coexistência total.

---

## Fase 0 — Documentação (base antes de mexer em código)

- [x] Criar este `IMPLEMENTATION-PLAN.md` (checklist mestre)
- [x] Reescrever `docs/ARCHITECTURE.md` — arquitetura single-app
- [x] Reescrever `docs/PORT-PLAN.md` — Route D (frontend custom), histórico
- [x] `docs/PLAN-E-STAGING.md` → `docs/STAGING.md` (só staging de dados, sem RA)
- [x] Atualizar `docs/FILESYSTEM.md` — FS no frontend (não mais no core)
- [x] Reescrever `docs/HANDOFF.md` — estado novo + referências
- [x] Atualizar `AGENTS.md` — remover protocolo `scummvm-core:`, novo layout
- [x] Atualizar `README.md` — narrativa single-app, features, estrutura
- [x] Update `patches/scummvm/README.md` — nota: 0002 (build MSVC) sem uso

## Fase 1 — Esqueleto + frontend portado (meta: GUI do ScummVM na tela)

> Implementado, mas o host do lifecycle é `SdlMain.cpp` (SDL_WinRTRunApp), não
> `App.cpp`/`ScummVMMain.cpp` — ver `ARCHITECTURE.md` §10.

### 1.1 Vendor de dependências
- [x] Vendor `libretro.h` (`launcher/ScummVMLauncher/libretro.h`)
- [x] Vendor `libretro-common` mínimo (VFS) em `launcher/ScummVMLauncher/extern/libretro-common`
- [x] Include paths no projeto apontando pros vendored dirs

### 1.2 Port do frontend (fonte: `vs2022\dosbox-pure-unleashed-uwp\dosbox-uwp`)
- [x] `Common/` — `DeviceResources`, `DirectXHelper`, `StepTimer`
- [x] `Content/RetroCore.cpp/.h` — `LoadLibrary("cores\\scummvm_libretro.dll")` + `GetProcAddress`
- [x] Renderizador — GL (Mesa WGL, FBO compartilhado) no caminho atual; SW RGB565 → BGRA8888 mantido
- [x] `Content/XAudio2Output` — sample rate do core (48k)
- [x] `Content/SdlInput` — gamepad → RetroPad (mapping do `CONTROLS.md`)
- [x] Host do lifecycle — `main.cpp` + `SdlMain.cpp` (SDL_WinRTRunApp); `App.cpp`/`ScummVMMain.cpp` compilados mas bypassados
- [x] **Descartar** do dosbox-uwp: `FileBrowser`, `FrontendMenu`, etc. (ScummVM tem GUI própria)

### 1.3 Env handler do core ScummVM (mais commands que dosbox)
- [x] `GET_SYSTEM_DIRECTORY` → `LocalState\system`
- [x] `GET_SAVE_DIRECTORY` → `LocalState\saves`
- [x] `GET_LIBRETRO_PATH` → caminho do DLL
- [x] `GET_LOG_INTERFACE`, `GET_LANGUAGE`, `GET_PLAYLIST_DIRECTORY`
- [x] `GET_VFS_INTERFACE` → VFS FromApp (`vfs_implementation_uwp`)
- [x] `SET_SUPPORT_NO_GAME` = true (core abre GUI próprio sem conteúdo)
- [x] `GET_MIDI_INTERFACE` → false; `GET_INPUT_BITMASKS` → false; `SET_HW_RENDER` → **aceito (GL)** quando hw_acceleration=enabled
- [x] `SET_CORE_OPTIONS_V2*` / `SET_VARIABLES` / `GET_VARIABLE` (responde todos os defaults declarados pelo core)

### 1.4 Boot do core
- [x] `retro_init()` + `retro_load_game(NULL)` → GUI do ScummVM
- [x] Thread de emulação (`RetroCore`) + thread de render (FBO compartilhado) + pacing
- [x] Verificação **Xbox** (boot pelo tile validado — `XBOX-TILE-LAUNCH-KILL.md` §10b)

### 1.5 Riscos a validar
- [x] Threading interno do core (`USE_LIBCO`) × modelo RetroCore (core prova stack de 1 MB no nightly 2026-10-19; ver `DISCOVERIES.md` §19)
- [x] Import de APIs não-UWP no buildbot DLL (ok — full-trust)
- [x] RGB565 mantido no caminho SW; GL é o caminho default

## Fase 2 — Bootstrap + staging em C++

- [x] Bootstrap em C++: extrair `system/scummvm.zip` → `LocalState\system` (flag de versão) — `Bootstrap.cpp`
- [x] `scummvm.ini` (`gui_theme`, `gui_scale`) se ausente
- [x] Re-stage limpo quando versão muda
- [x] Logging no app — `LogHelper.h` (shim próprio; arquivo `LocalState\scummvm-debug.log` + ODS gated por `IsDebuggerPresent`)
- [ ] **Remover** código C# morto (`SeedRetroArchConfig`, `FromAppFile.cs`, `MainPage.xaml.cs`) — arquivos C# ainda presentes como legado

## Fase 3 — Manifest + limpeza do payload RetroArch

- [x] Manifest: **1 Application entry** (ScummVM); sem entry RetroArch, sem protocolos
- [x] **Manter**: `runFullTrust` + `broadFileSystemAccess`
- [x] Pacote não inclui payload RetroArch (só `ScummVMLauncher.exe` + Mesa + `cores/` + `system/`)
- [ ] **Limpeza do workspace** — `launcher/ScummVMLauncher/` ainda contém artefatos mortos
      no disco: `RetroArch-msvcUWP.exe`, `Qt5*.dll`, `av*/libav*`, dezenas de DLLs
      MinGW, `retroarch.cfg`. Não entram no appx, mas poluem o diretório.
- [ ] `scripts/fetch-payload.ps1`: parar de baixar RetroArch (hoje ainda tenta — stale)
- [ ] Remover submodules mortos `extern/retroarch` + `extern/spdlog` (não referenciados)
- [x] Quit limpo: `RETRO_ENVIRONMENT_SHUTDOWN` → `CoreApplication::Exit()` → dashboard

## Fase 4 — Verificação no Xbox

- [x] Build (`scripts/build.ps1`) + deploy manual via Device Portal
- [x] 1ª execução — staging + GUI abre + tema carrega
- [x] 2ª execução — sem re-extract (flag == versão)
- [ ] bump de versão — re-stage limpo
- [ ] Fallback sem diretório externo
- [x] Game roda — GUI navegável, input e áudio OK
- [ ] Saves gravam (SAVE_DIR) e recarregam — verificar
- [x] Quit do GUI → dashboard (sem RA, sem cadeia)
- [ ] Teste por formato/jogo com pass/fail (com log) — pendente varredura sistemática
- [x] Boot pelo **tile** (não só F5) — validado

## Fase 5 — Release

- [ ] Docs finais: `HANDOFF.md`, `README.md`, `DISCOVERIES.md` (append)
- [ ] CI verde com app único (`.github/workflows/release.yml`)
- [ ] Empacotar e cortar release (tag `v...`)

## Fase GL — OpenGL via Mesa (renderização hardware)

Plano detalhado em `docs/opengl-plan/`. Resumo:

### GL.1 — DLLs Mesa no pacote
- [x] `opengl32.dll` + `libgallium_wgl.dll` + `dxil.dll` + `z-1.dll` (+ `libglapi`, `spirv_to_dxil`, `SDL2`)
- [x] Adicionados ao vcxproj como Content (em `mesa/`, aparecem na raiz do pacote)
- [x] Presença no .appx verificada

### GL.2 — Env handler + SET_HW_RENDER
- [x] `SET_HW_RENDER` aceito quando `scummvm_video_hw_acceleration=enabled`
- [x] Callbacks: `get_proc_address`, `get_framebuffer`, `context_reset`, `context_destroy`
- [x] Estado GL no caminho SDL (`SdlMain.cpp`)

### GL.3 — CreateGLContext + split init
- [x] Contexto Mesa WGL criado; FBO compartilhado via `wglShareLists` entre core thread e render thread
- [x] Split init: core thread faz `retro_init`/`load_game`/`run`; render thread (main) apresenta
- [x] GL mode → Mesa; SW mode → caminho software

### GL.4 — Frame presentation GL
- [x] Render thread faz blit FBO → tela + swap no cadence ~8ms
- [x] `retro_video_cb` só marca `g_frameReady`; present é do render thread

### GL.5 — Cleanup + fallback
- [x] Cleanup de contexto GL
- [ ] Fallback SW em todos os pontos de falha GL — validar cobertura
- [x] Docs atualizados (`ARCHITECTURE.md` §10, `DISCOVERIES.md` §19)

---

## Decisões log

- **2026-08-14**: Route D aprovado — frontend libretro próprio (port do
  dosbox-uwp) em vez de shell RetroArch. Core buildbot inalterado (zero
  recompilação do ScummVM). Sem XAML (ScummVM tem GUI própria). Staging E:
  mantido + fallback LocalState. `runFullTrust` mantido no manifest (é o que
  torna o fopen do core válido em processo).
- **2026-10-19**: Blocker de tile-launch resolvido (`58a476e` + `6de71e2`) —
  3 proteções restauradas no caminho SDL, verificado pelo tile. Ver
  `XBOX-TILE-LAUNCH-KILL.md`.
- **2026-10-19**: Core atualizado para o nightly buildbot (137 MB, base
  `2026.3.1git` inalterada; stack cothread dobra para 1 MB no upstream). Detalhe
  em `DISCOVERIES.md` §19. Rastreabilidade de build movida para o header do
  `.info`.
