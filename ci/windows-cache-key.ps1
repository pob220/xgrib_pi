$ErrorActionPreference = 'Stop'
# The current CircleCI image can change Visual Studio independently of this
# repository. Immutable caches must be keyed by the compiler, so a refreshed
# dependency cache can be saved and reused by all Windows package jobs.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installations = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json)
if ($LASTEXITCODE -ne 0 -or $installations.Count -ne 1) {
    throw 'Cannot identify the Visual C++ installation for the dependency cache'
}
$installation = $installations[0]
$toolsetFile = Join-Path $installation.installationPath 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt'
$toolset = (Get-Content $toolsetFile -Raw).Trim()
if (-not $toolset) { throw 'Visual C++ toolset version is empty' }
New-Item -ItemType Directory -Force cache | Out-Null
$key = "VisualStudio=$($installation.installationVersion)`nMSVC=$toolset"
Set-Content -Path cache/windows-toolchain-id.txt -Value $key -Encoding ascii
Write-Output $key
