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
- Currents: TPXO10 local model/cache, Marine.ie Irish Sea, NOAA RTOFS,
  Copernicus NWS/Global, an existing GRIB, NetCDF, or synthetic test data.

Some sources require provider credentials or separately licensed local model
data. These are not bundled with xGRIB.

## Build

The xGRIB viewer requires the normal OpenCPN plugin toolchain plus development
packages for wxWidgets, Jasper, bzip2 and zlib. Building the bundled native
generator also requires ecCodes, NetCDF, libcurl, jsoncpp, Qhull, Blosc,
libzip and PROJ.

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
