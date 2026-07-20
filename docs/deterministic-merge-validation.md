# Deterministic merge validation

The functional fixture generator is compiled from
`generator/tests/environmental_merge_tests.cpp`. It uses the production
ecCodes writers and `MergeEnvironmentalGribs`; no GRIB merge logic is copied
into the test.

The generated 3 x 2 grid covers 6.3 W to 6.1 W and 53.0 N to 53.1 N. Its
reference time is 2026-07-12 00:00 UTC. The fixtures contain known U/V wind
values at hours 0 and 3 and known U/V current values at hours 0, 3, and 6.
Matching-time, differing-compatible-time, weather-only, current-only,
corrupt, wrong-role, non-overlapping-area, and non-overlapping-time cases are
checked.

Run the complete production CLI and xGRIB-reader validation after building:

```sh
scripts/run-functional-merge-test.sh build artifacts/local
```

The output directory retains the source fixtures, combined GRIB, structured
merge result, independent inspection, xGRIB reader log, and SHA-256 checksums.
Checksum entries are relative to the artifact directory, so downloaded or
moved evidence can be checked with `sha256sum -c tests/checksums.txt` from that
directory (or `shasum -a 256 -c` on macOS).
