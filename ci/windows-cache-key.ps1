$ErrorActionPreference = 'Stop'
# The current CircleCI image can change Visual Studio independently of this
# repository. Immutable caches must be keyed by the compiler, so a refreshed
# dependency cache can be saved and reused by all Windows package jobs.
. (Join-Path $PSScriptRoot 'windows-toolchain.ps1')
$installation = Get-XgribVisualStudio2022
$toolsetFile = Join-Path $installation.installationPath 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt'
$toolset = (Get-Content $toolsetFile -Raw).Trim()
if (-not $toolset) { throw 'Visual C++ toolset version is empty' }
New-Item -ItemType Directory -Force cache | Out-Null
$key = "VisualStudio=$($installation.installationVersion)`nMSVC=$toolset"
Set-Content -Path cache/windows-toolchain-id.txt -Value $key -Encoding ascii
Write-Output $key
