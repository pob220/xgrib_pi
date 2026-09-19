# xGRIB 0.2.6 validation — 19 September 2026

## Fix

The shared generator inspector previously sorted the first and last longitude
of every GRIB grid. ECMWF's global 0.25-degree grid (1440 columns, first 180,
last 179.75) therefore appeared to cover only 179.75–180 degrees. Combining
it with Copernicus currents around Tonga incorrectly failed geographic
overlap validation, including requests wholly east of the antimeridian.

Generator 0.1.10 derives regular longitude coverage from grid dimensions,
increment and scan direction. Cyclic global grids cover every longitude;
wrapped regional grids retain their eastward interval. Individual grid
regions are compared modulo 360, preserving latitude limits and gaps between
separate regions. This is shared by IFS/AIFS, other regular-grid providers
and imported files. Source coordinates, field values and merged records are
not rewritten. Generation request boxes crossing the antimeridian remain
unsupported; that separate request validation is unchanged.

## Results

- New regression failed before the fix and passed afterwards.
- GRIB1 and GRIB2 global grids: origins 0 and 180, forward/reverse scanning,
  with and without a repeated seam endpoint.
- Regional antimeridian and Greenwich crossings, positive/negative 180
  boundary equivalence, wide regions, disjoint regions and wrong latitudes.
- Global weather plus regional Tonga currents, with and without wave fields.
- Clean Linux Release preflight: all 28 CTest tests passed, deterministic
  merge/production-reader checks passed, staged helper passed, and exact
  catalogue archive passed. Local native package target: Arch x86_64 rolling.
- Actual failed ECMWF/Copernicus cache pair: 132 weather plus 66 current
  messages merged successfully (413,404,992 bytes). The production xGRIB
  reader sampled both wind and both current components at 20 S, 175 W.
- Python differential comparison passed synthetic byte identity and NetCDF
  value parity, then failed TPXO-cache comparison at maximum difference
  0.00040149688720703125 against its strict 1e-12 tolerance. The same failure
  and value were reproduced with the old installed helper. This is a
  pre-existing reference/model discrepancy, not a regression from longitude
  coverage changes; full differential parity is not claimed.

The local package is suitable for this working machine. Cross-platform
release artifacts still require their corresponding hosted CI builds.
