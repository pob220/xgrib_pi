param(
    [switch] $ChecksOnly
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-NativeSuccess([string] $operation) {
    if ($LASTEXITCODE -ne 0) {
        throw "$operation failed with exit code $LASTEXITCODE"
    }
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repo

Write-Host "==> PowerShell syntax checks"
Get-ChildItem (Join-Path $repo "ci") -File -Filter "*.ps1" | ForEach-Object {
    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        $_.FullName, [ref]$tokens, [ref]$errors)
    if ($errors.Count -ne 0) {
        $details = ($errors | ForEach-Object { $_.Message }) -join "; "
        throw "PowerShell syntax check failed for $($_.Name): $details"
    }
}

Write-Host "==> Repository and submodule checks"
git diff --check
Assert-NativeSuccess "git diff --check"
$submodules = @(git submodule status --recursive)
Assert-NativeSuccess "git submodule status"
$submodules | ForEach-Object { Write-Host $_ }
$badSubmodules = @($submodules | Where-Object { $_ -match "^[+\-U]" })
if ($badSubmodules.Count -ne 0) {
    throw "A submodule is missing, modified, or at the wrong recorded commit"
}
git submodule foreach --quiet --recursive `
    'test -z "$(git status --porcelain)"'
Assert-NativeSuccess "clean submodule worktrees"
if (-not (Select-String -Quiet -Path "po\POTFILES.in" `
        -SimpleMatch "src/XyGribPanel.cpp")) {
    throw "po/POTFILES.in does not contain the portable XyGribPanel path"
}
cmake --list-presets=all | Out-Null
Assert-NativeSuccess "CMake preset validation"

if ($ChecksOnly) {
    Write-Host "Windows preflight checks passed (build skipped by -ChecksOnly)."
    exit 0
}

Write-Host "==> Native Windows split-ABI build, tests and package"
& (Join-Path $repo "ci\circleci-build-windows.ps1")
if (-not $?) {
    throw "Native Windows validation script failed"
}
Write-Host "Windows preflight validation passed."
