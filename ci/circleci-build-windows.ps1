$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-NativeSuccess([string] $operation) {
    if ($LASTEXITCODE -ne 0) {
        throw "$operation failed with exit code $LASTEXITCODE"
    }
}

function Invoke-NativeLogged(
    [scriptblock] $command,
    [string] $logPath,
    [string] $operation
) {
    # Windows PowerShell converts native stderr into ErrorRecord objects.  With
    # ErrorActionPreference=Stop this can abort at the first diagnostic line,
    # before the native process exits and before its useful error is logged.
    $savedPreference = $ErrorActionPreference
    $nativeExit = 1
    try {
        $ErrorActionPreference = "Continue"
        & $command 2>&1 | Tee-Object -FilePath $logPath
        $nativeExit = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedPreference
    }
    if ($nativeExit -ne 0) {
        throw "$operation failed with exit code $nativeExit"
    }
}

function Assert-PowerShellSyntax([string] $path) {
    $parseTokens = $null
    $parseErrors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        $path, [ref]$parseTokens, [ref]$parseErrors)
    if ($parseErrors.Count -ne 0) {
        $details = ($parseErrors | ForEach-Object { $_.Message }) -join "; "
        throw "PowerShell syntax check failed for $path`: $details"
    }
}

function Find-Dumpbin {
    $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $programFilesX86 = ${env:ProgramFiles(x86)}
    $vswhere = Join-Path $programFilesX86 `
        "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "Cannot locate vswhere.exe or dumpbin.exe on this Windows runner"
    }

    $installation = (& $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath | Select-Object -First 1)
    if (-not $installation) {
        throw "No Visual Studio installation with the C++ toolchain was found"
    }

    $msvcRoot = Join-Path $installation "VC\Tools\MSVC"
    $toolsets = @(Get-ChildItem $msvcRoot -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending)
    foreach ($toolset in $toolsets) {
        foreach ($relative in @(
            "bin\Hostx64\x64\dumpbin.exe",
            "bin\Hostx64\x86\dumpbin.exe",
            "bin\Hostx86\x86\dumpbin.exe"
        )) {
            $candidate = Join-Path $toolset.FullName $relative
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    throw "Visual Studio was found, but its dumpbin.exe could not be located"
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repo
$build = Join-Path $repo "build"
$generatorBuild = Join-Path $repo "build-generator-x64"
$generatorStage = Join-Path $repo "stage-generator-x64"
$stage = Join-Path $repo "stage"
$artifact = Join-Path $repo "artifacts\windows-x86"
$logDir = Join-Path $artifact "logs"
$testDir = Join-Path $artifact "tests"
$packageDir = Join-Path $artifact "package"
$fixtureDir = Join-Path $artifact "fixtures"
$diagnosticDir = Join-Path $artifact "diagnostics"
Assert-PowerShellSyntax (Join-Path $repo "ci\test-windows-opencpn-runtime.ps1")
$dumpbin = Find-Dumpbin
New-Item -ItemType Directory -Force `
    $build,$generatorBuild,$generatorStage,$stage,$logDir,$testDir,$packageDir,$fixtureDir,$diagnosticDir | Out-Null
Copy-Item (Join-Path $repo "test\fixtures\*") $fixtureDir

git submodule update --init --recursive
Assert-NativeSuccess "git submodule update"

$generatorTriplet = "x64-windows-release"
$pluginTriplet = "x86-windows-release"
$vcpkg = $env:VCPKG_ROOT
if (-not $vcpkg) { $vcpkg = "C:\vcpkg" }
if (-not (Test-Path (Join-Path $vcpkg "vcpkg.exe"))) {
    $vcpkgVersion = "2026.06.24"
    $vcpkg = Join-Path $repo "cache\vcpkg-$vcpkgVersion"
    if (-not (Test-Path (Join-Path $vcpkg ".git"))) {
        git clone --branch $vcpkgVersion --depth 1 `
            https://github.com/microsoft/vcpkg.git $vcpkg
        Assert-NativeSuccess "Pinned vcpkg checkout"
    }
    & (Join-Path $vcpkg "bootstrap-vcpkg.bat") -disableMetrics
    Assert-NativeSuccess "vcpkg bootstrap"
}
if (-not (Test-Path (Join-Path $vcpkg "vcpkg.exe"))) {
    throw "vcpkg bootstrap did not create vcpkg.exe"
}

$generatorPackages = Get-Content (Join-Path $repo "ci\windows-vcpkg-deps.txt") |
    Where-Object { $_.Trim() -and -not $_.Trim().StartsWith("#") } |
    ForEach-Object { $_.Trim() }
$pluginPackages = @("bzip2", "glew", "zlib")
$env:VCPKG_DEFAULT_BINARY_CACHE = Join-Path $repo "cache\vcpkg-binary"
New-Item -ItemType Directory -Force $env:VCPKG_DEFAULT_BINARY_CACHE | Out-Null
$overlayPorts = Join-Path $repo "ci\vcpkg-overlay-ports"
$overlayTriplets = Join-Path $repo "ci\vcpkg-triplets"

function Save-VcpkgBuildtreeDiagnostics([string] $triplet, [string] $label) {
    $diagnostic = Join-Path $logDir "vcpkg-$label-buildtree-errors.log"
    $files = Get-ChildItem (Join-Path $vcpkg "buildtrees") -Recurse -File |
        Where-Object {
            $_.FullName -like "*$triplet*" -and
            $_.Name -match "(out|CMakeCache|CMakeConfigureLog).*\.log$"
        } | Sort-Object FullName
    $lines = foreach ($file in $files) {
        "===== $($file.FullName) ====="
        Get-Content $file.FullName
    }
    $lines | Set-Content -Encoding utf8 $diagnostic
    ($lines | Select-Object -Last 500) | ForEach-Object { Write-Host $_ }
}

function Install-VcpkgPackages(
    [string] $triplet,
    [string[]] $packages,
    [string] $label
) {
    $retryDelays = @(30,45,60)
    for ($attempt = 1; $attempt -le 4; $attempt++) {
        $attemptLog = Join-Path $logDir "vcpkg-$label-attempt-$attempt.log"
        $savedPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = "Continue"
            & (Join-Path $vcpkg "vcpkg.exe") install --triplet $triplet `
                "--overlay-ports=$overlayPorts" `
                "--overlay-triplets=$overlayTriplets" @packages 2>&1 |
                Tee-Object -FilePath $attemptLog
            $vcpkgExit = $LASTEXITCODE
        }
        finally {
            $ErrorActionPreference = $savedPreference
        }
        if ($vcpkgExit -eq 0) { return }

        $attemptText = Get-Content $attemptLog -Raw
        $transient = $attemptText -match `
            "(?i)(HTTP status code (408|429|5[0-9][0-9])|failed to download|timed out|connection reset|could not resolve host|rate limit)"
        if (-not $transient -or $attempt -eq 4) {
            Save-VcpkgBuildtreeDiagnostics $triplet $label
            throw "vcpkg $label dependency installation failed"
        }
        $delay = $retryDelays[$attempt - 1]
        Write-Warning "vcpkg $label attempt $attempt had a transient failure; retrying in $delay seconds"
        Start-Sleep -Seconds $delay
    }
}

# OpenCPN's supported MSVC plugin ABI is x86, while ecCodes explicitly
# supports only 64-bit platforms. The generator is an isolated process, so
# build it and its dependencies for x64 and keep the in-process plugin x86.
Install-VcpkgPackages $generatorTriplet $generatorPackages "generator-x64"
Install-VcpkgPackages $pluginTriplet $pluginPackages "plugin-x86"
$vcpkgAttemptLogs = Get-ChildItem $logDir -Filter "vcpkg-*-attempt-*.log" |
    Sort-Object Name | ForEach-Object { $_.FullName }
Get-Content -Path $vcpkgAttemptLogs | Set-Content -Encoding utf8 `
    (Join-Path $logDir "vcpkg.log")
$vcpkgList = & (Join-Path $vcpkg "vcpkg.exe") list
Assert-NativeSuccess "vcpkg version inventory"
$vcpkgList | Set-Content -Encoding utf8 (Join-Path $logDir "dependencies.log")
function Get-VcpkgVersion([string] $package, [string] $triplet) {
    $line = $vcpkgList | Where-Object { $_ -like "${package}:$triplet *" } |
        Select-Object -First 1
    if (-not $line) { throw "Installed vcpkg package not found: $package" }
    return $line
}

if ($env:XGRIB_WINDOWS_DEPS_ONLY -eq "1") {
    Write-Host "Release-only vcpkg dependency cache prepared successfully"
    exit 0
}

$diagnosticsOnly = $env:XGRIB_WINDOWS_DIAGNOSTICS_ONLY -eq "1"
if ($diagnosticsOnly) {
    Invoke-NativeLogged `
        { choco install cmake -y --no-progress } `
        (Join-Path $logDir "chocolatey-build-tools.log") `
        "Chocolatey build-tool installation"
} else {
    Invoke-NativeLogged `
        { choco install cmake pkgconfiglite nsis -y --no-progress } `
        (Join-Path $logDir "chocolatey-build-tools.log") `
        "Chocolatey build-tool installation"
}
if (-not $diagnosticsOnly) {
    Invoke-NativeLogged `
        { choco install gettext --version 1.0.0.20260310 -y --no-progress } `
        (Join-Path $logDir "chocolatey-gettext.log") `
        "Chocolatey gettext installation"
}
$machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$env:PATH = "$machinePath;$userPath;$env:PATH"
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $env:PATH = "C:\Program Files\CMake\bin;$env:PATH"
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake installation did not provide cmake.exe"
}
if (-not $diagnosticsOnly -and
    (-not (Get-Command msgfmt -ErrorAction SilentlyContinue) -or
    -not (Get-Command msgmerge -ErrorAction SilentlyContinue))) {
    throw "GNU gettext installation did not provide msgfmt.exe and msgmerge.exe"
}

