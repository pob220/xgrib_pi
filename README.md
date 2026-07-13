# xGRIB for OpenCPN

xGRIB is a catalogue-installable GRIB viewer and environmental-data generator
for OpenCPN. It retains the familiar GRIB timeline, overlays, cursor data,
download tools and plugin-message protocol, while adding an integrated native
generator for combined weather, wave and current GRIB files.

The generator runs in a separate process. Provider downloads, NetCDF parsing,
UKV regridding, TPXO current calculation and ecCodes writing therefore do not
run inside OpenCPN. Generated files open directly in xGRIB after strict GRIB
validation.

## Important installation rule

xGRIB replaces the bundled GRIB plugin; the two must not be active together.
Disable **GRIB** in **Settings > Plugins**, enable **xGRIB**, and restart
OpenCPN. xGRIB detects an enabled bundled GRIB and remains inactive rather than
creating duplicate overlays or ambiguous `GRIB_*` plugin messages.

## Supported generator sources

- Weather: NOAA GFS and HRRR, Met Office UKV, DWD ICON-EU, ECMWF IFS/AIFS
  Open Data, or an existing GRIB.
- Waves: NOAA GFS Wave and Copernicus Marine Global Waves.
- Currents: Offline Tidal from a separately supplied xGRIB `.xtd` package,
  TPXO10 local model/cache, Marine.ie Irish Sea, NOAA RTOFS, Copernicus
  NWS/Global, an existing GRIB, NetCDF, or synthetic test data.

Some sources require provider credentials or separately licensed local model
data. These are not bundled with xGRIB.

### Offline Tidal

**Offline Tidal** evaluates global astronomical tidal-current harmonics from
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

Offline Tidal does not require internet access or TPXO NetCDF files at runtime.
It is separate from the direct TPXO and prepared TPXO-cache options, which use
model data supplied locally by the user. The global XTD package is deliberately
not embedded in the plugin archive because of its size and update lifecycle.
Distribution of a package derived from TPXO requires compliance with the
source model's registration, copyright, and redistribution terms. The reader
and package format do not themselves grant permission to redistribute model
data.
See [the XTD v1 reader format](docs/xtd-format-v1.md).

## Build

The xGRIB viewer requires the normal OpenCPN plugin toolchain plus development
packages for wxWidgets, Jasper, bzip2 and zlib. Building the bundled native
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

Linux catalogue packages include a private helper runtime and ecCodes/PROJ
data. They do not alter OpenCPN's process-wide library search path.

See [the architecture note](docs/architecture.md) and
[catalogue release procedure](docs/catalogue-release.md).

Generated model data is for planning and experimentation. It is not an
official navigation product and does not replace notices to mariners,
observations, forecasts, or prudent seamanship.

## License

xGRIB is GPL-3.0-or-later. The native helper is MIT licensed. Bundled
third-party components retain their respective licenses.
