# ScummVM UWP — Plan: E:\ staging para RetroArch real (fluxo com fallback)

Status: **implementação**. Author: Marcelo Frau. Last updated: 2026-08-11.

Documento de execução do fluxo novo: detectar RetroArch real instalado no Xbox e
lançá-lo via `retroarch:` com **nosso** `retroarch.cfg` e **nosso** system dir
(ScummVM) stageado em `E:\scummvm`. Se algo falhar, cair no fallback bundled
(comportamento da última release, intocado).

---

## 1. Contexto / por quê

- Hoje (HEAD `c351f5a`) o appx empacota RetroArch inteiro (bundled) +
  `scummvm_libretro.dll` + `scummvm.zip` + `retroarch.cfg`. O launcher XAML
  dispara o protocolo próprio `scummvm-core:` e o RetroArch bundled sobe com
  nosso cfg/core/ini.
- O usuário também tem **RetroArch REAL** instalado (package
  `1e4cf179-...`, v1.22.2.0). Regra do projeto: coexistir, nunca tocar nele.
- RetroArch real lê `E:\` (storage compartilhado entre apps). Nosso LocalState
  é scoped por package — o RetroArch real **não** lê nosso LocalState.
- Solução: stagear o system do ScummVM (conteúdo do `scummvm.zip`) em
  `E:\scummvm\system`, escrever nosso `retroarch.cfg` em `E:\scummvm\`, e lançar
  o RetroArch real via `retroarch:` passando `-c E:\scummvm\retroarch.cfg`.
  O core usado é a **própria DLL do RetroArch real** (parâmetro `-L` com nome
  simples, sem caminho — RA resolve nos próprios dirs).

## 2. Estrutura alvo em E:\

```
E:\scummvm\
  retroarch.cfg           → cópia exata do bundled (3433 linhas) +
                            libretro_system_directory = "E:\scummvm\system"
                            (linha ADICIONADA — não existe no cfg bundled hoje)
  system\
    scummvm\              → scummvm.zip extraído (zip já tem prefixo scummvm/)
      extra\
      theme\
      COPYING, COPYRIGHT, ...
    scummvm.ini           → [scummvm] gui_theme=scummremastered gui_scale=150
    .scummvm-ready        → flag contendo a VERSÃO do app que fez o staging
```

Nota: espelha o setup real do usuário em
`E:\_emulators\_bios\retroarch\system` (`system\scummvm\` + `scummvm.ini` na raiz
de system).

## 3. Fluxo do Run() (novo)

```
Run():
  [preamble inalterado]
  Bootstrap()                       → LocalState\system + ini + flag   (INALTERADO)
  SeedRetroArchConfig()             → LocalState\retroarch.cfg         (INALTERADO)

  raReal  = IsRealRetroArchInstalled()
  eDrive  = FromAppFile.DriveExists('E')

  if raReal AND eDrive:
      SetProgressPanel(true)
      staged = StageToE(currentVersion)
      SetProgressPanel(false)
      if staged:
          uri = "retroarch:?cmd=retroarch -v -L scummvm_libretro.dll" +
                " -c E:\scummvm\retroarch.cfg" +
                "&launchOnExit=scummvm-launcher:?cmd=exit"
          ok = await LaunchWithRetry(uri, 4, useBundled: false)
          if ok: Log + Application.Current.Exit(); return
      Log("caiu no fallback bundled")

  [fallback bundled INALTERADO — byte-identical à última release]
  scummvm-core URI + LaunchWithRetry(4, useBundled: true)
```

Critério de queda para fallback: sem RA real, sem drive E:, staging falhou, ou
launch do RA real falhou.

## 4. StageToE(version) — detalhamento

```
private static bool StageToE(string version):
  if !FromAppFile.DriveExists('E'): Log; return false
  eSys    = @"E:\scummvm\system"
  flag    = eSys + @"\.scummvm-ready"

  # versão atual == flag → já stageado, pronto
  if FromAppFile.Exists(flag) AND FromAppFile.ReadAllText(flag) == version:
      Log("E:\ staging ok (versão igual); skip"); return true

  # versão diferente / sem flag → re-stage limpo
  Log("E:\ staging: versão difere; re-stage")
  DeleteTreeRecursive(eSys)                     # limpa obsoletos
  FromAppFile.CreateDirectory(eSys)

  # extrai scummvm.zip → E:\scummvm\system (entry-por-entry, write stream FromApp)
  zipPath = Package\system\scummvm.zip          # System.IO lê do package (ok)
  using ZipArchive (System.IO.Compression):
      foreach entry:
          se dir:  FromAppFile.CreateDirectory(eSys + "\\" + entry.FullName)
          senão:
              dest = eSys + "\\" + entry.FullName
              FromAppFile.CreateDirectory(dirname(dest))
              FromAppFile.WriteStream(dest, entry.Open())   # CopyTo

  # scummvm.ini
  FromAppFile.WriteAllText(eSys + @"\scummvm.ini",
      "[scummvm]\ngui_theme=scummremastered\ngui_scale=150\n")

  # retroarch.cfg = cópia bundled + libretro_system_directory
  WriteCfgWithSystemDir(@"E:\scummvm\retroarch.cfg",
                         Package\retroarch.cfg, @"E:\scummvm\system")
      # read bundled via System.IO; se linha libretro_system_directory já existe
      # → substitui; senão append; grava via FromAppFile.WriteAllText

  FromAppFile.WriteAllText(flag, version)
  return true
