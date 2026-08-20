# ScummVM UWP — Plano de Implementação (single-app, frontend libretro próprio)

Status: **em execução**. Author: Marcelo Frau. Última atualização: 2026-08-16.

Checklist mestre da migração: de "launcher C# + RetroArch shell (2 apps)" para
**um único appx com frontend libretro próprio** que carrega `scummvm_libretro.dll`
direto. Marca cada item com `[x]` conforme for concluído. Design e justificativas
em `PORT-PLAN.md`; arquitetura-alvo em `ARCHITECTURE.md`.

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

### 1.1 Vendor de dependências
- [ ] Vendor `libretro.h` (cabeçalho da API libretro)
- [ ] Vendor `libretro-common` mínimo: `vfs/vfs.h`, `vfs/vfs_implementation.h`,
      `vfs/vfs_implementation_uwp.cpp` (fonte: `vs2022\dosbox-pure-unleashed-uwp\extern\libretro-common`)
- [ ] Include paths no projeto apontando pros vendored dirs

### 1.2 Port do frontend (fonte: `vs2022\dosbox-pure-unleashed-uwp\dosbox-uwp`)
- [ ] `Common/` — `DeviceResources`, `DirectXHelper`, `StepTimer`
- [ ] `Content/RetroCore.cpp/.h` — **adaptar**: `LoadLibrary("cores\\scummvm_libretro.dll")`
      + `GetProcAddress` dos símbolos `retro_*` (substitui `dosbox_pure_sta.h`)
- [ ] `Content/RetroD3D11Renderer` + `RetroScreenRenderer` + shaders — **suportar
      RGB565** (converter → BGRA8888; core ScummVM usa RGB565 em modo software)
- [ ] `Content/XAudio2Output` — conferir sample rate do core (44.1k/48k)
- [ ] `Content/SdlInput` — gamepad → RetroPad (mapping do CONTROLS.md)
- [ ] `App.cpp` + `ScummVMMain.cpp` (CoreWindow loop) — substitui App XAML C#
- [ ] **Descartar** do dosbox-uwp (ScummVM tem GUI própria): `FileBrowser`,
      `FrontendMenu`, `AboutDialog`, `ConfirmDialog`, `SettingsManager`

### 1.3 Env handler do core ScummVM (mais commands que dosbox)
- [ ] `GET_SYSTEM_DIRECTORY` → `LocalState\system`
- [ ] `GET_SAVE_DIRECTORY` → `LocalState\saves`
- [ ] `GET_LIBRETRO_PATH` → caminho do DLL
- [ ] `GET_LOG_INTERFACE`, `GET_LANGUAGE`, `GET_PLAYLIST_DIRECTORY`
- [ ] `GET_VFS_INTERFACE` → VFS FromApp (vfs_implementation_uwp)
- [ ] `SET_SUPPORT_NO_GAME` = true (core abre GUI próprio sem conteúdo)
- [ ] Retornar false (fallback do core é seguro): `GET_MIDI_INTERFACE`,
      `SET_HW_RENDER`, `GET_INPUT_BITMASKS`
- [ ] `SET_CORE_OPTIONS_V` / `SET_VARIABLES` / `GET_VARIABLE` / `GET_VARIABLE_UPDATE`

### 1.4 Boot do core
- [ ] `retro_init()` + `retro_load_game(NULL)` → GUI do ScummVM
- [ ] Thread de emulação (padrão RetroCore) + ring de frames + pacing
- [ ] Verificação **Windows** (UWP desktop) antes do Xbox

### 1.5 Riscos a validar
- [ ] Threading interno do core (`retro_init_emu_thread`) × modelo RetroCore
- [ ] Import de APIs não-UWP no buildbot DLL (baixo — full-trust como hoje)
- [ ] RGB565 nos dois caminhos: GUI do ScummVM + jogo

## Fase 2 — Bootstrap + staging em C++

- [ ] Reescrever bootstrap em C++: extrair `system/scummvm.zip` →
      `LocalState\system` (flag `.scummvm-ready` com versão)
