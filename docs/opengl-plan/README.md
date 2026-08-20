# OpenGL Mode — ScummVM UWP via Mesa

Status: **planejado (Fase pendente)**. Author: Marcelo Frau + opencode.
Criado: 2026-08-21. Última atualização: 2026-08-21.

---

## Objetivo

Habilitar renderização OpenGL no ScummVM libretro core dentro do frontend UWP
do Xbox, substituindo o caminho software (RGB565 → conversão CPU → D2D) por
OpenGL 4.6 hardware-accelerado via Mesa Gallium (D3D12 backend).

### Por quê?

O core ScummVM já vem compilado com `USE_OPENGL` habilitado no buildbot —
o path GL completo existe mas nunca foi ativado porque o frontend sempre
respondeu `false` para `SET_HW_RENDER`. Ativar GL desbloqueia:

- **Filtros/scalers do GPU** — Scanline, Super2x,hq2x, etc. rodando no D3D12
  em vez de CPU
- **Shaders GLSL** — presets `.glslp` do ScummVM (pipeline `LibRetroPipeline`)
- **Aspect ratio correction** — correção de aspecto nativa do GL
- **Resolução nativa** — frame sempre em resolução real do display, sem
  stretching manual

### Não-objetivos

- Não substitui o modo software — é adicional (fallback garantido)
- Não requer rebuild do core — `scummvm_libretro.dll` já tem GL compilado
- Não modifica o RetroArch real do usuário

---

## Pré-requisitos (já satisfeitos)

| Item | Status | Detalhe |
|------|--------|---------|
| Core com `USE_OPENGL` | ✅ pronto | Buildbot: strings `scummvm_video_hw_acceleration` + `libretro-graphics-opengl` no binário |
| Mesa UWP prebuilt | ✅ disponível | `uwp-dep/x64/bin/`: `opengl32.dll` (42KB) + `libgallium_wgl.dll` (13MB) + `dxil.dll` + `z-1.dll` |
| WGL context no UWP | ✅ trivial | 5 linhas: `LoadLibrary` → `wglCreateContext` → `wglMakeCurrent` |
| Core negotiation | ✅ já funciona | Core chama `SET_HW_RENDER` durante `retro_init` se `scummvm_video_hw_acceleration=enabled` |

---

## Restrição principal

D3D11 (frontend atual) e D3D12 (Mesa) não podem compartilhar swap chain
no mesmo CoreWindow. O frontend precisa ter **dois modos de inicialização**:

```
                    ┌── scummvm_video_hw_acceleration=disabled ──→ Software mode
 retro_init() ─────┤   D2D/D3D11, RGB565→BGRA, Present()         (atual)
                    │
                    └── scummvm_video_hw_acceleration=enabled ──→ GL mode
                        Mesa WGL, GL FBO, wglSwapBuffers()       (novo)
```

---

## Documentação neste diretório

| Arquivo | Conteúdo |
|---------|----------|
| `README.md` | Este arquivo — visão geral e objetivos |
| `ARCHITECTURE.md` | Arquitetura GL mode, diagramas, componentes |
| `SPEC.md` | Especificações técnicas, contratos de API, data flow |
| `IMPLEMENTATION.md` | Plano de implementação fase a fase, com tasks |
| `DEPENDENCIES.md` | Análise de Mesa/uwp-dep, inventário de binários |

---

## Referências

- SternXD/mesa-uwp — https://github.com/SternXD/mesa-uwp (branch uwp-26.2.0)
- SternXD/uwp-dep — https://github.com/SternXD/uwp-dep (binários pré-compilados)
- SternXD/angle — https://github.com/SternXD/angle (alternativa, não usada)
- SternXD/SDL3-uwp — https://github.com/SternXD/SDL3-uwp (não necessário)
- ScummVM libretro GL: `extern/scummvm/backends/platform/libretro/src/libretro-graphics-opengl.cpp`
- Mesa WGL UWP: `mesa-uwp/src/gallium/winsys/d3d12/wgl/d3d12_wgl_framebuffer_uwp.cpp`
- Mesa GDI stubs: `mesa-uwp/src/gallium/winsys/uwp/gdi_uwp.cpp`
