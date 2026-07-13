# XTD v2 offline-current container

XTD v2 is an authenticated, random-access container for an offline
astronomical and climatological current model. It preserves a complete XTD v1
astronomical-tide package and adds independently tiled expected seasonal
circulation and uncertainty components.

The climatological component is an expected historical circulation. It is not
a forecast and does not predict eddies, exact boundary-current meanders,
storm-driven flow, surge, river discharge, or other short-term anomalies.

## Layout

All integers are fixed-width little-endian values. Ranges are absolute file
offsets and readers must validate every addition and multiplication before
allocation or I/O.

```text
512-byte XTD v2 header
canonical UTF-8 JSON package metadata
256-byte fixed component-directory entries
complete embedded XTD v1 package
residual metadata, 64-byte tile index, encrypted tiles
uncertainty metadata, 64-byte tile index, encrypted tiles
```

The outer magic is `XGRIBX2\0`, format version is 2, the endian marker is
`0x01020304`, and the header size is 512 bytes. Header bytes 200 through 511
are reserved and must be zero.

Header fields are:

| Offset | Size | Meaning |
|---:|---:|---|
| 0 | 8 | magic |
| 8 | 4 | format version |
| 12 | 4 | header size |
| 16 | 4 | endian marker |
| 20 | 4 | flags |
| 24 | 4 | component count |
| 28 | 4 | component-entry size |
| 32,40 | 8 each | metadata offset and length |
| 48,56 | 8 each | directory offset and length |
| 64 | 8 | first payload offset |
| 72 | 8 | exact file length |
| 80 | 16 | package id |
| 96 | 24 | package-key wrapping nonce |
| 120 | 48 | authenticated wrapped package key |
| 168 | 32 | keyed public-region MAC |

The public MAC covers the header with its MAC bytes zeroed, then the exact
metadata and component directory. The random package key is wrapped with the
same runtime-root mechanism as XTD v1.

## Components

Directory entries are sorted by numeric type and contain type,
representation, version, flags, component id, grid/index information, all
ranges, and SHA-256 hashes of logical, stored, and source-manifest content.
Unknown required components are rejected.

The first release defines:

1. `deterministic_tide`, representation `embedded_xtd_v1`.
2. `climatological_residual`, representation `harmonic2` or `monthly12`.
3. `climatological_uncertainty`, representation `uncertainty_v1`.

The complete nested v1 byte range is copied unchanged. Its logical and stored
hashes are identical and must agree with `parent_package_hash` in outer
metadata. A bounded random-access source prevents the nested reader from
accessing bytes outside that range.

Tiled components use deterministic row-major tile ids. Each non-empty tile is
compressed with Zstandard and authenticated with XChaCha20-Poly1305. Component
keys are derived from the package key and component id using domain-separated
keyed BLAKE2b. Tile AAD is:

```text
"XTD2-TILE" || package_id || component_id || first_40_index_bytes
```

Readers authenticate before decompression, enforce configured cache and
plaintext limits, and never treat unavailable or corrupt values as zero.

## Residual values

Residual tiles use `XCR1`, a water mask, one symmetric float32 scale per field,
and field-major signed-int16 planes. Encoding 1 applies the reversible v1
two-dimensional modular delta transform before compression.

`harmonic2` fields are:

```text
u_mean, u_annual_cos, u_annual_sin, u_semiannual_cos, u_semiannual_sin,
v_mean, v_annual_cos, v_annual_sin, v_semiannual_cos, v_semiannual_sin
```

The phase is the elapsed fraction of the current Gregorian UTC year, including
leap years. `monthly12` stores January through December U followed by January
through December V and interpolates cyclically between calendar-month centres.

## Uncertainty values

Uncertainty tiles use `XCU1`. Continuous fields are residual U RMS, residual V
RMS, U/V covariance, and blocked-year expected-model standard error. Quality
planes hold accepted-year count, valid fraction, effective sample count,
skill summaries, and flags. This describes historical variability and model
stability; it is not forecast confidence.

## Modes and failure behaviour

XTD v1 supports `Astronomical tide only`. XTD v2 supports that mode and, when
the residual component validates, `Tide + expected seasonal circulation`.
Mode selection is explicit and persisted. A missing or corrupt residual causes
total mode to fail; it is never silently downgraded. Explicit tide-only mode
may use an independently valid nested v1 without loading residual or
uncertainty tiles.

Generated XTD v2 packages are not distributable merely because the reader and
format are public. Source-model licences, attribution, scientific validation,
and package-specific release approval remain separate requirements.
