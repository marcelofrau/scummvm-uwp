# OpenGL Mode — Especificações Técnicas

## 1. Core option: `scummvm_video_hw_acceleration`

**Chave:** `scummvm_video_hw_acceleration`
**Tipo:** `RETRO_VAR_TYPE_BOOL`
**Default:** `"disabled"`

| Valor | Comportamento |
|-------|---------------|
| `"disabled"` | Core usa software rendering (RGB565). Frontend converte → BGRA8888. D2D/D3D11. |
| `"enabled"` | Core pede `SET_HW_RENDER`. Frontend cria contexto GL Mesa. Core renderiza via GL. |

**Definição no core:** `libretro-core.cpp:492-508`, `retro_set_environment` callback.
Leitura via `retro_get_variable()`. Alteração exige reinício do core.

## 2. HW render negotiation

Quando `scummvm_video_hw_acceleration=enabled`, o core executa:

```c
// libretro-core.cpp:142-173
struct retro_hw_render_callback hw_render = {};
hw_render.context_type = RETRO_HW_CONTEXT_OPENGL;  // ou OPENGLES2
hw_render.context_reset = context_reset;
hw_render.context_destroy = context_destroy;
hw_render.get_current_framebuffer = retro_get_hw_fb;
hw_render.get_proc_address = retro_get_proc_address;
hw_render.bottom_left_origin = true;
hw_render.cache_context = false;
hw_render.version_major = 0;  // qualquer versão
hw_render.version_minor = 0;

environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render);
```

**Resposta do frontend:**

| Callback | Retorno | Descrição |
|----------|---------|-----------|
| `get_current_framebuffer()` | `0` | Default FBO (backbuffer do contexto GL Mesa) |
| `get_proc_address(name)` | `wglGetProcAddress(name)` | GL function pointer via Mesa WGL |
| `context_reset()` | `wglMakeCurrent(hdc, rc)` | Reativa contexto GL (após GC ou suspensão) |
| `context_destroy()` | `wglMakeCurrent(NULL, NULL)` | Desativa contexto GL |

## 3. Pixel format

| Modo | Pixel Format | Cores |
|------|-------------|-------|
| Software | `RETRO_PIXEL_FORMAT_RGB565` | 16-bit, 5-6-5 |
| GL | `RETRO_PIXEL_FORMAT_XRGB8888` | 32-bit, 8-8-8-8 (ignorado pelo core em GL) |

Em GL mode, o core renderiza direto no FBO — o pixel format é irrelevante
para a apresentação. O framebuffer GL é RGBA nativo.

## 4. GL requirements (core side)

Baseado em `backends/graphics/opengl/context.cpp` + `Makefile.common`:

| Requisito | Mínimo | Observação |
|-----------|--------|------------|
| GL version | 2.0 (desktop) ou ES 2.0 | GLAD load |
| GLSL version | 110 (desktop) ou ES 100 (GLES2) | Engines individuais pedem ≥120 |
| Extensions | `GL_ARB_texture_non_power_of_two` | NPOT textures; implícito em GL 2.0+ |
| Extensions | `GL_ARB_framebuffer_object` ou `GL_EXT_framebuffer_object` | FBO mandatory |
| Extensions | `GL_ARB_multitexture` | Multi-texture; obrigatório para `LibRetroPipeline` |

### Pipelines de rendering

O core tem 3 pipelines, selecionados por capacidade do contexto:

1. **`LibRetroPipeline`** (desejado) — requer: shaders + multitexture + FBO.
   Suporta `.glslp` presets, filtros GPU, shaders customizados.
2. **`ShaderPipeline`** (fallback) — requer: shaders + multitexture.
   Fallback se FBO indisponível.
3. **`FixedPipeline`** (último recurso) — fixed-function OpenGL.
   Sem filtros, sem shaders.

Com Mesa GL 4.6 → `LibRetroPipeline` é selecionado automaticamente (todas
as capacidades disponíveis).

## 5. Mesa DLLs — inventário

