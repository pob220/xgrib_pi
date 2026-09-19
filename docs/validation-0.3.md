# xGRIB 0.3 validation — 20 September 2026

## Changes

xGRIB now derives from OpenCPN plugin API 1.17 and reports the generated
major, minor, patch and post-release version components. OpenCPN can therefore
display the complete `0.3.0.0` release rather than truncating it to `0.3`.

Generator 0.1.11 uses the legacy ecCodes sample-constructor name in its
provider-grid regression fixture. That name is supported by Ubuntu 22.04's
ecCodes 2.24.2 and remains supported by current ecCodes releases. This fixes
the Ubuntu 22.04 CI compile failure without changing runtime GRIB handling.

The global and wrapped-longitude coverage fix from xGRIB 0.2.6 and generator
0.1.10 is unchanged.

## Results

- Clean native Release build completed and all 28 CTest tests passed.
- The plugin-version contract verifies API 1.17 inheritance and full generated
  version reporting.
- The exact Ubuntu 22.04 x86_64 CI container flow completed using ecCodes
  2.24.2: configure, compile, all 28 tests, functional merge, staged install,
  packaged-helper validation, package creation and catalogue-archive
  validation.
- The generated Ubuntu package was
  `xgrib_pi-0.3.0.0-ubuntu-x86_64-22.04-jammy.tar.gz`.

Hosted cross-platform CI must pass before publishing the alpha catalogue.
