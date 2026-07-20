# xGRIB architecture

## Viewer boundary

xGRIB derives from OpenCPN's GRIB plugin and intentionally preserves its GRIB
reader, timeline, overlays, request dialogs and `GRIB_*` messaging contract.
This keeps Weather Routing and other consumers compatible without copying
records between plugins.

xGRIB has a distinct library name, catalogue identity and configuration
namespace. Since bundled GRIB implements the same overlays and message IDs,
xGRIB detects it at startup and remains inactive while it is enabled. The user
must explicitly disable bundled GRIB; xGRIB never edits another plugin's
configuration automatically.

## Generator process boundary

The wxWidgets dialog owns configuration, viewport presets and progress display.
Provider downloads, NetCDF parsing, UKV regridding, TPXO/XTD current
calculation and GRIB writing run in the `environmental-grib` helper process.

The dialog writes a schema-versioned job file, starts the helper asynchronously,
consumes JSON-lines progress, and reads an atomically written result file.
Passwords are supplied only through the child environment. Logs, job files and
displayed commands redact credentials and credential-like URL parameters.

Offline Tidal is implemented inside this same helper boundary. The plugin UI
passes an `offlineTidalFile` path in the schema-versioned job. The helper
authenticates package metadata and the tile index, loads only tiles needed by
the requested output grid, decrypts and decompresses them in memory, and
reuses interpolated harmonic coefficients for every requested timestamp.
Decrypted coefficients are never written to temporary files. A bounded LRU
tile cache prevents a regional request from loading the global package.

On successful strict validation, a typed callback opens the generated file in
the owning xGRIB control bar. The public `GRIB_APPLY_JSON_CONFIG` message remains
supported for external clients, but the internal generator does not depend on
global plugin-message routing.

## Runtime layout

On Linux the catalogue payload uses this private layout:

```text
lib/opencpn/libxgrib_pi.so
share/opencpn/plugins/xgrib_pi/
  data/                            viewer resources
  bin/environmental-grib          launcher
  libexec/environmental-grib.bin  native helper
  runtime/lib/                     private shared libraries
  runtime/share/eccodes/           definitions and samples
  runtime/share/proj/              projection database
```

The launcher sets private runtime paths before executing the helper. It does
not alter OpenCPN's library path. Catalogue resources are located through
OpenCPN's `GetPluginDataDir`, so both system and per-user installs work.

## Lifecycle and failure handling

Only one modeless generator dialog exists per xGRIB control bar. Closing it
hides it for reuse. Unloading xGRIB stops an active child process and destroys
the dialog before the viewer. Dependency, provider, validation and output
errors stay visible in the dialog and do not replace a currently open GRIB.

## Platform scope

The validation matrix covers native Linux x86_64 and ARM64, Flatpak x86_64 and
aarch64, genuine Windows x86 using OpenCPN's official API 1.21 MSVC import
library, and genuine Apple-Silicon macOS. Platform
build success is not treated as runtime success: each target manifest records
packaging, installation, OpenCPN discovery/load, graphical inspection and
merge/reopen results separately. Intel macOS and physical Raspberry Pi runtime
checks remain distinct follow-up targets rather than inferred compatibility.