```

Qualquer exceção → Log com win32 error → return false → fallback bundled.

## 5. FromAppFile.cs — reescrita (padrão x-files)

Referência: `F:\workspace\x-files-uwp\XFiles\FileSystem\`
(`Win32FileStream.cs`, `Win32FileWriteStream.cs`, `FileOperations.cs`).

**Regra dura:** `System.IO.FileStream` NÃO funciona no sandbox UWP/Xbox (é o que
x-files evita). Nada de `new FileStream(handle)`. Usar raw handle.

Métodos P/Invoke (DLL `api-ms-win-core-file-fromapp-l1-1-0.dll`):

| Método | API | Uso |
|---|---|---|
| `CreateFileFromAppW` | open/create | abrir handle p/ read/write em E:\ |
| `ReadFile` (kernel32) | read | stream de leitura (paridade c/ x-files) |
| `WriteFile` (kernel32) | write | stream de escrita c/ loop de escrita parcial |
| `CreateDirectoryFromAppW` | mkdir | criar dirs recursivamente |
| `GetFileAttributesExFromAppW` | stat | Exists / IsDirectory |
| `GetLogicalDrives` (kernel32) | drives | DriveExists('E') |
| `DeleteFileFromAppW` | delete | deletar arquivo |
| `RemoveDirectoryFromAppW` | rmdir | remover dir |
| `FindFirstFileExFromAppW` + `FindNextFileW` + `FindClose` | enumerate | delete recursivo |

Métodos públicos:
- `bool DriveExists(char)`
- `bool Exists(string)` / `bool IsDirectory(string)`
- `void CreateDirectory(string)` (recursivo)
- `Stream OpenRead(string)` / `Stream OpenWrite(string)` (raw, sem FileStream)
- `void WriteAllText(string, string)`
- `void WriteFromStream(string dest, Stream src)` (entry zip → E:\)
- `string ReadAllText(string)` (p/ ler flag)
- `void Delete(string)` / `void DeleteTree(string)` (recursivo)

Exceções carregam win32 error; caller (MainPage) faz catch + Log.

## 6. Arquivos a alterar

| Arquivo | Mudança |
|---|---|
| `launcher/ScummVMLauncher/FromAppFile.cs` | REESCREVER — raw handle (sem FileStream) + helpers dir/delete/enumerate |
| `launcher/ScummVMLauncher/ScummVMLauncher.csproj` | `<Compile Include="FromAppFile.cs" />` |
| `launcher/ScummVMLauncher/MainPage.xaml` | adicionar `ProgressPanel` (ProgressBar + ProgressText), Collapsed default |
| `launcher/ScummVMLauncher/MainPage.xaml.cs` | campos E:\; `SetProgress`/`SetProgressPanel`; `StageToE(version)`; `Run()` ramo real RA novo |

## 7. UI de progresso

- `ProgressPanel` (StackPanel): `ProgressBar` (Min 0, Max 100) + `ProgressText`.
  Invisível por padrão.
- `SetProgress(double fraction, string message)` e `SetProgressPanel(bool)`:
  instance methods (estilo `SetStatus`), marshal via `Dispatcher` porque
  `Bootstrap()`/`StageToE()` rodam em `Task.Run` (threadpool).
- Mostrar durante: extração zip → staging E:\ → cfg.

## 8. Build e verificação

1. `build.ps1` (roda `/t:Restore` antes; apagar obj/ sem restore quebra com WMC1006)
2. Appx sai em `launcher\ScummVMLauncher\AppPackages\ScummVMLauncher_<ver>_x64_Test\`
3. Assinar: signtool + `certs\dosbox-uwp.pfx` (senha `dev`)
4. Deploy: manual via Device Portal do Xbox (`https://<ip>:11443`, CSRF flow —
   ver AGENTS.md). NUNCA tocar no RetroArch real.
5. Teste manual no Xbox:
   - Caso A (real RA + E:): app sobe → staging em E:\scummvm → RetroArch real
     abre com cfg nosso + system E:\scummvm → ScummVM roda. `retroarch.cfg` e
     `system/scummvm.ini` presentes em E:\.
   - Caso B (2ª execução): flag `.scummvm-ready` == versão → sem re-stage.
   - Caso C (bump de versão): re-stage limpo (delete-first).
   - Caso D (sem E:\ / staging falha / launch falha): fallback bundled igual à
     última release.

## 9. Riscos / decisões

1. `libretro_system_directory = "E:\scummvm\system"` — path literal com
   backslash. Se RA não aceitar, inverter barra (`E:/scummvm/system`).
2. `-L scummvm_libretro.dll` (bare) — RA real resolve nos próprios dirs. Se não
   resolver, reavaliar caminho.
3. WriteFile de 79 MB (zip extraído ~200MB descomprimido) — stream 64 KB buffer,
   loop de escrita parcial (padrão x-files).
4. Staging síncrono dentro de Run() — bloqueia UI thread até 1-2 min na 1ª vez;
   mitigado pelo progress bar. Aceitável (Bootstrap já bloqueia).
5. `.scummvm-ready` no system → RA pode reescrever dir? Risco baixo; flag é
   arquivo próprio.

## 10. Decisões log

- **2026-08-11**: fluxo novo com fallback aprovado. RetroArch real usa a PRÓPRIA
  DLL (bare name). E:\scummvm = shared storage (RA real lê E:\, nós escrevemos via
  FromApp). `retroarch.cfg` em E:\ = cópia exata do bundled + ADD
  `libretro_system_directory`. Re-stage gated por versão do app (delete-first em
  versão nova). FromAppFile.cs reescrito no padrão x-files (raw handle, sem
  `System.IO.FileStream`).
