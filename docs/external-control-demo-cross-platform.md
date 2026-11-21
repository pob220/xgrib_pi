# xGRIB 0.4.6 developer-preview provider

This pinned preview uses the qualified xGRIB 0.4.6 source and Generator 0.4.0,
including native CCSDS-compressed GRIB2 decoding, immediate area/time checks,
Copernicus authentication diagnostics, date-line handling and surface-wave
fixes. It retains the unchanged environmental-data provider from the previously
qualified developer bundle.
The provider registers only when the enhanced host supplies the optional
external-control API, and unregisters before the plugin is unloaded.
Normal stock-host operation remains available without that API.