- [ ] `scummvm.ini` (`gui_theme=scummremastered`, `gui_scale=150`) se ausente
- [ ] Re-stage limpo quando versão muda (delete-first, preserva `scummvm.ini`)
- [ ] Logging no app (substitui `launcher.log`; `OutputDebugStringA` + arquivo)
- [ ] **Remover**: `SeedRetroArchConfig`, `retroarch.cfg` seed/rewrite,
      `CoreLastRunResolved`, `StageToE` C#, `FromAppFile.cs`, `MainPage.xaml.cs`

## Fase 3 — Manifest + limpeza do payload RetroArch

- [ ] Manifest: **1 Application entry** (ScummVM); remover entry RetroArch +
      `AppListEntry` + protocolos `scummvm-core:`/`scummvm-launcher:`
- [ ] **Manter**: `runFullTrust` + `broadFileSystemAccess` (fopen do core + browse E:)
- [ ] Remover do appx: `RetroArch-msvcUWP.exe` + DLLs runtime (Qt5/SDL2/ANGLE/ffmpeg)
- [ ] `scripts/fetch-payload.ps1`: parar de baixar RetroArch (só core + zip)
- [ ] Quit limpo: `RETRO_ENVIRONMENT_SHUTDOWN` → `CoreApplication::Exit()` → dashboard

## Fase 4 — Verificação no Xbox

- [ ] Build (`scripts/build.ps1`) + deploy manual via Device Portal
      (`https://<ip>:11443`, CSRF, campo `InstalledPackages`; nunca desinstalar RA real)
- [ ] Caso A: 1ª execução — staging E: + GUI abre + tema carrega
- [ ] Caso B: 2ª execução — sem re-extract (flag == versão)
- [ ] Caso C: bump de versão — re-stage limpo
- [ ] Caso D: sem E: — fallback LocalState funciona
- [ ] Game roda (SCUMM + SKY): vídeo RGB565, áudio, input (CONTROLS.md)
- [ ] Saves gravam (SAVE_DIR) e recarregam
- [ ] Quit do jogo → GUI ScummVM; quit do GUI → dashboard (sem RA, sem cadeia)
- [ ] Teste por formato/jogo com pass/fail (com log)

## Fase 5 — Release

- [ ] Docs finais: `HANDOFF.md`, `README.md` (features reais), `DISCOVERIES.md` (append)
- [ ] CI verde com app único (`.github/workflows/release.yml` — sem RA, sem LFS novo)
- [ ] Empacotar e cortar release (tag `v...`)

## Fase GL — OpenGL via Mesa (renderização hardware)

Plano detalhado em `docs/opengl-plan/`. Resumo:

### GL.1 — DLLs Mesa no pacote
- [ ] Copiar `opengl32.dll` + `libgallium_wgl.dll` + `dxil.dll` + `z-1.dll` de `uwp-dep`
- [ ] Adicionar ao vcxproj como Content
- [ ] Verificar presence no .appx

### GL.2 — Env handler + SET_HW_RENDER
- [ ] Responder `SET_HW_RENDER` com true quando `scummvm_video_hw_acceleration=enabled`
- [ ] Implementar callbacks: `get_proc_address`, `get_framebuffer`, `context_reset`, `context_destroy`
- [ ] Adicionar membros GL ao ScummVMMain (m_glContext, m_glDC, m_glLib, m_useGL)

### GL.3 — CreateGLContext + split init
- [ ] `CreateGLContext()`: LoadLibrary → wglCreateContext → wglMakeCurrent
- [ ] Defer swap chain: CreatePresentationResources() após boot async
- [ ] GL mode → contexto Mesa; SW mode → D3D11 (fluxo atual)

### GL.4 — Frame presentation GL
- [ ] video_cb(HW_FRAME_BUFFER_VALID) → m_frameReadyGL = true
- [ ] Update(): wglSwapBuffers quando frame ready em GL mode
- [ ] Render(): skip D2D em GL mode

### GL.5 — Cleanup + fallback
- [ ] Destrutor: wglDeleteContext + FreeLibrary
- [ ] Fallback SW em todos os pontos de falha GL
- [ ] Atualizar docs

---

## Decisões log

- **2026-08-14**: Route D aprovado — frontend libretro próprio (port do
  dosbox-uwp) em vez de shell RetroArch. Core buildbot inalterado (zero
  recompilação do ScummVM). Sem XAML (ScummVM tem GUI própria). Staging E:
  mantido + fallback LocalState. `runFullTrust` mantido no manifest (é o que
  torna o fopen do core válido em processo).