Fonte: `F:\workspace\uwp-dep\x64\bin\` (release 26.2.0)

| DLL | Tamanho | Função | Obrigatória |
|-----|---------|--------|-------------|
| `opengl32.dll` | 42 KB | Forwarder: exports `gl*` + `wgl*` → `libgallium_wgl.dll` | Sim |
| `libgallium_wgl.dll` | 13.3 MB | Gallium3D WGL driver (D3D12 backend) | Sim |
| `dxil.dll` | 1.5 MB | DirectX shader compiler (Mesa dependency) | Sim |
| `z-1.dll` | 84 KB | zlib (compressão de texturas) | Sim |
| `libuwp.dll` | 62 KB | UWP helpers (refresh rate, events) | Não |

**Total obrigatório:** ~15 MB adicionais no pacote.

### Header requirements

`uwp-dep` não inclui headers GL. Usar de `mesa-uwp/include/GL/`:
- `gl.h` — OpenGL function declarations
- `glext.h` — Extension declarations
- `glcorearb.h` — Core profile (opcional)
- `wglext.h` — WGL extension declarations

Ou: definir typedefs manualmente (padrão MAME-uwp pattern):
```c
typedef int GLint;
typedef unsigned int GLuint;
typedef unsigned int GLenum;
// etc. — apenas o que o frontend usa diretamente
```

## 6. Contrato de apresentação (data flow)

### Software mode (atual)

```
Core                    Frontend                    GPU
  │                        │                         │
  ├── retro_run()          │                         │
  │   render game →        │                         │
  │   surface->getPixels() │                         │
  │   (RGB565, RAM)        │                         │
  │─── video_cb(pixels) ──→│                         │
  │                        ├── RGB565→BGRA8888 (CPU) │
  │                        ├── bitmap.CopyFromMemory │
  │                        │   (D2D bitmap)           │
  │                        ├── RenderTarget→bitmap    │
  │                        │   (D2D composite)        │
  │                        ├── Present() ────────────→│
  │                        │   (D3D11 flip)           │
```

### GL mode (novo)

```
Core                    Frontend              Mesa          GPU
  │                        │                   │             │
  ├── retro_run()          │                   │             │
  │   render game →        │                   │             │
  │   OpenGL FBO ──────────┼───────────────────┼──→ D3D12   │
  │   (GPU, zero-copy)     │                   │   texture   │
  │─── video_cb(HW_VALID) ─→│                   │             │
  │                        ├── wglSwapBuffers ─→│             │
  │                        │                   ├── flip ────→│
  │                        │                   │   present   │
```

## 7. Fallback behavior

| Cenário | Comportamento |
|---------|---------------|
| `scummvm_video_hw_acceleration=disabled` | Software mode (atual) |
| `scummvm_video_hw_acceleration=enabled` + GL OK | GL mode |
| `scummvm_video_hw_acceleration=enabled` + GL falhou | Fallback para software mode + log warning |
| Mesa DLLs ausentes | LoadLibrary falha → fallback SW + log error |
| `wglCreateContext` falha | Fallback SW + log error |
| `SET_HW_RENDER` rejeitado pelo core | Fallback automático (core chama SW) |

## 8. Thread safety

| Recurso | Thread | Observação |
|---------|--------|------------|
| `retro_run()` | Emu thread (libco fiber) | GL context obrigatoriamente nesta thread |
| `wglSwapBuffers()` | UI thread | Inativa sem contexto — precisa `context_reset` first |
| `XAudio2` | Emu thread (callback) | Compartilhado, inalterado |
| `SdlInput` | UI thread (poll) | Compartilhado, inalterado |

**Crucial:** O core usa libco (cooperative fibers). A emu thread
não muda de OS thread — o GL context permanece válido durante todo
o `retro_run()` incluindo `co_switch`. Não há thread affinity issue.

**wglSwapBuffers timing:** Chamado após `video_cb(HW_FRAME_BUFFER_VALID)`.
Pode ser na emu thread (após retro_run) ou na UI thread (next Update).
A opção mais simples: chamar na UI thread no próximo Update() após
detectar `m_frameReadyGL = true`.
