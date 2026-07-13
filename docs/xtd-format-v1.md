# xGRIB Tidal Data format v1

## Purpose and scope

An XTD file is an xGRIB-specific, random-access package containing derived
eastward and northward astronomical tidal-current velocity harmonics. Version
1 is designed for the full nominal TPXO10 Atlas v2 1/30-degree C grid. It does
not embed NetCDF files, source transport fields, bathymetry, tidal height, or
unrelated model data.

The format specification describes xGRIB's reader contract. It does not grant
rights to distribute packages derived from third-party source models; package
publishers remain responsible for the source model's licence and attribution.

The public reader treats an XTD package as untrusted binary input. Metadata,
the tile index, and every tile payload are authenticated before use. All
integers use little-endian fixed-width encoding. Reserved fields must be zero.

## File layout

```text
512-byte fixed header
UTF-8 JSON public metadata
fixed-width tile index
authenticated encrypted tile payloads
```

The eight-byte magic is `XGRIBX1\0`. The header declares format version,
endianness marker, full grid dimensions, tile dimensions, constituent count,
metadata/index/payload offsets, U/V staggered-grid origins and common spacing,
coverage, package identifier, content-key wrapping metadata, declared file
length, and a keyed authentication value covering the header, metadata, and
index.

The JSON metadata identifies the source model, package/build version,
constituents, coverage, nominal resolution, coefficient units and correction
convention. Readers must not use metadata until its authentication succeeds.

## Tile index

Index entries are 64 bytes in deterministic row-major tile order. Each entry
contains:

- tile id and X/Y tile coordinates;
- actual edge-tile width and height;
- flags, including an explicit empty tile;
- compression identifier;
- payload offset;
- encrypted and uncompressed lengths;
- a 24-byte per-tile nonce.

The reader verifies tile count, dimensions, ordering, integer overflow,
non-overlap, bounds, and declared lengths before any payload is allocated.
Empty tiles carry no payload and represent cells for which no tidal velocity
harmonics are available; they are not interpreted as zero current.

## Tile plaintext

After authenticated decryption and Zstandard decompression, a non-empty tile
contains:

- the `XTP1` tile magic and version;
- tile dimensions and count fields;
- separate U-grid and V-grid validity masks;
- one float32 quantization scale for each constituent/component;
- signed int16 coefficients in constituent-major order:
  U-real, U-imaginary, V-real, V-imaginary.

Version 1 encoding `1` applies a reversible two-dimensional modular delta
predictor to each signed int16 component plane before compression. The
predictor changes no coefficient bits; the reader restores the exact quantized
values after decompression. The global Atlas-parity v1 package uses this
16-bit encoding. Encoding `2` reserves a packed signed 12-bit delta mode for
smaller future packages whose precision requirements permit it. The encoding
identifier and exact plaintext length must agree or the tile is rejected.

Coefficients are velocities in centimetres per second. The source
transport-to-velocity conversion has already been performed on the appropriate
staggered grid. Tile-local, component-local scaling retains substantially more
precision than a global scale while supporting compact decoding.

## Compression and authentication

Each non-empty tile is compressed independently with Zstandard, then encrypted
and authenticated with XChaCha20-Poly1305. Associated data binds a payload to
its package id and exact index entry. A random package content key is wrapped
for the xGRIB runtime; this is a practical copying/access deterrent rather than
a claim of absolute DRM.

The runtime decrypts only requested tiles into memory, clears key material and
temporary plaintext where practical, and maintains a bounded LRU cache. It
does not expose a coefficient export operation.

## Runtime interpolation and prediction

Longitude is periodic for a global package. U and V retain their native C-grid
origins. The reader performs bilinear interpolation independently on each
staggered grid and requires all four interpolation corners to be valid. Missing
or masked data remains missing; it is never converted to zero current.

The resulting complex harmonics are passed to the same Atlas astronomical
prediction implementation used by xGRIB's direct TPXO path. This includes UTC
handling, astronomical arguments, nodal corrections, equilibrium arguments,
phase convention, and optional supported minor-constituent inference. Output
is converted to metres per second only when written as GRIB parameter 49/50.

## Evolution

Readers reject unsupported versions and nonzero reserved fields. Future
formats may add new compression methods, metadata, precision modes, or model
families under a new explicit format version; v1 parsers must not guess at
unknown layouts.
