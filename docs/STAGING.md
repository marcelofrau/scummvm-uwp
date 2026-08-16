# ScummVM UWP — Staging em LocalState (dados do ScummVM, C++)

Status: **implementação**. Author: Marcelo Frau. Última atualização: 2026-08-14.

O app novo (single-app, frontend libretro próprio) stageia os dados do ScummVM
**sempre em `LocalState\system`** — desktop e Xbox. Sem E:\ no caminho: o storage
interno do Series S/X é NVMe SSD (extração de ~80 MB em 1-2 s) e o app fica
100% contido no package (sem dependência de drive externo).

Substitui o antigo `PLAN-E-STAGING.md` (que stageava `retroarch.cfg` e lançava o
RetroArch real). `retroarch.cfg` é código morto — o core lê config via
`GET_VARIABLE` (respondido pela tabela do frontend), não de arquivo.

---

## 1. Estrutura alvo em LocalState

```
%LOCALAPPDATA%\Packages\ScummVMFrontend_<hash>\LocalState\
  system\
    scummvm\            → scummvm.zip extraído (zip já tem prefixo scummvm/)
      extra\
      theme\
      COPYING, COPYRIGHT, ...
    scummvm.ini         → [scummvm] gui_theme=scummremastered gui_scale=150
    .scummvm-ready      → flag contendo a VERSÃO do app que fez o staging
  saves\                → save_directory do core
```

## 2. Fluxo (C++)

```
OnLaunched:
  splash nativo (branding) segura enquanto roda Bootstrap em background thread
  Bootstrap::Run():
    systemDir = LocalState\system
    saveDir   = LocalState\saves
    create_directories(systemDir, saveDir)
    StageIfNeeded(systemDir, saveDir, appVersion, gateByVersion=true)
      # gated por .scummvm-ready (igual ao antigo, mas em LocalState)
      # re-stage limpo quando a versão do app muda (delete-first + rename)
    WriteIniIfAbsent(systemDir)        # scummvm.ini, nunca sobrescreve
  LoadCore()                           # LoadLibrary cores\scummvm_libretro.dll
  env GET_SYSTEM_DIRECTORY → systemDir
  env GET_SAVE_DIRECTORY → saveDir
  retro_load_game(NULL) → GUI do ScummVM
```

- Extração extrai entry-por-entry (`system\scummvm.zip` do package), com tmp dir
  + rename pra não deixar árvore parcial.
- `.scummvm-ready` guarda a versão do app que stageou; atualização → delete + re-extract.
- `scummvm.ini` só é escrito se ausente (preserva config/saves do usuário).

## 3. Acesso a arquivo (C++)

- Processo é **`runFullTrust`**: `fopen`/`_wfopen`/`CreateFile` funcionam direto
  em LocalState (desktop e Xbox). Caminho preferencial e simples.
- Rede de segurança: família **FromApp** (`CreateFile2FromAppW`,
  `GetFileAttributesExFromAppW`, `FindFirstFileExFromAppW`,
  `CreateDirectoryFromAppW`, `DeleteFileFromAppW`, `RemoveDirectoryFromAppW`)
  via `vfs_implementation_uwp.cpp` (libretro-common) — mesma implementação já
  usada para a VFS do core. Reaproveitar essas funções no bootstrap.

## 4. Continuidade de saves (E:\ legado)

O setup antigo (launcher C#) e o RetroArch real podem ter saves em E:\:

- `E:\scummvm\saves\` — staging antigo do launcher C#.
- `E:\retroarch\saves\scummvm\` — save dir configurado no RetroArch real.

O app NÃO lê E:\ no runtime. Para migrar saves existentes: copiar manualmente
pro `LocalState\saves\` (ou o core usa o browser de arquivos do save dir e salva
lá). A partir daí, tudo novo fica em LocalState.

## 5. Build e verificação

1. `scripts/build.ps1` (roda `/t:Restore`; obj/ sem restore quebra WMC1006).
2. Deploy manual via Device Portal do Xbox (`https://<ip>:11443`, CSRF). NUNCA
   tocar no RetroArch real.
3. Teste manual:
   - Caso A (1ª execução): extração pra `LocalState\system`, GUI abre.
   - Caso B (2ª execução): flag == versão → sem re-extract.
   - Caso C (bump de versão): re-stage limpo (delete-first).

## 6. Riscos / decisões

1. Staging síncrono em background thread bloqueia o start na 1ª vez (~1-2 s no
   NVMe, mais em HD) — aceitável; splash pode ser adicionado se preciso.
2. LocalState é scoped por package: desinstalar o app apaga os dados. E:\ era
   persistente entre re-installs — aceitou-se a perda em troca de simplicidade.
3. `scummvm.ini` nunca é sobrescrito (preserva saves/config do usuário).

## 7. Decisões log

- **2026-08-14**: staging reduzido a dados do ScummVM (sem retroarch.cfg, sem
  detecção/launch de RetroArch real). Rewrite em C++.
- **2026-08-14**: **E:\ removido do caminho** — staging sempre em LocalState
  (desktop e Xbox). Motivo: storage interno é NVMe (rápido), app 100% contido no
  package, sem dependência de drive externo. `retroarch.cfg` confirmado código
  morto (core lê GET_VARIABLE).
