# release.ps1 - assertive release flow.
#
# Guarantees: git tag v<version> == committed version files == built/signed appx version.
# The app version auto-increments on every build (PreBuildEvent -> tools/version.ps1),
# so the version is DETERMINED BY THE BUILD, not predicted before it. Therefore:
#
#   1. Preflight  - working tree must be clean (all source committed).
#   2. Build      - package.ps1 builds + signs + zips. The build bumps the version to N.
#   3. Read N     - read version.txt (the exact version this build produced).
#   4. Verify     - the appx <N> exists and is signed (package.ps1 already verified it).
#   5. Commit     - commit the version bump (version.txt, build_counter.txt, manifest) as "release: vN".
#   6. Tag        - annotated tag vN on that commit (tag == committed version == appx version).
#   7. Push       - push the commit and the tag.
#
# The tag vN references a commit whose recorded version exactly matches the signed appx N,
# so a checkout of the tag reproduces the released version's manifest.

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [ValidateSet('x64')]
    [string]$Platform = 'x64',
    [switch]$NoPush
)
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot

Push-Location $root
try {
    # 1. Preflight: every source change must be committed before releasing.
    $status = git status --porcelain
    if ($LASTEXITCODE -ne 0) { Write-Error 'git status failed.' }
    if ($status) {
        Write-Host 'Working tree is not clean. Commit or stash before releasing:' -ForegroundColor Yellow
        $status | ForEach-Object { Write-Host "  $_" }
        exit 1
    }

    # 2. Build + sign + zip. The build bumps the version to N.
    & "$PSScriptRoot\package.ps1" -Configuration $Configuration -Platform $Platform
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    # 3. Read the exact version this build produced.
    $version = (Get-Content (Join-Path $root 'version.txt')).Trim()
    if ($version -notmatch '^\d+\.\d+\.\d+\.\d+$') {
        Write-Error "Invalid version read from version.txt: '$version'"; exit 1
    }

    # 4. Verify the signed appx for this version exists.
    $appx = Join-Path $root "launcher\ScummVMLauncher\AppPackages\ScummVMLauncher_${version}_x64_Test\ScummVMLauncher_${version}_x64.appx"
    if (-not (Test-Path $appx)) {
        Write-Error "Appx not found for version $version : $appx"; exit 1
    }
    Write-Host "Release build: $version" -ForegroundColor Cyan

    $tag = "v$version"

    # 5. The tag must not exist yet.
    $existing = git tag -l $tag
    if ($LASTEXITCODE -ne 0) { Write-Error 'git tag -l failed.' }
    if ($existing) {
        Write-Error "Tag $tag already exists. This version is already released."; exit 1
    }

    # 6. Commit the version bump.
    git add version.txt build_counter.txt launcher/ScummVMLauncher/Package.appxmanifest
    if ($LASTEXITCODE -ne 0) { Write-Error 'git add failed.' }
    $diff = git diff --cached --stat
    if (-not $diff) {
        Write-Error "No version files changed by the build. Aborting (would create an empty release commit)."; exit 1
    }
    git commit -m "release: v$version"
    if ($LASTEXITCODE -ne 0) { Write-Error 'git commit failed.' }

    # 7. Tag + push.
    git tag -a $tag -m "Release $version"
    if ($LASTEXITCODE -ne 0) { Write-Error 'git tag failed.' }

    if (-not $NoPush) {
        git push
        if ($LASTEXITCODE -ne 0) { Write-Error 'git push failed.' }
        git push origin $tag
        if ($LASTEXITCODE -ne 0) { Write-Error 'git push tag failed.' }
    }

    Write-Host ''
    Write-Host "Release $tag ready." -ForegroundColor Green
    Write-Host "  appx : $appx"
    Write-Host "  zip  : $(Join-Path $root "scummvm-uwp_${version}_x64.zip")"
    Write-Host "  tag  : $tag (local$(if ($NoPush) { '' } else { ' + pushed to origin' }))"
    Write-Host '  Note: the next build will auto-increment to the next version.' -ForegroundColor DarkGray
}
finally {
    Pop-Location
}
