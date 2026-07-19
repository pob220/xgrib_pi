# Third-party components

xGRIB catalogue packages contain a native helper and may bundle
the following runtime components. They remain under their own licenses:

- ecCodes (Apache-2.0)
- JsonCpp (MIT/public-domain dual terms)
- netCDF-C (BSD-3-Clause)
- curl (curl license)
- Qhull (Qhull license)
- bzip2 (BSD-like license)
- c-blosc (BSD-3-Clause)
- libzip (BSD-3-Clause)
- PROJ (MIT)
- libsodium (ISC)
- Zstandard (BSD-3-Clause/GPL-2.0-only dual terms)
- Jasper 4.2.9 (JasPer License 2.0; pinned and statically linked into the
  viewer plugin)

Their transitive runtime libraries retain their respective distribution and
upstream licenses. Release builds must collect the corresponding license files
from the build image into `runtime/licenses`; see `docs/catalogue-release.md`.
Source code and license links for the direct components are recorded in the
native helper repository:
https://github.com/pob220/environmental-grib-generator

The Environmental GRIB Generator helper itself is MIT licensed. The OpenCPN
plugin is GPL-3.0-or-later.
