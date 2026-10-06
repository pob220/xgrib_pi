# xGRIB 0.3.7 developer-preview provider

This pinned preview build retains the current 0.3.7 plugin and generator,
including the date-line and older-ecCodes surface-wave fixes. It carries the
existing environmental-data provider from the September developer preview.
The provider registers only when the enhanced host supplies the optional
external-control API, and unregisters before the plugin is unloaded.
Normal stock-host operation remains available without that API.
