# Open and display a GRIB from another plugin

xGRIB 0.4.5 accepts the existing `GRIB_APPLY_JSON_CONFIG` message with an optional boolean `show`:

```json
{"grib_file":"/absolute/path/to/forecast.grb","show":true}
```

A true `show` with an existing file initializes the viewer if necessary, loads that exact file and enables its chart overlay. Repeated requests keep it visible. Display layer and unit preferences are preserved. A missing file or malformed request does not activate the viewer. The normal reader handles unsupported or corrupt file contents.

Omitting `show`, setting it to false, or sending settings without a file retains the existing non-activating message behavior. This does not hide a viewer that is already visible. A non-boolean `show` does not activate it. GRIB protocol version 5.0 and the native plugin API remain unchanged. The bundled-GRIB conflict policy still applies.

Weather Routing 2.2.5 sends this request after a user loads a passage forecast, or successfully generates one with “Open generated GRIB after creation” checked. Weather Routing continues to work without xGRIB. Older xGRIB versions can update an already-open viewer, but 0.4.5 is required for automatic initial display.
