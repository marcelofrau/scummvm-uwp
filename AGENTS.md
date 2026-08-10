# ScummVM UWP — Regras do projeto

## RetroArch coexistente (NUNCA interferir)
- Usuário tem RetroArch REAL instalado no Xbox (package 1e4cf179-f3c2-404f-b9f3-cb2070a5aad8, v1.22.2.0).
- ScummVM deve COEXISTIR com ele. NÃO desinstalar, atualizar, substituir ou tocar em qualquer instalação existente de RetroArch.
- Nosso app usa protocolo próprio `scummvm-core:` — NÃO registrar/colidir com `retroarch:`.
- LocalState é scoped por package (148433a7 vs 1e4cf179) — nunca escrever no do RetroArch.

## Deploy Xbox (WDP) — manual via Device Portal
- `scripts/deploy-xbox.ps1` foi REMOVIDO (não funcionava). Deploy é manual pela Device Portal do Xbox. Qualquer script novo de deploy NUNCA pode conter lógica de uninstall de RetroArch existente.
- Endpoint: HTTPS `https://<ip>:11443` (porta 11443, NÃO 10343).
- Auth: `Authorization: Basic base64(user:pass)`.
- CSRF: primeiro GET `/api/os/info` devolve `Set-Cookie: CSRF-Token=...`; todo POST/DELETE precisa header `X-CSRF-Token: <token>` + `-b <cookiejar>`.
- Lista pacotes: `GET /api/app/packagemanager/packages` → campo é `InstalledPackages` (NÃO `Packages`).
- Instalar appx: `POST /api/app/packagemanager/package?package=<filename>` com multipart `-F "package=@file"`. Retorna 202.
- Uninstall: `DELETE /api/app/packagemanager/package?package=<PackageFullName>`.
- Launch app: `POST /api/taskmanager/app?appid=<PRAID base64>&package=<FullName base64>` + CSRF. PRAID = PackageRelativeId (ex: `..._atgxky5qxrpe0!App`).
- Deps: appx instala direto mesmo sem VCLibs/NET.Native explícitos no console.

## Build
- MSBuild: VS2022 Community (`C:\Program Files\Microsoft Visual Studio\18\Community`) via vswhere.
- `build.ps1` roda `/t:Restore` antes — apagar obj/ sem restore quebra com WMC1006.
- Appx sai em `launcher\ScummVMLauncher\AppPackages\ScummVMLauncher_<ver>_x64_Test\`.
- Assinar: signtool + `certs\dosbox-uwp.pfx` (senha `dev`).