$generatorInstalled = Join-Path $vcpkg "installed\$generatorTriplet"
$pluginInstalled = Join-Path $vcpkg "installed\$pluginTriplet"
$env:PATH = "$(Join-Path $generatorInstalled 'bin');$(Join-Path $pluginInstalled 'bin');$env:PATH"
$env:PROJ_DATA = Join-Path $generatorInstalled "share\proj"
$env:ECCODES_DEFINITION_PATH = Join-Path $generatorInstalled "share\eccodes\definitions"
$env:ECCODES_SAMPLES_PATH = Join-Path $generatorInstalled "share\eccodes\samples"

$toolchain = Join-Path $vcpkg "scripts\buildsystems\vcpkg.cmake"
$generatorDiagnosticCmakeArgs = @()
if ($diagnosticsOnly) {
    $generatorDiagnosticCmakeArgs = @(
        "-DCMAKE_CXX_FLAGS_RELEASE=/Zi /O2 /Ob2 /DNDEBUG",
        "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/DEBUG /INCREMENTAL:NO"
    )
}

Invoke-NativeLogged {
    cmake -S (Join-Path $repo "generator") -B $generatorBuild `
        -G "Visual Studio 17 2022" -A x64 `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
        "-DVCPKG_TARGET_TRIPLET=$generatorTriplet" `
        "-DVCPKG_OVERLAY_TRIPLETS=$overlayTriplets" `
        -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON `
        @generatorDiagnosticCmakeArgs
} (Join-Path $logDir "configure-generator-x64.log") `
    "x64 generator CMake configure"
Invoke-NativeLogged `
    { cmake --build $generatorBuild --config Release --parallel 2 } `
    (Join-Path $logDir "build-generator-x64.log") "x64 generator build"
if ($diagnosticsOnly) {
    & (Join-Path $repo "ci\diagnose-windows-generator.ps1") `
        -Repository $repo -GeneratorBuild $generatorBuild `
        -GeneratorInstalled $generatorInstalled -ArtifactDirectory $artifact
    if ($LASTEXITCODE -ne 0) {
        throw "Windows generator CDB diagnostics failed with exit code $LASTEXITCODE"
    }
    exit 0
}
Invoke-NativeLogged {
    ctest --test-dir $generatorBuild -C Release --output-on-failure `
        --output-junit (Join-Path $testDir "generator-x64-ctest.xml")
} (Join-Path $logDir "test-generator-x64.log") "x64 generator CTest"
Invoke-NativeLogged `
    { cmake --install $generatorBuild --config Release --prefix $generatorStage } `
    (Join-Path $logDir "install-generator-x64.log") `
    "x64 generator staged installation"

$wxVersion = "3.2.8"
$wxRoot = Join-Path $repo "cache\wxWidgets-$wxVersion"
New-Item -ItemType Directory -Force $wxRoot | Out-Null
$wxBase = "https://github.com/wxWidgets/wxWidgets/releases/download/v$wxVersion"
$downloads = [ordered]@{
    "wxWidgets-$wxVersion-headers.7z" = "86a2c99b4e9608b7cfc0b59e0f5a6d200a9d2541"
    "wxMSW-$($wxVersion)_vc14x_Dev.7z" = "65112a99d3e253796081d1ec80df294290403398"
    "wxMSW-$($wxVersion)_vc14x_ReleaseDLL.7z" = "44ceee6ddcbb6aa60de6b6fc26c57491c189477f"
}
foreach ($entry in $downloads.GetEnumerator()) {
    $archive = $entry.Key
    $path = Join-Path $env:TEMP $archive
    if (-not (Test-Path $path)) {
        Invoke-WebRequest "$wxBase/$archive" -OutFile $path
    }
    $actualHash = (Get-FileHash -Algorithm SHA1 $path).Hash.ToLowerInvariant()
    if ($actualHash -ne $entry.Value) { throw "Checksum mismatch for $archive" }
    & 7z x -y "-o$wxRoot" $path | Out-Null
    Assert-NativeSuccess "Extracting $archive"
}
$wxLib = Join-Path $wxRoot "lib\vc14x_dll"
$wxRuntimeDlls = @(Get-ChildItem $wxLib -Filter "wxbase32u_*.dll" -File)
if ($wxRuntimeDlls.Count -eq 0) {
    throw "The wxWidgets release archive did not provide runtime DLLs"
}
# CTest launches the standalone x86 test executables before OpenCPN is
# installed. Make their wxWidgets runtime explicit so a missing-DLL loader
# dialog cannot silently hold the non-interactive CI job open.
$env:PATH = "$wxLib;$env:PATH"

$generatorBin = Join-Path $generatorStage "bin"
Copy-Item (Join-Path $generatorInstalled "bin\*.dll") $generatorBin
$generatorRuntime = Join-Path $generatorStage "runtime"
$runtimeEccodes = Join-Path $generatorRuntime "share\eccodes"
$runtimeShare = Join-Path $generatorRuntime "share"
$runtimeLicenses = Join-Path $generatorRuntime "licenses"
New-Item -ItemType Directory -Force `
    $runtimeEccodes,$runtimeShare,$runtimeLicenses | Out-Null
Copy-Item (Join-Path $generatorInstalled "share\eccodes\definitions") `
    $runtimeEccodes -Recurse
Copy-Item (Join-Path $generatorInstalled "share\eccodes\samples") `
    $runtimeEccodes -Recurse
Copy-Item (Join-Path $generatorInstalled "share\proj") `
    $runtimeShare -Recurse
foreach ($package in $generatorPackages) {
    $copyright = Join-Path $generatorInstalled "share\$package\copyright"
    if (Test-Path $copyright) {
        Copy-Item $copyright (Join-Path $runtimeLicenses "$package.txt")
    }
}

Invoke-NativeLogged {
    cmake -S $repo -B $build -G "Visual Studio 17 2022" -A Win32 `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
        "-DVCPKG_TARGET_TRIPLET=$pluginTriplet" `
        "-DVCPKG_OVERLAY_TRIPLETS=$overlayTriplets" `
        "-DwxWidgets_ROOT_DIR=$wxRoot" `
        "-DwxWidgets_LIB_DIR=$wxLib" `
        "-DXGRIB_EXTERNAL_GENERATOR_DIR=$generatorStage" `
        -DCMAKE_BUILD_TYPE=Release `
        -DBUNDLE_GENERATOR_RUNTIME=ON
} (Join-Path $logDir "configure.log") "CMake configure"
Invoke-NativeLogged `
    { cmake --build $build --config Release --parallel 2 } `
    (Join-Path $logDir "build.log") "CMake build"
$firstPluginTest = Join-Path $build "Release\xgrib_generator_job_json_tests.exe"
& $dumpbin /dependents $firstPluginTest 2>&1 | Set-Content -Encoding utf8 `
    (Join-Path $logDir "plugin-test-dependencies.log")
Assert-NativeSuccess "Inspecting standalone plugin-test dependencies"
$env:ECCODES_DEFINITION_PATH = Join-Path $runtimeEccodes "definitions"
$env:ECCODES_SAMPLES_PATH = Join-Path $runtimeEccodes "samples"
$env:PROJ_DATA = Join-Path $runtimeShare "proj"
Invoke-NativeLogged {
    ctest --test-dir $build -C Release --output-on-failure `
        --timeout 120 `
        --output-junit (Join-Path $testDir "ctest.xml")
} (Join-Path $logDir "test.log") "CTest"

$merge = Join-Path $generatorBin "environmental-grib.exe"
$reader = Join-Path $build "Release\xgrib_reader_integration_tests.exe"
$combined = Join-Path $testDir "combined-windows.grb2"
& $merge merge-environment-gribs `
    --weather (Join-Path $repo "test\fixtures\wind-known.grb2") `
    --current (Join-Path $repo "test\fixtures\current-differing.grb") `
    --output $combined --overwrite | Set-Content -Encoding utf8 `
    (Join-Path $testDir "merge-result.json")
Assert-NativeSuccess "Deterministic merge"
& $reader $combined --combined | Set-Content -Encoding utf8 `
    (Join-Path $testDir "xgrib-reader-reopen.log")
Assert-NativeSuccess "xGRIB reader reopen"
& $merge inspect-grib $combined | Set-Content -Encoding utf8 `
    (Join-Path $testDir "combined-inspection.json")
Assert-NativeSuccess "Combined GRIB inspection"
$mergeResult = Get-Content (Join-Path $testDir "merge-result.json") -Raw | ConvertFrom-Json
if (-not $mergeResult.success -or $mergeResult.output_message_count -ne 10) {
    throw "Deterministic Windows merge validation failed"
}
$checksumFiles = @(
    "fixtures/wind-known.grb2",
    "fixtures/current-matching.grb",
    "fixtures/current-differing.grb",
    "fixtures/current-incompatible-area.grb",
    "fixtures/current-incompatible-time.grb",
    "fixtures/corrupt.grb",
    "fixtures/fixture-manifest.json",
    "tests/combined-windows.grb2"
)
$testChecksums = foreach ($relative in $checksumFiles) {
    $nativeRelative = $relative.Replace("/", "\")
    $hash = (Get-FileHash -Algorithm SHA256 `
        (Join-Path $artifact $nativeRelative)).Hash.ToLowerInvariant()
    "$hash  $relative"
}
$testChecksums | Set-Content -Encoding ascii `
    (Join-Path $testDir "checksums.txt")

Invoke-NativeLogged `
    { cmake --install $build --config Release --prefix $stage } `
    (Join-Path $logDir "install.log") "Staged installation"
$pluginBinary = Join-Path $stage "plugins\xgrib_pi.dll"
$packagedHelper = Join-Path $stage "plugins\xgrib_pi\bin\environmental-grib.exe"
$pluginHeaders = & $dumpbin /headers $pluginBinary 2>&1
Assert-NativeSuccess "Inspecting xGRIB plugin architecture"
$pluginHeaders | Set-Content -Encoding utf8 `
    (Join-Path $logDir "plugin-headers.log")
$helperHeaders = & $dumpbin /headers $packagedHelper 2>&1
Assert-NativeSuccess "Inspecting environmental helper architecture"
$helperHeaders | Set-Content -Encoding utf8 `
    (Join-Path $logDir "helper-headers.log")
if (-not ($pluginHeaders -match "14C machine \(x86\)")) {
    throw "Packaged xGRIB plugin is not an x86 PE binary"
}
if (-not ($helperHeaders -match "8664 machine \(x64\)")) {
    throw "Packaged environmental helper is not an x64 PE binary"
}
& $dumpbin /dependents $pluginBinary 2>&1 | Set-Content -Encoding utf8 `
    (Join-Path $logDir "plugin-dependencies.log")
Assert-NativeSuccess "Inspecting xGRIB plugin dependencies"
& $dumpbin /dependents $packagedHelper 2>&1 | Set-Content -Encoding utf8 `
    (Join-Path $logDir "helper-dependencies.log")
Assert-NativeSuccess "Inspecting environmental helper dependencies"
& $packagedHelper capabilities | Set-Content -Encoding utf8 `
    (Join-Path $testDir "packaged-helper-capabilities.json")
Assert-NativeSuccess "Packaged helper execution"

Push-Location $build
Invoke-NativeLogged `
    { cpack -G TGZ -C Release --config CPackConfig.cmake } `
    (Join-Path $logDir "package.log") "CPack TGZ package"
Pop-Location
$archive = Get-ChildItem $build -Filter "xgrib_pi-*.tar.gz" | Select-Object -First 1
$metadata = Get-ChildItem $build -Filter "xgrib_pi-*.xml" | Select-Object -First 1
if (-not $archive -or -not $metadata) { throw "Windows package or metadata missing" }
if (-not (Select-String -Quiet -Path $metadata.FullName -Pattern "<target>msvc")) {
    throw "Windows metadata target is invalid"
}
if (-not (Select-String -Quiet -Path $metadata.FullName `
        -SimpleMatch "<source> https://github.com/pob220/xgrib_pi </source>")) {
    throw "Windows metadata source repository is invalid"
}
Copy-Item $archive.FullName,$metadata.FullName $packageDir
$packagedArchive = Join-Path $packageDir $archive.Name
$checksum = (Get-FileHash -Algorithm SHA256 $packagedArchive).Hash.ToLowerInvariant()
$packageChecksums = foreach ($file in @($archive, $metadata)) {
    $packagedFile = Join-Path $packageDir $file.Name
    $hash = (Get-FileHash -Algorithm SHA256 $packagedFile).Hash.ToLowerInvariant()
    "$hash  $($file.Name)"
}
$packageChecksums | Set-Content -Encoding ascii `
    (Join-Path $packageDir "checksums.txt")

Invoke-NativeLogged {
    powershell.exe -ExecutionPolicy Bypass -File `
        (Join-Path $repo "ci\test-windows-opencpn-runtime.ps1") `
        -Repository $repo -PackageArchive $packagedArchive `
        -ArtifactDirectory $artifact
} (Join-Path $logDir "opencpn-runtime-test.log") `
    "OpenCPN Windows runtime test"
$runtimeResult = Get-Content `
    (Join-Path $testDir "windows-opencpn-runtime.json") -Raw | ConvertFrom-Json

$result = [ordered]@{
    schema = "xgrib-target-result-v1"
    target = "windows-x86"
    xgrib_repository_commit = (git rev-parse HEAD)
    xgrib_version = "0.1.0.1"
    opencpn_version = $runtimeResult.opencpn_version
    operating_system = "Windows Server 2022"
    operating_system_version = [Environment]::OSVersion.VersionString
    architecture = "x86"
    helper_architecture = "x86_64"
    compiler = "Visual Studio 2022 MSVC"
    cmake_version = (cmake --version | Select-Object -First 1)
    wxwidgets_version = $wxVersion
    grib_library_versions = @{
        eccodes = (Get-VcpkgVersion "eccodes" $generatorTriplet)
        netcdf = (Get-VcpkgVersion "netcdf-c" $generatorTriplet)
        blosc = (Get-VcpkgVersion "blosc" $generatorTriplet)
        proj = (Get-VcpkgVersion "proj" $generatorTriplet)
    }
    build_status = "passed"; test_status = "passed"; package_status = "passed"
    metadata_validation_status = "passed"; installation_status = "passed"
    plugin_discovery_status = "passed"; plugin_load_status = "passed"
    graphical_test_status = "passed"; file_path_display_status = "passed"
    merge_status = "passed"; output_validation_status = "passed"
    screenshot_paths = $runtimeResult.screenshots; log_paths = @(
        "logs/chocolatey-build-tools.log", "logs/chocolatey-gettext.log",
        "logs/vcpkg.log", "logs/dependencies.log",
        "logs/configure-generator-x64.log",
        "logs/build-generator-x64.log", "logs/test-generator-x64.log",
        "logs/install-generator-x64.log",
        "logs/configure.log", "logs/build.log", "logs/test.log",
        "logs/install.log", "logs/plugin-headers.log",
        "logs/helper-headers.log", "logs/plugin-dependencies.log",
        "logs/helper-dependencies.log", "logs/package.log",
        "logs/opencpn-runtime-test.log", "logs/opencpn.log",
        "tests/ctest.xml", "tests/helper-launch.json",
        "tests/windows-opencpn-runtime.json",
        "tests/runtime-combined-inspection.json",
        "tests/generator-x64-ctest.xml",
        "tests/checksums.txt")
    package_filename = $archive.Name; package_checksum_sha256 = $checksum
    elapsed_time_seconds = $null
    result_classification = "fully-tested"
    blocker_or_failure_details = ""
}
$result | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 (Join-Path $artifact "result.json")
$environment = [ordered]@{
    target = $result.target
    operating_system = $result.operating_system
    operating_system_version = $result.operating_system_version
    architecture = $result.architecture
    helper_architecture = $result.helper_architecture
    compiler = $result.compiler
    cmake_version = $result.cmake_version
    wxwidgets_version = $result.wxwidgets_version
    grib_library_versions = $result.grib_library_versions
}
$environment | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 `
    (Join-Path $artifact "environment.json")
