# xGRIB 0.4.4

Copernicus NWS current records use whole UTC hours. Previously, manually entering
an otherwise valid time such as 16:01 could begin weather/wave downloads before
failing with the ambiguous message “requested time is unavailable”.

The desktop and Android generator interfaces now share an input check for NWS
requests. They explain the whole-hour requirement before a download starts and
retain the requested time for the user to correct. Whole-hour requests and
other current sources, including arbitrary-time tidal calculations, are
unchanged. The check uses the converted UTC instant, including when desktop
controls display an IANA local timezone.

The native generator/provider code remains pinned to
`4208c87dbf223bedb78064043313c5fc09c629f1`; this release changes input validation,
not the current model, interpolation, GRIB encoding or Weather Routing engines.
The existing 17 catalogue platforms and API 1.21/1.22 build matrix are retained.
The Jasper source version and checksum are unchanged; a direct upstream archive
URL provides a fallback for transient GitHub archive errors.
