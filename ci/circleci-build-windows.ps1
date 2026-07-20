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

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repo
$build = Join-Path $repo "build"
$stage = Join-Path $repo "stage"
$artifact = Join-Path $repo "artifacts\windows-x86"
$logDir = Join-Path $artifact "logs"
$testDir = Join-Path $artifact "tests"
$packageDir = Join-Path $artifact "package"
$fixtureDir = Join-Path $artifact "fixtures"
New-Item -ItemType Directory -Force `
    $build,$stage,$logDir,$testDir,$packageDir,$fixtureDir | Out-Null
Copy-Item (Join-Path $repo "test\fixtures\*") $fixtureDir

git submodule update --init --recursive
Assert-NativeSuccess "git submodule update"

$triplet = "x86-windows-release"
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

$vcpkgPackages = Get-Content (Join-Path $repo "ci\windows-vcpkg-deps.txt") |
    Where-Object { $_.Trim() -and -not $_.Trim().StartsWith("#") } |
    ForEach-Object { $_.Trim() }
$env:VCPKG_DEFAULT_BINARY_CACHE = Join-Path $repo "cache\vcpkg-binary"
New-Item -ItemType Directory -Force $env:VCPKG_DEFAULT_BINARY_CACHE | Out-Null
$overlayPorts = Join-Path $repo "ci\vcpkg-overlay-ports"
$overlayTriplets = Join-Path $repo "ci\vcpkg-triplets"
$vcpkgExit = 1
$retryDelays = @(30,45,60)
for ($attempt = 1; $attempt -le 4; $attempt++) {
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        & (Join-Path $vcpkg "vcpkg.exe") install --triplet $triplet `
            --allow-unsupported `
            "--overlay-ports=$overlayPorts" `
            "--overlay-triplets=$overlayTriplets" @vcpkgPackages 2>&1 | Tee-Object -FilePath `
            (Join-Path $logDir "vcpkg-attempt-$attempt.log")
        $vcpkgExit = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedPreference
    }
    if ($vcpkgExit -eq 0) { break }
    if ($attempt -lt 4) {
        $delay = $retryDelays[$attempt - 1]
        Write-Warning "vcpkg attempt $attempt failed; retrying in $delay seconds"
        Start-Sleep -Seconds $delay
    }
}
$vcpkgAttemptLogs = Get-ChildItem $logDir -Filter "vcpkg-attempt-*.log" |
    Sort-Object Name | ForEach-Object { $_.FullName }
Get-Content -Path $vcpkgAttemptLogs | Set-Content -Encoding utf8 `
    (Join-Path $logDir "vcpkg.log")
if ($vcpkgExit -ne 0) {
    throw "vcpkg dependency installation failed after 4 attempts"
}
$vcpkgList = & (Join-Path $vcpkg "vcpkg.exe") list
Assert-NativeSuccess "vcpkg version inventory"
$vcpkgList | Set-Content -Encoding utf8 (Join-Path $logDir "dependencies.log")
function Get-VcpkgVersion([string] $package) {
    $line = $vcpkgList | Where-Object { $_ -like "${package}:$triplet *" } |
        Select-Object -First 1
    if (-not $line) { throw "Installed vcpkg package not found: $package" }
    return $line
}

if ($env:XGRIB_WINDOWS_DEPS_ONLY -eq "1") {
    Write-Host "Release-only vcpkg dependency cache prepared successfully"
    exit 0
}

Invoke-NativeLogged `
    { choco install cmake pkgconfiglite nsis -y --no-progress } `
    (Join-Path $logDir "chocolatey-build-tools.log") `
    "Chocolatey build-tool installation"
Invoke-NativeLogged `
    { choco install gettext --version 1.0.0.20260310 -y --no-progress } `
    (Join-Path $logDir "chocolatey-gettext.log") `
    "Chocolatey gettext installation"
$machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$env:PATH = "$machinePath;$userPath;$env:PATH"
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $env:PATH = "C:\Program Files\CMake\bin;$env:PATH"
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake installation did not provide cmake.exe"
}
if (-not (Get-Command msgfmt -ErrorAction SilentlyContinue) -or
    -not (Get-Command msgmerge -ErrorAction SilentlyContinue)) {
    throw "GNU gettext installation did not provide msgfmt.exe and msgmerge.exe"
}

