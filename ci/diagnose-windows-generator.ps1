param(
    [Parameter(Mandatory = $true)] [string] $Repository,
    [Parameter(Mandatory = $true)] [string] $GeneratorBuild,
    [Parameter(Mandatory = $true)] [string] $GeneratorInstalled,
    [Parameter(Mandatory = $true)] [string] $ArtifactDirectory
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Find-Cdb {
    $candidates = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64\cdb.exe",
        "$env:ProgramFiles\Windows Kits\10\Debuggers\x64\cdb.exe"
    )
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            return (Resolve-Path $candidate).Path
        }
    }
    return $null
}

function Install-Cdb {
    $installer = Join-Path $env:TEMP "xgrib-winsdksetup.exe"
    $installerUrl = "https://go.microsoft.com/fwlink/?linkid=2361308"
    Invoke-WebRequest $installerUrl -OutFile $installer
    $signature = Get-AuthenticodeSignature $installer
    if ($signature.Status -ne "Valid" -or
        -not $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -notmatch "Microsoft") {
        throw "The downloaded Windows SDK installer does not have a valid Microsoft signature"
    }
    $process = Start-Process -FilePath $installer -Wait -PassThru `
        -ArgumentList "/features OptionId.WindowsDesktopDebuggers /quiet /norestart"
    if ($process.ExitCode -notin @(0, 3010)) {
        throw "Windows SDK debugger installation failed with exit code $($process.ExitCode)"
    }
}

function Get-ProcDump {
    $toolDirectory = Join-Path $env:TEMP "xgrib-procdump"
    $executable = Join-Path $toolDirectory "procdump64.exe"
    if (-not (Test-Path $executable)) {
        $archive = Join-Path $env:TEMP "xgrib-procdump.zip"
        Invoke-WebRequest "https://download.sysinternals.com/files/Procdump.zip" `
            -OutFile $archive
        New-Item -ItemType Directory -Force $toolDirectory | Out-Null
        Expand-Archive -Path $archive -DestinationPath $toolDirectory -Force
    }
    $signature = Get-AuthenticodeSignature $executable
    if ($signature.Status -ne "Valid" -or
        -not $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -notmatch "Microsoft") {
        throw "ProcDump does not have a valid Microsoft signature"
    }
    return (Resolve-Path $executable).Path
}

