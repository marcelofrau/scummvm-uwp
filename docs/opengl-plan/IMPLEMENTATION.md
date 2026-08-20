# OpenGL Mode — Plano de Implementação

Estimativa total: **3-4 dias**. Cada fase é testável independentemente.

---

## Fase 1 — Adicionar DLLs Mesa ao pacote (0.5 dia)

**Objetivo:** DLLs Mesa presentes no package, prontas para uso.

### Tasks

- [ ] Copiar de `F:\workspace\uwp-dep\x64\bin\` para o package:
  - `opengl32.dll` (42 KB)
  - `libgallium_wgl.dll` (13.3 MB)
  - `dxil.dll` (1.5 MB)
  - `z-1.dll` (84 KB)
- [ ] Adicionar ao `ScummVMFrontend.vcxproj` como `<Content Include="...">`
- [ ] Verificar que DLLs aparecem no `.appx` após build
- [ ] Testar: build Debug, verificar DLLs no AppPackages output

### Critério de aceite
DLLs visíveis no diretório do exe no package.

---

## Fase 2 — Env handler + SET_HW_RENDER (1 dia)

**Objetivo:** Core pode negociar HW render com o frontend.

### Tasks

- [ ] Adicionar ao `env_handler()` em `RetroCore.cpp`:
  - Responder `RETRO_ENVIRONMENT_SET_HW_RENDER` com `true` quando
    `scummvm_video_hw_acceleration=enabled`
  - Preencher `retro_hw_render_callback`:
    - `context_type = RETRO_HW_CONTEXT_OPENGL`
    - `bottom_left_origin = true`
    - `cache_context = false`
    - `get_current_framebuffer = callback`
    - `get_proc_address = callback`
    - `context_reset = callback`
    - `context_destroy = callback`
  - Guardar ponteiro do callback para uso futuro
- [ ] Adicionar membros ao `ScummVMMain.h`:
  - `bool m_useGL = false`
  - `bool m_frameReadyGL = false`
  - `HGLRC m_glContext = nullptr`
  - `HDC m_glDC = nullptr`
  - `HMODULE m_glLib = nullptr`
  - Function pointers: `wglCreateContext`, `wglMakeCurrent`, `wglSwapBuffers`,
    `wglGetProcAddress`, `wglDeleteContext`
- [ ] Implementar callbacks no frontend:
  - `GetFramebufferCallback()` → retorna `0`
  - `GetProcAddressCallback(name)` → `wglGetProcAddress(name)`
  - `ContextResetCallback()` → `wglMakeCurrent(m_glDC, m_glContext)`
  - `ContextDestroyCallback()` → `wglMakeCurrent(NULL, NULL)`
- [ ] Adicionar `GET_CORE_OPTIONS_VERSION` = 2 (já existe)
- [ ] Log markers: `[boot] GL mode: REQUESTED` / `[boot] GL mode: NOT REQUESTED`

### Critério de aceite
Log mostra `SET_HW_RENDER accepted` quando `scummvm_video_hw_acceleration=enabled`.

---

## Fase 3 — CreateGLContext + WGL init (1 dia)

**Objetivo:** Contexto GL Mesa funcionando no CoreWindow.

### Tasks

- [ ] Criar `ScummVMMain::CreateGLContext()`:
  ```
  m_glLib = LoadLibrary(L"opengl32.dll")
  wglCreateContext  = GetProcAddress(m_glLib, "wglCreateContext")
  wglMakeCurrent    = GetProcAddress(m_glLib, "wglMakeCurrent")
  wglGetProcAddress = GetProcAddress(m_glLib, "wglGetProcAddress")
  wglSwapBuffers    = GetProcAddress(m_glLib, "wglSwapBuffers")
  wglDeleteContext   = GetProcAddress(m_glLib, "wglDeleteContext")
  m_glDC = (HDC)winrt::get_abi(CoreWindow::GetForCurrentThread())
  m_glContext = wglCreateContext(m_glDC)
  wglMakeCurrent(m_glDC, m_glContext)
  ```
- [ ] Headers: copiar `mesa-uwp/include/GL/gl.h` + `glext.h` para
  `launcher/ScummVMFrontend/GL/` OU definir typedefs mínimos
- [ ] Deferir `CreateWindowSizeDependentResources()` até após o boot async:
  - Se `m_useGL == true` → `CreateGLContext()` (não cria D3D11 swap chain)
  - Se `m_useGL == false` → `CreateD3D11Resources()` (fluxo atual)
- [ ] Modificar `ScummVMMain` constructor para NÃO chamar
  `CreateWindowSizeDependentResources` — mover para `CreatePresentationResources()`
- [ ] Log markers: `[boot] GL context created OK` / `[boot] GL context FAILED, fallback SW`

### Critério de aceite
F5 no VS: log mostra GL context created. `glGetString(GL_VERSION)` loga versão
Mesa (ex: "4.6 (Compatibility Profile) Mesa ...").

---

## Fase 4 — Frame presentation GL (1 dia)

**Objetivo:** Core renderiza via GL, Mesa apresenta no CoreWindow.

### Tasks

- [ ] No env handler, quando `video_cb(HW_FRAME_BUFFER_VALID)` é chamado:
  - Setar `m_frameReadyGL = true`
- [ ] No `Update()` (UI thread):
  - Se `m_frameReadyGL && m_useGL`:
    - `wglSwapBuffers(m_glDC)`
    - `m_frameReadyGL = false`
- [ ] No `Render()`:
  - Se `m_useGL`: skip D2D rendering (Mesa apresenta via swap chain próprio)
  - Se SW: fluxo atual (D2D + D3D11 Present)
- [ ] No `retro_video_refresh_t` callback:
  - Se HW mode: ignorar dados (core renderiza no FBO, não em RAM)
  - Se SW mode: fluxo atual (RGB565→BGRA conversion)
- [ ] Log markers: `[emu] GL frame NNNN presented via wglSwapBuffers`
- [ ] Adicionar fallback: se GL falhar em qualquer ponto, desabilitar
  `m_useGL` e re-criar D3D11 resources

### Critério de aceite
ScummVM GUI visível no Xbox em GL mode. Shaders/filtros funcionando.
Frame rate ≥30fps.

---

## Fase 5 — Cleanup e polish (0.5 dia)

**Objetivo:** Destruição limpa, fallback robusto, logs completos.

### Tasks

- [ ] Destrutor: `wglMakeCurrent(NULL)` → `wglDeleteContext(rc)` → `FreeLibrary`
- [ ] Fallback SW em todos os pontos de falha GL
- [ ] `scummvm.ini` defaults: adicionar `filtering=hardware` e
  `aspect_ratio_correction=true` quando GL mode ativo
- [ ] Atualizar `docs/ARCHITECTURE.md` seção 2 (contrato do core) — adicionar
  `SET_HW_RENDER` como opcional
- [ ] Atualizar `docs/IMPLEMENTATION-PLAN.md` — marcar Fase GL completa
- [ ] Commit + push

### Critério de aceite
- GL mode funciona no Xbox (F5 + standalone)
- SW mode continua funcionando (regression test)
- Clean shutdown sem crashes
- Logs mostram todo o fluxo GL

---

## Dependências entre fases

```
Fase 1 (DLLs) ──→ Fase 2 (Env handler) ──→ Fase 3 (GL context) ──→ Fase 4 (Presentation) ──→ Fase 5 (Cleanup)
                    │                        │
                    └── testável isolado ─────┘ (log mostra negociação)
```

Fases 1-2 são independentes de código existente (additive only).
Fase 3 requer refator do constructor (risco maior).
Fase 4 requer split do presentation path (mudança estrutural).
Fase 5 é polish e documentação.

---

## Riscos e mitigações

| Risco | Impacto | Mitigação |
|-------|---------|-----------|
| D3D12 (Mesa) incompatível com Xbox FL | Alto | Testar Fase 3 early; fallback SW garantido |
| libco + GL context affinity | Alto | Prof. confirma mesma OS thread; testar com frames pesados |
| opengl32.dll load conflict com D3D11 | Médio | Nunca ambos ao mesmo tempo; split init |
| Performance D3D12 vs D3D11 no Xbox | Médio | Medir frame time; D3D12 FL 12.x no Series X|S |
| DLLs não encontradas no package | Baixo | Verificar no build; log erro claro |
