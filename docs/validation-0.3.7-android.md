# xGRIB 0.3.7 Android and shared-generator validation

## Source and installation

The Android build starts from `630a04f`, the exact source of the tablet's
installed 0.3.6 library. It retains the Android touch interface, encrypted
credentials, private PNG-handler initialization and 16 KB LOAD/RELRO alignment.
The 0.3.7 date-line changes are applied on top. Android and desktop use generator
revision `891a8e865dabcc9e81448f63d6efa5e1876329ab`.

Device: Samsung SM-X210, Android 15, arm64. Host: OpenCPN
`5.14.1-pob220-16k-test`, package `org.opencpn.opencpn.dev`.
The plugin was imported through Android's OpenCPN plugin manager, with final
shared-engine changes installed as an atomic replacement of that imported
library. The final installed library is identical to the import archive's
payload: SHA-256 `c6f4cf517401a952c20b0450e3ee39f972e22dd8a79a825804f19f9b7a7872b9`.
Other plugin binaries and the host APK are unchanged.

## Shared fixes found during tablet testing

- Global Copernicus ARCO uses nominal padded boundary chunks. Using clipped
  longitude strides caused missing western current cells near 180 degrees.
  The reader now selects nominal or clipped layout from the exact decoded
  byte count, validates dimensions and retains source masks. Regression data
  exercises both layouts with nonconstant longitude values.
- A non-overlapping weather/current merge still fails. Its shared error now
  includes both UTC ranges and explains how to adjust weather duration or
  the current start. This message is used by every platform.
- CF NetCDF time origins normalize positive and negative UTC offsets, including
  offsets separated by whitespace, compact offsets and abbreviated hours.
  A negative offset no longer acquires an erroneous trailing `Z`.
- Gregorian dates are validated before time conversion. Unsupported noncivil
  NetCDF calendars produce an explicit error instead of incorrect timestamps.

## Date-line time rules

Longitude wraps; UTC does not. For example, `2026-10-05 00:00 +12:00` and
`2026-10-04 00:00 -12:00` identify the same instant, `2026-10-04 12:00 UTC`.
A forecast field keeps that same timestamp throughout its grid.

| Provider/storage | Time interpretation |
| --- | --- |
| GFS, HRRR, IFS/AIFS, ICON and downloaded GRIB | Decode reference time, lead and valid time from GRIB; retain cycles and cadence |
| UKV, Nordic, local NetCDF and NetCDF current products | Decode CF origin, units and UTC offset; validate the calendar |
| Copernicus ARCO currents/waves | Decode the metadata's epoch-based time axis; select only usable published frames |
| TPXO/XTD tidal predictions | Evaluate requested UTC instants at every grid point |
| Hosted GFS failover | Require the same cycle and requested forecast leads as the original NOAA job |

Provider adapters differ; adding or subtracting a day according to longitude
is never a valid adapter operation. The CF time origin/calendar rules are
specified in [CF conventions](https://cfconventions.org/cf-conventions/cf-conventions.html#time-coordinate);
GRIB time metadata is described by [ECMWF's GRIB format reference](https://codes.ecmwf.int/grib/format/grib2/).

## Verification on 6 October 2026

| Check | Result |
| --- | --- |
| Desktop regression suite using final shared generator | 39/39 passed |
| Standalone Linux generator suite | 9/9 passed |
| Final native arm64 Android suites | All eight passed |
| Viable component fixture matrix | 230 combinations passed on Linux and Android |
| Padded and clipped ARCO boundary chunks | Passed |
| Both local calendar dates mapping to one UTC instant | Passed |
| Crossing GRIB1/GRIB2 merges through midnight, year-end and both leap-day boundaries | Passed; exact expected valid times, no extra day |
| Positive/negative CF time origins and unsupported-calendar rejection | Passed |
| Android TLS symbol isolation and final 16 KB package/library audits | Passed |
| Native Android live GFS weather plus GFS waves, W179/E-179/S-1/N1 | 21 records generated, opened and independently decoded |
| Native Android server download with the same crossing box | 114 records downloaded, decoded and opened |
| Final native Android live IFS + GFS waves + credentialed Copernicus Global | 81 records; every current record has zero missing cells in this ocean test area |
| Production reader sampling final mixed file | Wind, current and waves valid at 179.75, 180, -180 and -179.75 degrees |
| Android chart readout and stepping through 6–7 October UTC midnight | Wind, current, waves, temperature and pressure displayed; midnight advanced correctly |
| Native ordinary English Channel GFS weather + waves after cancellation | 21 records generated successfully |
| Live Android cancellation | Returned to usable form; no partial final output published |
| Portrait/landscape and cold restart | Native touch controls usable; plugin loaded successfully |

A short IFS/current job was deliberately found to have no common time period.
Increasing weather duration produced a valid mixed file. Fields retain their
own actual time coverage: the overall file's time union is not a promise that
all providers supply every frame. Regional sources still require real coverage.

The 230-case matrix uses deterministic provider fixtures; it is not 230 live
provider downloads. Live checks above qualify the attached development host.
Windows/macOS GUI runs, the Play Store APK, other screen sizes and large-area
memory stress are not claimed by this validation.

Artifacts, screenshots, device outputs, logs, package and private rollback
backup: `artifacts/xgrib-0.3.7-android-20261006/` in the OpenCPN workspace.
