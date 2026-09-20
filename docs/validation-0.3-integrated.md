# xGRIB 0.3 integrated validation — 20 September 2026

## Scope

xGRIB 0.3.0.0 unifies the Android tablet implementation and native Windows x64
Preview build with the global/wrapped-longitude correction and full four-part
plugin version reporting. Every packaged platform is built from the same
plugin revision and the same environmental generator 0.3.0 revision.

The Cloudsmith archive for each platform embeds the same substituted catalogue
XML as top-level `metadata.xml`. Consequently, one archive works both as the
catalogue download payload and with OpenCPN's manual **Import plugin...** action.
The packaging regression test rejects missing, duplicate and version-mismatched
metadata.

The shared generator therefore provides all of the following on applicable
platforms:

- correct global and wrapped regular-longitude coverage when merging weather,
  waves and regional currents, including the Tonga regression case;
- Android in-process generation, cooperative cancellation and isolated TLS;
- the correct NOAA GFS mean-sea-level pressure request;
- compatibility with the older ecCodes supplied by Ubuntu 22.04.

## Qualification boundary

The Android implementation was exercised on an Android 15 arm64 tablet with
OpenCPN 5.14, including touch navigation, rotation/keyboard behaviour, server
download, weather-only output, presets, `.xtd` currents and account handling.

Windows x64 compiles, runs its standalone tests, validates AMD64 package and
host-SDK architecture, and remains distinct from the ordinary Windows x86
catalogue ABI. It has not yet been loaded in an actual Windows x64 OpenCPN
installation and must be described as experimental until that runtime test is
completed. The Windows x86 package retains its separate OpenCPN runtime test.

Hosted results and exact package checksums will be recorded after the unified
0.3.0.0 CircleCI release matrix completes.
