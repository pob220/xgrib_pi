# Android xGRIB — native generator rebuild (0.2.5.6)

This development branch targets **OpenCPN 5.14, Android arm64**. It replaces
the limited Android-only NOAA assembler with the same C++ generator used by
desktop xGRIB, linked into the plugin. It does not execute a desktop helper
from writable Android storage. Desktop dialogue layouts are unchanged.

## Interface

- The chart panel has Open, Generate, Settings and Close actions, forecast
  selection, timeline, layer toggles and an embedded cursor readout.
- Tap the displayed forecast time to open a touch-sized selection list, then
  choose **Use selected time** or **Cancel**. Previous/next remain available.
- Generate opens a separate touch-sized form. The chart panel is hidden until
  the form closes, avoiding overlapping controls.
- Area/time, Weather/waves, Currents and Options are independently scrollable.
  Fields not relevant to the chosen providers are hidden. Size estimates update
  as inputs change; these are **not** total RAM requirements.
- Generation runs on a background worker. Cancel requests cooperative
  cancellation; an already-running operation must finish or time out first.
  The form prevents overlapping jobs and output-file overwrites.
- Output is stored in the plugin's `generated` directory, with an option to
  open the result on the chart. Passwords are not saved to configuration.
- Settings uses OpenCPN's Android GRIB settings activity, including its download
  workflow. Existing GRIBs and local generator inputs use Android file selection.

## Provider scope

The shared engine contains GFS, HRRR, UKV, Nordic, ICON-EU, ECMWF IFS/AIFS,
GFS waves, Copernicus waves/currents, Marine.ie, RTOFS/OFS, local GRIB/NetCDF,
TPXO and offline tidal-package processing. This is shared processing code,
**not a claim that every live provider has been validated on Android**.
Regional coverage, provider availability and account/licence requirements still
apply. Large areas and high resolution can exhaust a tablet's memory; this
release is not a hard memory-limiting mode.

## Verified on 19 September 2026

Device: Samsung SM-X210, Android 15, arm64 OpenCPN 5.14.0 development app.

| Check | Result |
| --- | --- |
| Full local Linux regression run | All 28 tests passed, including seven generator suites |
| Native Android cancellation/context test | Passed |
| Native Android size-estimate tests | Passed |
| Native Android engine tests | Zero failures |
| Native Android offline tidal-package tests | Zero failures |
| Native Android deterministic GRIB merge | Passed |
| Native Android concurrency tests | Zero failures |
| Live GFS weather + waves in OpenCPN | 63 messages generated, merged and opened; pressure verified with ecCodes |
| Chart rendering and forecast step | Wind rendered over Irish Sea; time advanced |
| Touch readout | Wind, pressure, waves and temperature values displayed |
| Forecast-time selection | Dedicated touch list opened; choosing 18:00 updated the chart/time |
| Playback | Advanced automatically to the last forecast and stopped |
| Existing GRIB | Opened through Android file picker |
| Native Android UTC display test | Summer, winter, leap day and table format passed under three timezone settings |
| Android CircleCI build | Job 820 built the shared-engine package successfully; subsequent UI fixes require a new run |
| Cancel active download | Returned to usable form, “Generation cancelled” |
| Portrait/landscape generator | Fits available display; long pages scroll |
| Landscape keyboard | Form resizes; editing field and action buttons remain visible |
| Package | Local arm64 import package built; certificates/licences included |

Remaining release validation includes a production OpenCPN APK, additional
screen sizes, credentialed providers and extended low-memory/large-job testing.
The connected development app is not a substitute for testing the Play Store
build. Do not describe unchecked combinations as fully supported yet.

### Additional findings from live testing

- NOAA's GFS sea-level pressure field is `PRMSL`, not `PRES`; the shared
  request and its regression test now use the correct variable. The final
  test file contains nine forecast times with seven fields per time.
- The old wxQt readout/playback timer path did not operate in the plugin.
  Android uses direct touch readout updates and native Qt playback timers.
- On this BST tablet, wxQt formatted a 15:00 UTC epoch as 16:00 even with an
  explicit UTC request. Qt's explicit UTC formatter matched the actual GRIB
  timestamps; Android now uses it. No time values in the GRIB are shifted.
- Temperature degree symbols are constructed as Unicode, avoiding an
  extra `\u00c2` character from legacy narrow-string decoding.
- Ordinary forecast duration is measured from the chosen model cycle, as
  in the shared desktop engine. The form explains this distinction from
  tidal/current start time and reports the generated file's actual time range.

`test/AndroidTimeFormatTests.cpp` is built by Android CI as a separate device
test artifact, not included in the plugin payload. It runs with the matching
Qt5Core and libc++ shared libraries on `LD_LIBRARY_PATH`. Playback must be
tested inside OpenCPN: this Android Qt build requires a Java application context
to create its event loop, so a plain adb shell process cannot test GUI timers.

## Reproducible build and runtime isolation

`ci/build-android-arm64.sh` builds against the pinned OpenCPN 5.14 core and its
Qt 5.12.2/wxQt support. `ci/build-android-generator-deps.sh` uses a pinned vcpkg
baseline, arm64 Android triplet and scoped ecCodes/libsodium overlays.
Three missing, unmodified Qt public headers are supplied with upstream notices.

ecCodes needs its static accessor registration objects retained. The complete
curl/OpenSSL stack also must be linked privately: resolving some symbols
against OpenCPN's older OpenSSL caused a reproducible generation crash.
`ci/verify-android-runtime.py` rejects public/imported TLS symbols, preventing
that mixed-runtime build from passing CI. HTTPS verification remains enabled;
the package includes a Mozilla CA bundle, also configured for NetCDF DAP.

The original tablet plugin library and settings were backed up before testing.
No other plugin or chart/user-data files are replaced by this build.
