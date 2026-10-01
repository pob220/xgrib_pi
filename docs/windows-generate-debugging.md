# Debugging Generate on Windows

Keep ecCodes: the generator uses it to read and write GRIB, including GRIB2.
Windows CI installs it using vcpkg and packages its runtime with the separate
helper. A hand-written `eccodes.pc` is unnecessary.

Ordinary OpenCPN for Windows is an x86 host even on 64-bit Windows. Its xGRIB
DLL is x86, while `environmental-grib.exe` is x64. Use an x64 DLL only with the
separate OpenCPN x64 host.

## Build the released plugin with symbols and reuse its helper

This lets you debug the dialog without rebuilding ecCodes or the generator.
Install Visual Studio 2022 C++ tools, CMake and vcpkg. Use the release
wxWidgets 3.2.8 **x86** headers and MSVC libraries, matching Windows x86 CI.
Set `XGRIB_WX_ROOT` to the wxWidgets installation and `XGRIB_WX_LIB` to its
`lib\vc14x_dll` directory. In PowerShell:

```powershell
git clone https://github.com/pob220/xgrib_pi.git
cd xgrib_pi
git switch --detach v0.3.4.0
git submodule update --init --recursive

$env:VCPKG_ROOT = 'C:\vcpkg'
$overlay = Join-Path $PWD 'ci\vcpkg-triplets'
& "$env:VCPKG_ROOT\vcpkg.exe" install bzip2 glew zlib `
  --triplet x86-windows-release "--overlay-triplets=$overlay"

# Substitute the actual installed/extracted 0.3.4 plugin root containing
# bin\environmental-grib.exe, its DLLs, and runtime\share\eccodes.
$helperRoot = 'C:\OpenCPN-x86-portable\plugins\xgrib_pi'
cmake -S . -B build-rick -G 'Visual Studio 17 2022' -A Win32 `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  "-DVCPKG_OVERLAY_TRIPLETS=$overlay" `
  -DVCPKG_TARGET_TRIPLET=x86-windows-release `
  "-DwxWidgets_ROOT_DIR=$env:XGRIB_WX_ROOT" `
  "-DwxWidgets_LIB_DIR=$env:XGRIB_WX_LIB" `
  -DwxWidgets_CONFIGURATION=mswu `
  "-DXGRIB_EXTERNAL_GENERATOR_DIR=$helperRoot" `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUNDLE_GENERATOR_RUNTIME=ON
cmake --build build-rick --config RelWithDebInfo --parallel 2
```

Check each command's exit status before continuing. Keep the resulting DLL
and matching PDB together, and attach Visual Studio to the x86 `opencpn.exe`.
This pins source to the 0.3.4 icon-fix release. With Visual Studio,
`--config RelWithDebInfo` selects the configuration; `CMAKE_BUILD_TYPE` alone
does not. Use a fresh build directory when changing toolchain or architecture.

To debug generation itself, attach to `environmental-grib.exe` using a helper
built with symbols from the pinned generator submodule. Its standalone CMake
build uses `find_package(eccodes CONFIG REQUIRED)` and the x64 vcpkg triplet.
The full dependency and runtime-staging procedure is in
`ci/circleci-build-windows.ps1`. Preserve ecCodes definitions and samples,
PROJ data, and all helper DLLs alongside the executable.

## Identify where Generate stalls

Collect the entire Generate log/details text: command, PID, last progress
line and exit status. Also collect `opencpn.log`, the installed xGRIB version,
providers, area, duration and step. Exclude credentials.

First run the installed helper directly:

```powershell
& "$helperRoot\bin\environmental-grib.exe" capabilities
```

Retain any exact loader/error text. Next test a small local-file merge with
waves and forecast extension disabled. A successful local merge followed by
a stalled provider request points to the download/provider path. The helper
appears in Task Manager's Details tab as `environmental-grib.exe`; the dialog
logs its PID.

The local fix branch adds timestamped construction checkpoints to
`opencpn.log` and a visible running/cancelling status with elapsed time. It
also corrects Windows process polling and console-helper termination. The
opening delay remains unconfirmed. A remembered offline `.xtd` file triggers
synchronous inspection during construction; the new checkpoint helps locate
a delay there when that file is configured.