$Repository = (Resolve-Path $Repository).Path
$GeneratorBuild = (Resolve-Path $GeneratorBuild).Path
$GeneratorInstalled = (Resolve-Path $GeneratorInstalled).Path
$releaseDirectory = Join-Path $GeneratorBuild "Release"
$diagnosticDirectory = Join-Path $ArtifactDirectory "diagnostics"
$dumpDirectory = Join-Path $diagnosticDirectory "dumps"
$logDirectory = Join-Path $diagnosticDirectory "logs"
$symbolDirectory = Join-Path $diagnosticDirectory "symbols"
New-Item -ItemType Directory -Force `
    $diagnosticDirectory,$dumpDirectory,$logDirectory,$symbolDirectory |
    Out-Null

$env:PATH = "$(Join-Path $GeneratorInstalled 'bin');$env:PATH"
$env:ECCODES_DEFINITION_PATH =
    Join-Path $GeneratorInstalled "share\eccodes\definitions"
$env:ECCODES_SAMPLES_PATH =
    Join-Path $GeneratorInstalled "share\eccodes\samples"
$env:PROJ_DATA = Join-Path $GeneratorInstalled "share\proj"

$cdb = Find-Cdb
if (-not $cdb) {
    Install-Cdb
    $cdb = Find-Cdb
}
if (-not $cdb) {
    throw "cdb.exe was not found after installing Windows Desktop Debuggers"
}
$procdump = Get-ProcDump

$symbolIndex = 0
Get-ChildItem $GeneratorBuild -Recurse -Filter "*.pdb" -File |
    Sort-Object FullName | ForEach-Object {
        $symbolIndex++
        $destination = "{0:D3}-{1}" -f $symbolIndex, $_.Name
        Copy-Item $_.FullName (Join-Path $symbolDirectory $destination)
    }
if ($symbolIndex -eq 0) {
    throw "The diagnostic generator build did not produce any PDB files"
}

$tests = @(
    "environmental_grib_tests",
    "environmental_grib_xtd_tests"
)
$results = @()
$diagnosticFailure = $false
foreach ($test in $tests) {
    $executable = Join-Path $releaseDirectory "$test.exe"
    if (-not (Test-Path $executable)) {
        throw "Diagnostic executable is missing: $executable"
    }
    $dump = Join-Path $dumpDirectory "$test.dmp"
    $log = Join-Path $logDirectory "$test-cdb.log"
    $console = Join-Path $logDirectory "$test-procdump-console.log"
    $commands = Join-Path $logDirectory "$test-cdb-commands.txt"
    $symbolPath = $releaseDirectory.Replace("\", "/")
    Get-ChildItem $dumpDirectory -Filter "$test*.dmp" -File |
        Remove-Item -Force

    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        & $procdump -accepteula -ma -e -x $dumpDirectory $executable 2>&1 |
            Tee-Object -FilePath $console
        $procdumpExit = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedPreference
    }
    $generatedDump = Get-ChildItem $dumpDirectory -Filter "$test*.dmp" -File |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if ($generatedDump -and $generatedDump.FullName -ne $dump) {
        Move-Item $generatedDump.FullName $dump -Force
    }

    @(
        ".symfix"
        ".sympath+ $symbolPath"
        ".reload /f"
        "!analyze -v"
        ".ecxr"
        "kv 100"
        "lm"
        "q"
    ) | Set-Content -Encoding ascii $commands

    $debuggerExit = $null
    if (Test-Path $dump) {
        try {
            $ErrorActionPreference = "Continue"
            & $cdb -lines -z $dump -cf $commands -logo $log 2>&1 |
                Add-Content -Encoding utf8 $console
            $debuggerExit = $LASTEXITCODE
        }
        finally {
            $ErrorActionPreference = $savedPreference
        }
    }
    $dumpPresent = Test-Path $dump
    $dumpSize = if ($dumpPresent) { (Get-Item $dump).Length } else { 0 }
    $dumpHash = if ($dumpPresent) {
        (Get-FileHash -Algorithm SHA256 $dump).Hash.ToLowerInvariant()
    } else { $null }
    if (-not $dumpPresent -or $dumpSize -eq 0) {
        $diagnosticFailure = $true
    }
    $results += [ordered]@{
        test = $test
        procdump_exit_code = $procdumpExit
        debugger_exit_code = $debuggerExit
        dump_captured = $dumpPresent -and $dumpSize -gt 0
        dump_path = "diagnostics/dumps/$test.dmp"
        dump_size_bytes = $dumpSize
        dump_sha256 = $dumpHash
        stack_trace_path = "diagnostics/logs/$test-cdb.log"
        procdump_console_path = "diagnostics/logs/$test-procdump-console.log"
        debugger_commands_path = "diagnostics/logs/$test-cdb-commands.txt"
    }
}

$cdbVersion = (Get-Item $cdb).VersionInfo.FileVersion
$manifest = [ordered]@{
    schema = "xgrib-windows-generator-diagnostics-v1"
    repository_commit = (git -C $Repository rev-parse HEAD)
    debugger = $cdb
    debugger_version = $cdbVersion
    dump_capture = $procdump
    dump_capture_version = (Get-Item $procdump).VersionInfo.FileVersion
    build_configuration = "Release with MSVC PDBs"
    capture_policy = "ProcDump full dump on unhandled exception; CDB offline analysis"
    tests = $results
    pdb_count = $symbolIndex
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 `
    (Join-Path $diagnosticDirectory "diagnostic-result.json")

if ($diagnosticFailure) {
    throw "CDB did not capture a non-empty full dump for every failing executable"
}
Write-Host "CDB captured full dumps and stack traces for both failing executables"
