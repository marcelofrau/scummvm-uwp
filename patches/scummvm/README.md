# ScummVM patches

Patches aplicaveis ao submodule `extern/scummvm` (HEAD `e833307e`).
O submodule fica **100% upstream/limpo** (sem fork); os fixes vivem aqui e sao
aplicados sob demanda. Origem do patching: `docs/DISCOVERIES.md` ## 15/16.

## Aplicar / reverter

```powershell
# aplicar
git -C extern/scummvm apply F:\workspace\scummvm-uwp\patches\scummvm\0001-gameoptions-misc-theme.patch
git -C extern/scummvm apply F:\workspace\scummvm-uwp\patches\scummvm\0002-msvc-libretro-build.patch

# reverter
git -C extern/scummvm apply -R F:\workspace\scummvm-uwp\patches\scummvm\0001-gameoptions-misc-theme.patch
git -C extern/scummvm apply -R F:\workspace\scummvm-uwp\patches\scummvm\0002-msvc-libretro-build.patch

# sem dirty tree: git -C extern/scummvm checkout -- .
```

## 0001-gameoptions-misc-theme.patch — fix Game Options (tab Misc) — ESSENCIAL

**Sintoma:** abrir Game Options de um jogo e clicar na aba "Misc" mata o
ScummVM (crash fatal).

**Causa raiz:** skew de versao entre o **binario do core** (buildbot,
ScummVM 2026.3.1git) e os **temas** empacotados. O binario registra o dialogo
`GameOptions_Misc` (nova feature de hotspot markers); os temas gerados do
source do submodule (`gui/themes/*.stx`) nao tinham esse layout → o parser de
tema nao encontra o dialogo e o ScummVM chama `error()` (`gui/object.cpp:79`),
que e fatal. O diagnostico inicial de "heap corruption" era um desvio; o
crash era sempre nesse lookup de layout.

**Fix:** adiciona os blocos `GameOptions_Misc` +
`GameOptions_Misc_Container` (EnableHotspots, HotspotMarkerPopup,
ShowHotspotText) em 4 arquivos de tema:

- `gui/themes/common/highres_layout.stx`
- `gui/themes/common/lowres_layout.stx`
- `gui/themes/scummclassic/classic_layout.stx`
- `gui/themes/scummclassic/classic_layout_lowres.stx`

**Onde o fix ja vive:** o `launcher/ScummVMLauncher/system/scummvm.zip`
**versionado** ja contem os temas corrigidos (os zips de tema dentro de
`theme/` foram regenerados com o patch aplicado). O patch e a fonte da
verdade para **regenerar** os zips quando o core/upstream mudar.

**Quando aplicar (workflow de update):** 1) atualizar o submodule para o novo
HEAD; 2) aplicar 0001; 3) regenerar os temas; 4) rebuildar o `scummvm.zip`
(bundle de datafiles) e **commitar o novo zip**; 5) reverter o submodule.

## 0002-msvc-libretro-build.patch — build do core com MSVC — OPCIONAL

So necessario se for compilar `scummvm_libretro.dll` do source com MSVC em
vez de usar o binario buildbot (que e o que estamos usando). Upstream ja tem
o target `windows_msvc2017` no Makefile; o patch fecha as lacunas MSVC:

- `include/libretro-fs.h`: `#ifdef _MSC_VER` → `#include <io.h>` +
  `#define access _access` (+ `F_OK` fallback); sem `<unistd.h>` no MSVC.
- `Makefile`: bloco `windows_msvc2017` ganha `CXXFLAGS += -std:c++17`
  (ScummVM 2026 exige C++17; o default do cl.exe e C++14).
- `Makefile` / `Makefile.common`: flags de warning gcc-only
  (`-Wno-reorder`, `-Wno-long-long`, ...) ficam sob guard `ifneq ($(NO_GCC),1)`.

**Atencao:** este patch foi **reconstruido em 2026-08-08** — o diff original
se perdeu (revert do submodule antes de capturar). Conteudo = features
documentadas na epoca, mas **verificar antes de usar** (apply --check ja
valida, mas nao garante compilacao completa — o build do core requer cygwin/
msys + make, feito manualmente pelo usuario).
