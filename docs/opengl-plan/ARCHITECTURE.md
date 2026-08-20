# OpenGL Mode — Arquitetura

## Visão geral

O frontend ScummVM UWP opera em dois modos mutuamente exclusivos,
determinados pelo core option `scummvm_video_hw_acceleration` lido durante
`retro_init()`:

```
┌─────────────────────────────────────────────────────────────────┐
│ ScummVMLauncher.exe                                             │
│                                                                 │
│  CoreWindow                                                     │
│  ├── RetroCore (emu thread)                                     │
│  ├── XAudio2Output (áudio)          ← compartilhado ambos modos │
│  ├── SdlInput (gamepad)             ← compartilhado ambos modos │
│  │                                                              │
│  │  ┌───── Software Mode ─────┐  ┌───── GL Mode ──────────┐   │
│  │  │ RetroScreenRenderer     │  │ Mesa WGL Context        │   │
│  │  │ (D2D bitmap)            │  │ (libgallium_wgl.dll)    │   │
│  │  │ RGB565→BGRA8888 CPU     │  │ GL FBO via core         │   │
│  │  │ RetroD3D11Renderer      │  │ wglSwapBuffers()        │   │
│  │  │ (D3D11 swap chain)      │  │ (Mesa D3D12 flip chain) │   │
│  │  │ Present()               │  │                         │   │
│  │  └─────────────────────────┘  └─────────────────────────┘   │
│  │                                                              │
│  └── VFS (compartilhado ambos modos)                            │
└─────────────────────────────────────────────────────────────────┘
```

## Fluxo de decisão

```
App::Initialize()
  └── Bootstrap (async): extrai system, LoadLibrary core
       └── retro_set_environment(env_handler)
       └── retro_init()
            └── core lê scummvm_video_hw_acceleration
                 ├── "disabled" → video_hw_mode = REQUEST_SW
                 └── "enabled"  → video_hw_mode = REQUEST_HW
                      └── core chama SET_HW_RENDER
                           ├── frontend aceita → HW mode
                           └── frontend recusa → fallback SW

  └── App::Run() → Update()
       └── 1º frame: CreatePresentationResources()
            ├── m_useGL == true  → CreateGLContext()     [Mesa WGL]
            └── m_useGL == false → CreateD3D11Resources() [atual]
```

## Componentes GL mode

### Mesa WGL Context

Responsabilidade: criar e manter o contexto OpenGL no CoreWindow.

```
Inicialização:
  HMODULE gl = LoadLibrary(L"opengl32.dll")
  wglCreateContext  = GetProcAddress(gl, "wglCreateContext")
  wglMakeCurrent    = GetProcAddress(gl, "wglMakeCurrent")
  wglGetProcAddress = GetProcAddress(gl, "wglGetProcAddress")
  wglSwapBuffers    = GetProcAddress(gl, "wglSwapBuffers")

  HDC hdc = (HDC)winrt::get_abi(CoreWindow::GetForCurrentThread())
  HGLRC rc = wglCreateContext(hdc)
  wglMakeCurrent(hdc, rc)
```

**Sem SetPixelFormat** — Mesa auto-detecta B8G8R8A8_UNORM via stubs GDI
(`gdi_uwp.cpp:GetPixelFormat`). **Sem D3D device manual** — Mesa cria
D3D12 internamente (`d3d12_create_dxgi_screen`). **Sem swap chain manual**
— Mesa cria `CreateSwapChainForCoreWindow` internamente.

### GLAD / proc addresses

O core ScummVM usa GLAD para carregar GL functions. O fluxo:

1. Frontend expõe `get_proc_address(name)` via `hw_render` callback
2. Core chama `get_proc_address("glGenTextures")` etc.
3. Frontend delega para `wglGetProcAddress(name)`
4. GLAD binding发生在 core side (`OpenGLContext.initialize()`)

**Thread affinity:** O contexto GL deve estar na mesma OS thread que
`retro_run()`. Como o core usa libco fibers (cooperative), a emu thread
é a mesma OS thread que chama `retro_run` → contexto GL permanece
válido durante todo o frame.

### Frame presentation (GL mode)

```
Emu thread:                         UI thread:
  retro_run()                         └── (idle, sem D2D)
    └── core.renderToGLFBO()
    └── video_cb(HW_FRAME_BUFFER_VALID, w, h, 0)
                                      └── wglSwapBuffers(hdc)
                                           └── Mesa apresenta no CoreWindow
```

Comparação com software mode:

| Aspecto | Software | GL |
|---------|----------|-----|
| Core renderiza para | `surface->getPixels()` (RAM) | GL FBO (GPU) |
| Frontend converte | RGB565→BGRA8888 CPU | nenhuma |
| Upload para GPU | `D2D bitmap::CopyFromMemory` | nenhuma (zero-copy) |
| Apresentação | `D3D11 Present()` | `wglSwapBuffers()` |
| Swap chain | D3D11 flip model | D3D12 flip model (Mesa) |
| Overlay D2D | ✅ disponível | ❌ indisponível (sem D2D) |
| Shaders/filtros | ❌ CPU only | ✅ GLSL pipeline do core |

### Destruição

```
~ScummVMMain()
  └── if (m_glContext)
       wglMakeCurrent(NULL, NULL)
       wglDeleteContext(m_glContext)
       FreeLibrary(m_glLib)
```

## Restrições conhecidas

1. **D3D11 ↔ D3D12:** não misturar swap chains no mesmo CoreWindow.
   GL mode usa exclusivamente Mesa/D3D12; SW mode usa exclusivamente D3D11.
2. **Hot-toggle impossível:** o core exige reinício para trocar SW↔GL
   (OSD já mostra "Core reload needed").
3. **D2D overlay indisponível em GL:** mensagens de diagnóstico (gamepad,
   heartbeats) ficam no log em vez de overlay na tela.
4. **D3D12 Feature Level:** Mesa requer FL 11.0 mínimo. Xbox Series X|S
   suporta FL 12.x — compatível.
5. **DLL loading:** `opengl32.dll` + `libgallium_wgl.dll` + `dxil.dll` +
   `z-1.dll` devem estar no mesmo diretório do exe (package install dir).
