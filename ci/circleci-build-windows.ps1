$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-NativeSuccess([string] $operation) {
    if ($LASTEXITCODE -ne 0) {
        throw "$operation failed with exit code $LASTEXITCODE"
    }
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repo
$build = Join-Path $repo "build"
$stage = Join-Path $repo "stage"
$artifact = Join-Path $repo "artifacts\windows-x86_64"
$logDir = Join-Path $artifact "logs"
$testDir = Join-Path $artifact "tests"
$packageDir = Join-Path $artifact "package"
$fixtureDir = Join-Path $artifact "fixtures"
New-Item -ItemType Directory -Force `
    $build,$stage,$logDir,$testDir,$packageDir,$fixtureDir | Out-Null
Copy-Item (Join-Path $repo "test\fixtures\*") $fixtureDir

git submodule update --init --recursive
Assert-NativeSuccess "git submodule update"

$triplet = "x64-windows"
$vcpkg = $env:VCPKG_ROOT
if (-not $vcpkg) { $vcpkg = "C:\vcpkg" }
if (-not (Test-Path (Join-Path $vcpkg "vcpkg.exe"))) {
    throw "The CircleCI Windows image does not provide vcpkg at $vcpkg"
}

choco install pkgconfiglite nsis -y --no-progress 2>&1 | Tee-Object -FilePath `
    (Join-Path $logDir "chocolatey.log")
Assert-NativeSuccess "Chocolatey dependency installation"
& (Join-Path $vcpkg "vcpkg.exe") install --triplet $triplet `
    bzip2 blosc curl eccodes jsoncpp libzip libsodium netcdf-c proj qhull zstd `
    2>&1 | Tee-Object -FilePath (Join-Path $logDir "vcpkg.log")
Assert-NativeSuccess "vcpkg dependency installation"
$vcpkgList = & (Join-Path $vcpkg "vcpkg.exe") list
Assert-NativeSuccess "vcpkg version inventory"
$vcpkgList | Set-Content -Encoding utf8 (Join-Path $logDir "dependencies.log")
function Get-VcpkgVersion([string] $package) {
    $line = $vcpkgList | Where-Object { $_ -like "${package}:$triplet *" } |
        Select-Object -First 1
    if (-not $line) { throw "Installed vcpkg package not found: $package" }
    return $line
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
    "wxMSW-$($wxVersion)_vc14x_x64_Dev.7z" = "abf0be396b7648405883058090e4ad5f4d13918e"
    "wxMSW-$($wxVersion)_vc14x_x64_ReleaseDLL.7z" = "2ad8fcc7bf28db8126eae55284eccbdfa7d02e27"
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
$wxLib = Join-Path $wxRoot "lib\vc14x_x64_dll"

cmake -S $repo -B $build -G "Visual Studio 17 2022" -A x64 `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    "-DVCPKG_TARGET_TRIPLET=$triplet" `
    "-DwxWidgets_ROOT_DIR=$wxRoot" `
    "-DwxWidgets_LIB_DIR=$wxLib" `
    -DCMAKE_BUILD_TYPE=Release `
    -DBUNDLE_GENERATOR_RUNTIME=ON `
    2>&1 | Tee-Object -FilePath (Join-Path $logDir "configure.log")
Assert-NativeSuccess "CMake configure"
cmake --build $build --config Release --parallel 2 2>&1 | Tee-Object -FilePath `
    (Join-Path $logDir "build.log")
Assert-NativeSuccess "CMake build"
ctest --test-dir $build -C Release --output-on-failure `
    --output-junit (Join-Path $testDir "ctest.xml") `
    2>&1 | Tee-Object -FilePath (Join-Path $logDir "test.log")
Assert-NativeSuccess "CTest"

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

cmake --install $build --config Release --prefix $stage 2>&1 | Tee-Object `
    -FilePath (Join-Path $logDir "install.log")
Assert-NativeSuccess "Staged installation"
$packagedHelper = Join-Path $stage "plugins\xgrib_pi\bin\environmental-grib.exe"
& $packagedHelper capabilities | Set-Content -Encoding utf8 `
    (Join-Path $testDir "packaged-helper-capabilities.json")
Assert-NativeSuccess "Packaged helper execution"

Push-Location $build
cpack -G TGZ -C Release --config CPackConfig.cmake 2>&1 | Tee-Object `
    -FilePath (Join-Path $logDir "package.log")
Assert-NativeSuccess "CPack TGZ package"
Pop-Location
$archive = Get-ChildItem $build -Filter "xgrib_pi-*.tar.gz" | Select-Object -First 1
$metadata = Get-ChildItem $build -Filter "xgrib_pi-*.xml" | Select-Object -First 1
if (-not $archive -or -not $metadata) { throw "Windows package or metadata missing" }
if (-not (Select-String -Quiet -Path $metadata.FullName -Pattern "<target>msvc")) {
    throw "Windows metadata target is invalid"
}
Copy-Item $archive.FullName,$metadata.FullName $packageDir
$packagedArchive = Join-Path $packageDir $archive.Name
$checksum = (Get-FileHash -Algorithm SHA256 $packagedArchive).Hash.ToLowerInvariant()
"$checksum  $($archive.Name)" | Set-Content (Join-Path $packageDir "checksums.txt")

$result = [ordered]@{
    schema = "xgrib-target-result-v1"
    target = "windows-x86_64"
    xgrib_repository_commit = (git rev-parse HEAD)
    xgrib_version = "0.1.0.1"
    opencpn_version = "not installed by build job"
    operating_system = "Windows Server 2022"
    operating_system_version = [Environment]::OSVersion.VersionString
    architecture = "x86_64"
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
        "logs/chocolatey.log", "logs/vcpkg.log", "logs/dependencies.log",
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
