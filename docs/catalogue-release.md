# Catalogue release procedure

## Prerequisites

- CMake 3.20 or newer and a C++20 compiler for the helper.
- OpenCPN Frontend2 build prerequisites, wxWidgets 3.2, bzip2 and zlib for
  the viewer. Jasper 4.2.9 is fetched at a pinned commit and linked statically
  by default.
- Development packages for ecCodes, NetCDF, libcurl, jsoncpp, Qhull,
  bzip2, Blosc, libzip, PROJ, libsodium, and Zstandard.
- All git submodules initialized at recorded commits.

## Local verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
cmake --install build --prefix /tmp/xgrib-stage
scripts/test-packaged-helper.sh /tmp/xgrib-stage
cmake --build build --target package
scripts/test-catalogue-archive.sh build
```

Run `git diff --check` and verify the generated archive contains the plugin
library, launcher, native helper, runtime libraries, ecCodes definitions and
samples, PROJ data, toolbar assets, and metadata.

For a bounded GUI lifecycle/parser smoke test in an isolated OpenCPN profile,
set `XGRIB_TEST_OPEN_FILE=/path/to/test.grb` for one launch. xGRIB opens the
normal control bar and the supplied file through its production viewer path.
Set `XGRIB_TEST_OPEN_GENERATOR=1` to open the integrated generator dialog as
well. These variables are developer test hooks and are ignored when unset.

## Catalogue package

Frontend2's `package`/`tarball` and `cloudsmith-upload.sh` flow produces an
OpenCPN Plugin Manager archive with embedded `metadata.xml`. Build release
artifacts in the project CI's oldest supported Linux image; do not publish an
Arch-built binary as a general Linux catalogue asset because its glibc ABI is
too new for older distributions.

Before publishing:

1. Tag both the generator and plugin repositories.
2. Update the generator gitlink and plugin version/date.
3. Run generator unit and differential parity tests.
4. Run packaged-helper tests from a clean staging directory.
5. Install through an isolated stock OpenCPN profile.
6. With bundled GRIB disabled, generate a synthetic GRIB and open it directly
   in xGRIB.
7. Test one public no-credential provider with a bounded request.
8. Verify cancellation, malformed jobs, missing credentials, unavailable
   provider data, and an unwritable output path all fail cleanly.
9. Verify xGRIB refuses activation while bundled GRIB is enabled.
10. Record third-party notices and source offers for bundled libraries.
11. Where source-model licensing permits distribution, test Offline Tidal
    against the separately supplied XTD package, verify its checksum, and
    confirm the package itself is not embedded in the plugin archive.

The release CI image must copy its package copyright/license records for all
libraries selected by `GET_RUNTIME_DEPENDENCIES` into the package's
`runtime/licenses` directory. The checked-in third-party notice is an index,
not a replacement for required license texts.

## Linux target policy

The oldest workable native build baseline is Debian 12 (Bookworm), not Debian
11. The generator requires CMake 3.20 and the C++20 calendar/chrono library
implemented by GCC 11 or newer; stock Bullseye has CMake 3.18 and GCC 10.
Building on an old image only lowers libc and library requirements. It does
not make one archive universal: OpenCPN still selects artifacts using target,
target version, architecture, wxWidgets and GTK ABI metadata.

Release candidates, in priority order, are:

| Artifact | OpenCPN hosts it can target |
| --- | --- |
| Debian 12 arm64 | 64-bit Raspberry Pi OS/Debian 12; OpenCPN also maps it to Ubuntu arm64 23.04, 23.10 and 24.04 |
| Debian 12 armhf | 32-bit Raspberry Pi OS/Debian 12; corresponding Ubuntu armhf mappings |
| Flatpak aarch64/x86_64 | OpenCPN Flatpak with the matching runtime/SDK branch |
| Debian 12 x86_64 | Debian 12; OpenCPN also maps it to Ubuntu x86_64 23.04, 23.10 and 24.04 |
| Ubuntu 22.04 x86_64 wx3.2 | Ubuntu 22.04 only |
| Debian 13 x86_64/arm64 | Debian 13, without pretending it is compatible with Debian 12 |

Only a row which completes its clean container build, all tests, staged-helper
test and archive test should be enabled for upload. A real Pi test is still
required before publishing ARM metadata. Windows x86_64 and native
Apple-Silicon macOS now have genuine hosted build, test and package jobs; they
are publishable only when retained manifests support the claimed validation.
CircleCI builds Bookworm x86_64/arm64, Jammy x86_64, Noble x86_64, Trixie
x86_64, Flatpak x86_64/aarch64, Windows x86_64 and macOS arm64.
`run_workflow_deploy` defaults to false and the separate deployment workflow
still stops at a manual approval gate. See `docs/alpha-validation.md`.

### Verification snapshot (20 July 2026)

The current tree completed clean builds, all ten CTest tests, staged-helper
execution and extracted-archive execution for these locally tested candidates:

| Candidate | Archive | ABI observation |
| --- | ---: | --- |
| Debian 12 x86_64 | 45 MiB | plugin and helper require at most GLIBC 2.35 |
| Ubuntu 22.04 x86_64 wx3.2 | 35 MiB | at most GLIBC 2.35; ecCodes 2.24 compatibility path |
| Ubuntu 24.04 x86_64 | 36 MiB | clean Noble container |
| Debian 13 x86_64 | 36 MiB | at most GLIBC 2.38 |
| Flatpak 25.08 x86_64 | 20 MiB | built against the stable OpenCPN Flatpak runtime |

The laptop has no registered ARM binfmt handler, so ARM64 is intentionally
left to CircleCI's native ARM executor rather than mislabelled emulation.
Flatpak aarch64 uses the same pinned manifest and has a native ARM CircleCI
job, but was not built on this x86_64 laptop. Debian armhf remains a later
candidate rather than an asserted supported target.

## Raspberry Pi release-candidate test

Build the ARM64 archive in CI or an emulated `debian:bookworm` container, then
create a temporary local catalogue (replace the address with the serving
machine's LAN address):

```sh
scripts/make-local-catalogue.sh build local-catalogue \
  http://192.168.1.10:8000
python3 -m http.server 8000 --directory local-catalogue
```

On the Pi, first record `dpkg --print-architecture`, the OS details from
`/etc/os-release`, and `opencpn --version`. In OpenCPN's plugin catalogue
settings, set the custom URL to
`http://192.168.1.10:8000/ocpn-plugins.xml`, update the catalogue and install
xGRIB. Then:

1. Confirm the stock GRIB plugin is disabled, enable xGRIB and restart.
2. Open the xGRIB control bar and a known GRIB file.
3. Generate a small synthetic file, open it, move the timeline and inspect
   cursor values/overlays.
4. Open the generator and run one small public-provider request.
5. Restart OpenCPN, repeat enable/disable once, and inspect the OpenCPN log for
   loader errors, missing shared libraries or helper failures.
6. Confirm uninstall removes xGRIB and does not alter the bundled GRIB plugin.

Do not add the ARM XML to the public catalogue until this test passes on the
actual Pi.