$installed = Join-Path $vcpkg "installed\$triplet"
$env:PKG_CONFIG_PATH = "$(Join-Path $installed 'lib\pkgconfig');$(Join-Path $installed 'share\pkgconfig')"
$env:PATH = "$(Join-Path $installed 'bin');$env:PATH"

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

$toolchain = Join-Path $vcpkg "scripts\buildsystems\vcpkg.cmake"
$wxLib = Join-Path $wxRoot "lib\vc14x_dll"

Invoke-NativeLogged {
    cmake -S $repo -B $build -G "Visual Studio 17 2022" -A Win32 `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
        "-DVCPKG_TARGET_TRIPLET=$triplet" `
        "-DVCPKG_OVERLAY_TRIPLETS=$overlayTriplets" `
        "-DwxWidgets_ROOT_DIR=$wxRoot" `
        "-DwxWidgets_LIB_DIR=$wxLib" `
        -DCMAKE_BUILD_TYPE=Release `
        -DBUNDLE_GENERATOR_RUNTIME=ON
} (Join-Path $logDir "configure.log") "CMake configure"
Invoke-NativeLogged `
    { cmake --build $build --config Release --parallel 2 } `
    (Join-Path $logDir "build.log") "CMake build"
Invoke-NativeLogged {
    ctest --test-dir $build -C Release --output-on-failure `
        --output-junit (Join-Path $testDir "ctest.xml")
} (Join-Path $logDir "test.log") "CTest"

$merge = Join-Path $build "generator\Release\environmental-grib.exe"
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
$packagedHelper = Join-Path $stage "plugins\xgrib_pi\bin\environmental-grib.exe"
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
"$checksum  $($archive.Name)" | Set-Content (Join-Path $packageDir "checksums.txt")

$result = [ordered]@{
    schema = "xgrib-target-result-v1"
    target = "windows-x86"
    xgrib_repository_commit = (git rev-parse HEAD)
    xgrib_version = "0.1.0.1"
    opencpn_version = "not installed by build job"
    operating_system = "Windows Server 2022"
    operating_system_version = [Environment]::OSVersion.VersionString
    architecture = "x86"
    compiler = "Visual Studio 2022 MSVC"
    cmake_version = (cmake --version | Select-Object -First 1)
    wxwidgets_version = $wxVersion
    grib_library_versions = @{
        eccodes = (Get-VcpkgVersion "eccodes")
        netcdf = (Get-VcpkgVersion "netcdf-c")
        blosc = (Get-VcpkgVersion "blosc")
        proj = (Get-VcpkgVersion "proj")
    }
    build_status = "passed"; test_status = "passed"; package_status = "passed"
    metadata_validation_status = "passed"; installation_status = "not-run"
    plugin_discovery_status = "not-run"; plugin_load_status = "not-run"
    graphical_test_status = "not-run"; file_path_display_status = "contract-tested"
    merge_status = "passed"; output_validation_status = "passed"
    screenshot_paths = @(); log_paths = @(
        "logs/chocolatey-build-tools.log", "logs/chocolatey-gettext.log",
        "logs/vcpkg.log", "logs/dependencies.log",
        "logs/configure.log", "logs/build.log", "logs/test.log",
        "logs/install.log", "logs/package.log", "tests/ctest.xml",
        "tests/checksums.txt")
    package_filename = $archive.Name; package_checksum_sha256 = $checksum
    elapsed_time_seconds = $null
    result_classification = "build-and-package-only"
    blocker_or_failure_details = "Genuine Windows build and functional tests ran; OpenCPN GUI runtime was not installed by this build job"
}
$result | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 (Join-Path $artifact "result.json")
$environment = [ordered]@{
    target = $result.target
    operating_system = $result.operating_system
    operating_system_version = $result.operating_system_version
    architecture = $result.architecture
    compiler = $result.compiler
    cmake_version = $result.cmake_version
    wxwidgets_version = $result.wxwidgets_version
    grib_library_versions = $result.grib_library_versions
}
$environment | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 `
    (Join-Path $artifact "environment.json")
