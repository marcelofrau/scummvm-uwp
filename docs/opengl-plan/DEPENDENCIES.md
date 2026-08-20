# OpenGL Mode — Dependências e Análise

## Repos SternXD

### 1. mesa-uwp (Mesa Gallium3D para UWP/Xbox)

**URL:** https://github.com/SternXD/mesa-uwp
**Branch:** uwp-26.2.0
**O que é:** Build do Mesa (Gallium3D) com D3D12 backend para UWP/Xbox.

O que produz:
- `opengl32.dll` — Forwarder que exporta `gl*` e `wgl*`, redirecionando
  para `libgallium_wgl.dll`
- `libgallium_wgl.dll` — Driver WGL Gallium3D, cria contexto GL via D3D12
- `dxil.dll` — DirectX IL compiler (shader compilation)
- `z-1.dll` — zlib (compressão de texturas)

GL version: 4.6 (Compatibility Profile) + OpenGL ES 3.1
Backend: D3D12 (não D3D11)
Tamanho total: ~15 MB

**Não precisamos buildar** — os prebuilt existem.

### 2. uwp-dep (binários pré-compilados)

**URL:** https://github.com/SternXD/uwp-dep
**O que é:** Binários UWP pré-compilados, incluindo Mesa + SDL2 + GLFW.

```
uwp-dep/x64/bin/
├── opengl32.dll          (42 KB)   forwarder
├── libgallium_wgl.dll    (13.3 MB) Mesa GL 4.6
├── dxil.dll              (1.5 MB)  DX shader compiler
├── z-1.dll               (84 KB)   zlib
├── libuwp.dll            (62 KB)   UWP helpers
├── SDL2.dll              (1.1 MB)  não necessário para nós
├── GLFW3.dll             (430 KB)  não necessário para nós
├── libEGL.dll            (620 KB)  não necessário (usamos WGL)
├── libGLESv2.dll         (4.2 MB)  não necessário (usamos GL, não GLES)
└── ...
```

**Nós precisamos:** `opengl32.dll`, `libgallium_wgl.dll`, `dxil.dll`, `z-1.dll`
**Não precisamos:** SDL2, GLFW, EGL, GLES (usamos WGL direto)

### 3. ANGLE (não usado)

**URL:** https://github.com/SternXD/angle
**O que é:** OpenGL ES → D3D11 translation layer.
**Por que não:** Build pesado (~20GB), sem prebuilts, suporta só GL ES
(não desktop GL 4.6), D3D11 interop complexo com nosso frontend.

### 4. SDL3-uwp (não usado)

**URL:** https://github.com/SternXD/SDL3-uwp
**O que é:** SDL3 com suporte UWP restaurado.
**Por que não:** Não precisamos de SDL — o frontend já tem input, áudio,
window management próprios.

---

## GLAD no core ScummVM

O core ScummVM usa GLAD para carregar GL functions dinamicamente.
O fluxo de loading:

```cpp
// backends/graphics/opengl/texture.cpp
OpenGLContext::initialize(GetProcAddress get_proc) {
    // GLAD Loads ALL GL functions
    gladLoadGLLoader((GLADloadproc)get_proc);
    // Verifica NPOT, FBO, multitexture
}
```

O frontend precisa prover `get_proc_address` via `retro_hw_render_callback`.
Mesa WGL resolve via `wglGetProcAddress`.

---

## Headers GL para o frontend

O frontend precisa de headers GL para tipagem (typedefs). Opções:

### Opção A: copiar de mesa-uwp (RECOMENDADO)
Copiar de `mesa-uwp/include/GL/`:
- `gl.h` — typedefs e function prototypes
- `glext.h` — extensions
- `wglext.h` — WGL extensions

Para `launcher/ScummVMFrontend/GL/`. Incluir no vcxproj.

### Opção B: typedefs mínimos
Definir apenas o que o frontend usa diretamente:
```cpp
// GLDefs.h — typedefs mínimos para o frontend
typedef int GLint;
typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef void GLvoid;
// constants
#define GL_FALSE 0
#define GL_TRUE 1
```

**Opção A é melhor** — permite usar `glGetString()` e outras funções
para verificação/debug.

---

## Compatibilidade D3D12 no Xbox

| Feature | Xbox Series X | Xbox Series S | Requisito Mesa |
|---------|---------------|---------------|----------------|
| D3D Feature Level | 12.2 | 12.2 | ≥ 11.0 ✅ |
| D3D12 | Full | Full | ✅ |
| Ray Tracing | Sim | Limitado | Não usa |
| Mesh Shaders | Sim | Sim | Não usa |

D3D12 é amplamente suportado no Xbox. Mesa usa D3D12 para criar o swap
chain e gerenciar texturas/buffers. Não há risco de incompatibilidade.

---

## Impacto no tamanho do pacote

| Componente | Tamanho |
|------------|---------|
| Frontend atual (exe + core + scummvm.zip) | ~125 MB |
| Mesa DLLs (adicionais) | ~15 MB |
| **Total** | **~140 MB** |

Incremento aceitável (~12%). As DLLs são comprimidas no .appx (LP固有 de MSIX).
