param(
    [string]$Package = '',
    [switch]$UninstallOnly,
    [switch]$SkipDeps,
    [switch]$NoLaunch
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (Test-Path (Join-Path $root '.env')) {
    Get-Content (Join-Path $root '.env') | ForEach-Object {
        $kv = $_ -split '=', 2
        if ($kv[0]) { Set-Item -Path "env:$($kv[0])" -Value $kv[1] }
    }
}

$ip = $env:XBOX_IP
$user = $env:XBOX_USER
$pass = $env:XBOX_PASS
if (-not $ip -or -not $user -or -not $pass) {
    Write-Error 'XBOX_IP/XBOX_USER/XBOX_PASS not set. Fix .env.'
    exit 1
}

$base64 = [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes("${user}:${pass}"))
$headers = @{ Authorization = "Basic $base64" }
$base = "https://${ip}:11443"

$version = (Get-Content (Join-Path $root 'version.txt')).Trim()
if (-not $Package) { $Package = Join-Path $root "launcher\ScummVMLauncher\AppPackages\ScummVMLauncher_${version}_x64_Test\ScummVMLauncher_${version}_x64.appx" }
if (-not (Test-Path $Package)) { Write-Error "Appx not found: $Package (run package.ps1 first)"; exit 1 }

Write-Host "=== Deploy to $ip ===" -ForegroundColor Cyan

try {
    $installed = Invoke-RestMethod -Uri "${base}/api/app/packagemanager/packages" -Method GET -Headers $headers -SkipCertificateCheck
    $installed.InstalledPackages | ForEach-Object { Write-Host "  installed: $($_.PackageFullName)" -ForegroundColor DarkGray }
} catch {
    Write-Error "Cannot reach Xbox WDP: $_"
    Write-Host 'Check: Dev Mode ON, Remote Access enabled, same network. Test: curl -k https://'$ip':11443/api/os/info'
    exit 1
}

$old = '1e4cf179-f3c2-404f-b9f3-cb2070a5aad8'
$oldPkg = $installed.InstalledPackages | Where-Object { $_.PackageFamilyName -match '^1e4cf179' -or $_.PackageFullName -match '^1e4cf179' } | Select-Object -First 1
if ($oldPkg) {
    Write-Host "NOTE: Existing RetroArch package found ($($oldPkg.PackageFullName)). Leaving it alone — ScummVM coexists with it." -ForegroundColor Yellow
} else {
    Write-Host "No pre-existing RetroArch package found." -ForegroundColor DarkGray
}

if ($UninstallOnly) { Write-Host "Done (uninstall only)." -ForegroundColor Green; exit 0 }

if (-not $SkipDeps) {
    $depsDir = Join-Path (Split-Path -Parent $Package) "Dependencies\x64"
    if (Test-Path $depsDir) {
        foreach ($dep in (Get-ChildItem $depsDir -Filter *.appx)) {
            Write-Host "Installing dependency: $($dep.Name) ..." -ForegroundColor Cyan
            try {
                Invoke-RestMethod -Uri "${base}/api/app/packagemanager/package?package=$($dep.Name)" -Method POST -Headers $headers `
                    -Form @{ package = Get-Item -LiteralPath $dep.FullName } -SkipCertificateCheck | Out-Null
            } catch { Write-Warning "Dependency $($dep.Name) failed: $_" }
            Start-Sleep -Milliseconds 500
        }
    }
}

Write-Host "Uploading/installing $([IO.Path]::GetFileName($Package)) ..." -ForegroundColor Cyan
try {
    Invoke-RestMethod -Uri "${base}/api/app/packagemanager/package?package=$([IO.Path]::GetFileName($Package))" -Method POST -Headers $headers `
        -Form @{ package = Get-Item -LiteralPath $Package } -SkipCertificateCheck | Out-Null
    Write-Host "Deploy OK" -ForegroundColor Green
} catch {
    Write-Error "Deploy failed: $_"
    exit 1
}

if ($NoLaunch) { exit 0 }

Write-Host "=== Launch ===" -ForegroundColor Cyan
try {
    Start-Sleep -Seconds 2
    $after = Invoke-RestMethod -Uri "${base}/api/app/packagemanager/packages" -Method GET -Headers $headers -SkipCertificateCheck
    $pkg = $after.InstalledPackages | Where-Object { $_.PackageFamilyName -like 'ScummVMLauncher*' } | Select-Object -First 1
    if (-not $pkg) {
        Write-Warning 'Package not found via API. Start manually: Dev Mode Home -> tile.'
        exit 0
    }
    $launchBody = @{ AppId = 'App'; PackageFamilyName = $pkg.PackageFamilyName } | ConvertTo-Json
    Invoke-RestMethod -Uri "${base}/api/taskmanager/app" -Method POST -Headers $headers `
        -Body $launchBody -ContentType 'application/json' -SkipCertificateCheck | Out-Null
    Write-Host "Launched: $($pkg.PackageFamilyName)" -ForegroundColor Green
} catch {
    Write-Warning "Launch failed: $_"
    Write-Host 'Manual: Xbox Dev Mode Home -> tile -> launch'
}
