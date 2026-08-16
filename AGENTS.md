# ScummVM UWP — Regras do projeto

## Arquitetura (single-app, frontend libretro próprio)
- Um único appx: `ScummVMLauncher.exe` (C++/CoreWindow) É o frontend libretro
  (render D2D/D3D11, áudio XAudio2, input gamepad, VFS FromApp). Carrega o core
  oficial `cores\scummvm_libretro.dll` via `LoadLibrary` + `GetProcAddress` e
  roda `retro_load_game(NULL)` → GUI do ScummVM na tela. Sem RetroArch, sem
  segundo app, sem protocolos. Detalhes: `docs/ARCHITECTURE.md`, checklist
  vivo em `docs/IMPLEMENTATION-PLAN.md`.
- Manifest declara `runFullTrust` + `broadFileSystemAccess` (essenciais): o
  fopen/StdioStream do core funciona em processo; a VFS FromApp
  (`vfs_implementation_uwp.cpp`) é o caminho preferencial do core e a rede de
  segurança.
- Core ScummVM usa RGB565 em modo software → converter para BGRA8888 no caminho
  de frame. Sample rate do core define o formato do XAudio2.
- Quit: core chama `RETRO_ENVIRONMENT_SHUTDOWN` → `CoreApplication::Exit()`.

## RetroArch coexistente (NUNCA interferir)
- Usuário tem RetroArch REAL instalado no Xbox (package
  `1e4cf179-f3c2-404f-b9f3-cb2070a5aad8`, v1.22.2.0).
- ScummVM deve COEXISTIR com ele. NÃO desinstalar, atualizar, substituir ou
  tocar em qualquer instalação existente de RetroArch.
- Nosso app NÃO registra protocolo algum (nem `scummvm-core:`, nem
  `retroarch:`) — não existe colisão.
- LocalState é scoped por package (148433a7 vs 1e4cf179) — nunca escrever no do
  RetroArch.
- `extern/retroarch` submodule é legado do shell antigo — remover na Fase 3
  (não é referência de código).

## Deploy Xbox (WDP) — manual via Device Portal
- `scripts/deploy-xbox.ps1` foi REMOVIDO (não funcionava). Deploy é manual pela
  Device Portal do Xbox. Qualquer script novo de deploy NUNCA pode conter
  lógica de uninstall de RetroArch existente.
- Endpoint: HTTPS `https://<ip>:11443` (porta 11443, NÃO 10343).
- Auth: `Authorization: Basic base64(user:pass)`.
- CSRF: primeiro GET `/api/os/info` devolve `Set-Cookie: CSRF-Token=...`; todo
  POST/DELETE precisa header `X-CSRF-Token: <token>` + `-b <cookiejar>`.
- Lista pacotes: `GET /api/app/packagemanager/packages` → campo é
  `InstalledPackages` (NÃO `Packages`).
- Instalar appx: `POST /api/app/packagemanager/package?package=<filename>` com
  multipart `-F "package=@file"`. Retorna 202.
- Uninstall: `DELETE /api/app/packagemanager/package?package=<PackageFullName>`.
- Launch app: `POST /api/taskmanager/app?appid=<PRAID base64>&package=<FullName
  base64>` + CSRF. PRAID = PackageRelativeId (ex:
  `..._atgxky5qxrpe0!App`).
- Deps: appx instala direto mesmo sem VCLibs/NET.Native explícitos no console.

## Build
- MSBuild: VS2022 Community (`C:\Program Files\Microsoft Visual Studio\18\Community`)
  via vswhere.
- `build.ps1` roda `/t:Restore` antes — apagar obj/ sem restore quebra com
  WMC1006.
- Appx sai em `launcher\ScummVMLauncher\AppPackages\ScummVMLauncher_<ver>_x64_Test\`.
- Assinar: signtool + `certs\dosbox-uwp.pfx` (senha `dev`).
