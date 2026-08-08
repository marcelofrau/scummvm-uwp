$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot

Write-Host "=== Repo status ===" -ForegroundColor Cyan
git -C $root status --short
Write-Host "`n=== Branch ===" -ForegroundColor Cyan
git -C $root branch --show-current
Write-Host "`n=== Last commits ===" -ForegroundColor Cyan
git -C $root log --oneline -5

Write-Host "`n=== Submodules ===" -ForegroundColor Cyan
git -C $root submodule status

Write-Host "`n=== version.txt ===" -ForegroundColor Cyan
Get-Content (Join-Path $root 'version.txt')

$dist = Join-Path $root 'dist\ScummVM.appx'
if (Test-Path $dist) {
    $f = Get-Item $dist
    Write-Host "`n=== dist\ScummVM.appx ===" -ForegroundColor Cyan
    Write-Host "  $($f.Length / 1MB) MB, modified $($f.LastWriteTime)"
    $sig = Get-AuthenticodeSignature $dist
    Write-Host "  Signature: $($sig.Status)"
}

$appPackages = Join-Path $root 'launcher\ScummVMLauncher\AppPackages'
if (Test-Path $appPackages) {
    Write-Host "`n=== AppPackages ===" -ForegroundColor Cyan
    Get-ChildItem $appPackages -Directory | ForEach-Object {
        $appx = Get-ChildItem $_.FullName -Filter '*.appx' | Where-Object { $_.Name -notmatch 'Dependencies' }
        Write-Host "  $($_.Name) -> $($appx.Count) appx"
    }
}
