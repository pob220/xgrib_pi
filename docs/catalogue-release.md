# Catalogue release procedure

## Prerequisites

- CMake 3.20 or newer and a C++20 compiler for the helper.
- OpenCPN Frontend2 build prerequisites, wxWidgets 3.2, Jasper, bzip2 and
  zlib for the viewer.
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

## Platform policy

Only platforms built and tested by CI should appear in catalogue metadata.
Linux x86_64 is the initial release target. Add Windows and macOS entries only
after their helper runtime bundles pass the same protocol and generation
tests.
