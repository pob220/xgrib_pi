# xGRIB for OpenCPN

xGRIB is a catalogue-installable GRIB viewer and environmental-data generator
for OpenCPN. It retains the familiar GRIB timeline, overlays, cursor data,
download tools and plugin-message protocol, while adding an integrated native
generator for combined weather, wave and current GRIB files.

The generator runs in a separate process. Provider downloads, NetCDF parsing,
UKV regridding, TPXO current calculation and ecCodes writing therefore do not
run inside OpenCPN. Generated files open directly in xGRIB after strict GRIB
validation.

The environmental generator can optionally extend a manually selected
short-range forecast to 15 days. Preferred weather, wave and current records
retain priority over explicitly selected long-range sources; xGRIB writes one
composite GRIB with each provider's native cadence. For example, UKV remains
hourly through 54 hours and three-hourly through 120 hours before NOAA GFS
continues the weather timeline. The generator reports the actual component
coverage after completion. Forecast duration is measured from the requested
UTC start; model-cycle age is allowed for and the final file is capped at the
requested end so weather, waves and currents cannot silently leave a
current-only tail.

## Important installation rule

xGRIB replaces the bundled GRIB plugin; the two must not be active together.
Disable **GRIB** in **Settings > Plugins**, enable **xGRIB**, and restart
OpenCPN. xGRIB detects an enabled bundled GRIB and remains inactive rather than
creating duplicate overlays or ambiguous `GRIB_*` plugin messages.

## Supported generator sources

- Weather: NOAA GFS and HRRR, Met Office UKV, DWD ICON-EU, ECMWF IFS/AIFS
  Open Data, or an existing GRIB.
- Waves: NOAA GFS Wave and Copernicus Marine Global Waves.
- Currents: Offline current from a separately supplied xGRIB `.xtd` package,
  TPXO10 local model/cache, Marine.ie Irish Sea, NOAA RTOFS, Copernicus
  NWS/Global, an existing GRIB, NetCDF, or synthetic test data.

Some sources require provider credentials or separately licensed local model
data. These are not bundled with xGRIB.

### Offline current (.xtd)

**Offline current** evaluates global astronomical tidal-current harmonics from
a separately supplied `xgrib-global-tides.xtd` package. Select the package in
the generator's current-source controls; xGRIB authenticates it and shows
its model, coverage, resolution, constituent count, and build identity before
generation is enabled. The selected path is retained in the plugin settings.

XTD v1 is a runtime-optimized representation derived from the full-resolution
TPXO10 Atlas v2 current solution. It is intended to reproduce that model's
eastward and northward astronomical tidal currents, including its standard
astronomical and nodal corrections and optional minor-constituent inference.
It does not contain or predict ocean circulation, Gulf Stream or gyre flow,
storm surge, wind-driven residuals, river flow, or other non-tidal currents.
Use a forecast/model provider when those effects are required.

Offline current does not require internet access or TPXO NetCDF files at runtime.
It is separate from the direct TPXO and prepared TPXO-cache options, which use
model data supplied locally by the user. The global XTD package is deliberately
not embedded in the plugin archive because of its size and update lifecycle.
Distribution of a package derived from TPXO requires compliance with the
source model's registration, copyright, and redistribution terms. The reader
and package format do not themselves grant permission to redistribute model
data.
See [the XTD v1 reader format](docs/xtd-format-v1.md).

An XTD v2 package can additionally contain long-term mean and predictable
seasonal circulation. When such a package is selected, choose explicitly
between **Astronomical tide only** and **Tide + expected seasonal
circulation**. The latter is historical climatology, not an ocean-current
forecast. Existing XTD v1 settings remain tide-only and no mode falls back
silently. See [the XTD v2 container format](docs/xtd-format-v2.md).

For diagnostics and independent validation, the bundled native helper can
sample a package without generating a GRIB:

```sh
environmental-grib sample-xtd currents.xtd \
  --latitude 53.75 --longitude -5.2 \
  --time 2026-07-13T12:00:00Z --mode tide-expected-seasonal
```

The reported direction is the oceanographic direction **towards** which the
current flows. Regional sampling reads only the necessary tide/residual tiles;
uncertainty tiles are not loaded for ordinary prediction.

## Build

The xGRIB viewer requires the normal OpenCPN plugin toolchain plus development
packages for wxWidgets, bzip2 and zlib. A pinned Jasper 4.2.9 is built
statically by default. Building the bundled native
generator also requires ecCodes, NetCDF, libcurl, jsoncpp, Qhull, Blosc,
libzip, PROJ, libsodium and Zstandard.

```sh
git clone --recurse-submodules https://github.com/pob220/xgrib_pi.git
cd xgrib_pi
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUNDLE_GENERATOR_RUNTIME=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Named presets make the tested platform intent explicit:

```sh
cmake --preset linux-release
cmake --build --preset linux-release-build
ctest --preset linux-release-test
```

Before pushing shared source, CMake, dependency, metadata or packaging changes,
run the clean end-to-end local preflight:

```sh
scripts/validate-before-push.sh
```

It checks the recorded submodules and portable gettext paths, configures from a
clean directory, builds, runs CTest, performs the deterministic merge and
production-reader reopen, stages the helper, packages xGRIB and validates the
unique archive/metadata pair. On Windows use
`ci\validate-before-push-windows.ps1`; `-ChecksOnly` performs just the fast
syntax, repository and preset checks.

Linux catalogue packages include a private helper runtime and ecCodes/PROJ
data. They do not alter OpenCPN's process-wide library search path.

See [the architecture note](docs/architecture.md) and
[catalogue release procedure](docs/catalogue-release.md). The repeatable local,
container, hosted-platform and evidence workflow is documented in
[Alpha validation](docs/alpha-validation.md).

Generated model data is for planning and experimentation. It is not an
official navigation product and does not replace notices to mariners,
observations, forecasts, or prudent seamanship.

## License

xGRIB is GPL-3.0-or-later. The native helper is MIT licensed. Bundled
third-party components retain their respective licenses.
