# Weather providers and display-data presets

xGRIB 0.2.2 offers four weather presets. `Minimal wind` requests 10 m wind
only. `Routing` adds mean-sea-level pressure and 2 m air temperature. `Marine
comfort` adds the provider's available gust, precipitation and total-cloud
fields. `All available display data` adds every further field from that
provider which xGRIB can classify and display. Waves and currents remain
separate selections.

“All available” is provider-aware: it does not request fields a source does
not publish, and it does not manufacture one physical quantity from a
differently defined field. In particular, AIFS skin temperature is not exposed
as sea-surface temperature.

## Field coverage

| Provider | Routing and marine fields | Further fields in “All available” |
|---|---|---|
| NOAA GFS 0.25° | 10 m wind, MSL pressure, 2 m temperature, gust, total precipitation, total cloud | 2 m relative humidity, CAPE, composite reflectivity; wind, temperature, relative humidity and geopotential height at 850/700/500/300 hPa |
| NOAA HRRR 3 km | 10 m wind, surface pressure, 2 m temperature, gust, total precipitation, total cloud | 2 m relative humidity, CAPE, composite reflectivity; wind at 850/700/500/300 hPa; temperature and geopotential height at 850/700/500 hPa |
| Met Office UKV 2 km | 10 m wind, MSL pressure, screen temperature, gust, precipitation accumulation, total cloud | screen relative humidity, CAPE; wind, temperature, relative humidity and geopotential height at 850/700/500/300 hPa |
| MET Norway Nordic 1 km | 10 m wind, MSL pressure, 2 m temperature, gust, one-hour precipitation, total cloud | 2 m relative humidity |
| DWD ICON-EU 0.0625° (about 7 km) | 10 m wind, MSL pressure, 2 m temperature, gust, total precipitation, total cloud | 2 m relative humidity and mixed-layer CAPE |
| ECMWF IFS Open Data 0.25° | 10 m wind, MSL pressure, 2 m temperature, gust, total precipitation, total cloud | most-unstable CAPE; wind, temperature, relative humidity and geopotential height at 850/700/500/300 hPa |
| ECMWF AIFS Single v2 Open Data 0.25° | 10 m wind, MSL pressure, 2 m temperature, total precipitation and total cloud; no published gust field | wind, temperature and geopotential height at 850/700/500/300 hPa |

The exact message count can exceed the number of rows implied by this table.
For example, NOAA's regional GRIB filter applies selected variables and levels
as a cross-product and can return extra valid messages at levels which xGRIB
does not display.

## Source formats and conversion

| Provider | Upstream access and format | xGRIB handling |
|---|---|---|
| GFS | NOAA NOMADS regional GRIB2 filter | Downloads a bbox and selected variable/level set directly. |
| HRRR | NOAA full Lambert-conformal GRIB2 plus text inventory | Uses HTTP byte ranges for selected messages. The selected messages retain the full model grid. |
| UKV | Public Met Office AWS objects, one CF-NetCDF file per field/time | Selects pressure axes where required, projects and bilinearly regrids the requested bbox, then writes regular-lat/lon GRIB2. |
| MET Norway | CF-NetCDF through the official THREDDS OPeNDAP endpoint | Reads only required projected hyperslabs, converts wind speed/direction to components, regrids the bbox and writes regular-lat/lon GRIB2. |
| ICON-EU | Bzip2-compressed, full-domain regular-lat/lon GRIB2 files | Decompresses selected field files and converts CCSDS-packed messages to simple packing. |
| IFS/AIFS | Global GRIB2 with JSON-lines `.index` files | Selects messages with HTTP byte ranges and converts CCSDS packing to simple packing. Current AIFS v2 data use the `aifs-single` path. |

xGRIB's in-process reader does not implement GRIB2 data representation
template 5.42 (CCSDS/AEC). The helper therefore decodes and repacks DWD and
ECMWF messages with ecCodes. During conversion it also normalizes provider
local cloud/precipitation identities and units, whole-atmosphere cloud levels,
surface gust/CAPE levels, and ECMWF geopotential to geopotential height.

The upstream format and access behavior are documented by the providers:

- [NOAA NOMADS GRIB filter](https://nomads.ncep.noaa.gov/info.php?page=gribfilter)
- [Met Office UKV Open Data on AWS](https://registry.opendata.aws/met-office-uk-deterministic/)
- [MET Norway THREDDS access](https://docs.api.met.no/doc/thredds.html)
- [DWD ICON-EU technical description](https://www.dwd.de/DE/leistungen/lf_20_wawfor/wawfor_techndoc_3_7.pdf?__blob=publicationFile&v=2)
- [ECMWF Open Data access, indexing and file naming](https://confluence.ecmwf.int/spaces/DAC/pages/272310539/ECMWF+open+data+real-time+forecasts+from+IFS+and+AIFS)

## Coverage, cadence and operational limits

- GFS is global and bbox-subset. The plugin accepts 1/3/6/12-hour steps.
- HRRR covers the contiguous United States, is hourly, and is limited to 48
  forecast hours.
- UKV covers the UK and Ireland. It is hourly through 54 hours and then
  three-hourly through 120 hours.
- MET Norway's compact Nordic forecast contains 57 hourly time records (0–56
  hours). xGRIB supports 1/3/6/12-hour sampling. The Nordic area preset selects
  this provider and a valid 48-hour hourly request.
- ICON-EU covers 23.5° W–62.5° E and 29.5°–70.5° N. xGRIB supports 1/3-hour
  sampling through 120 hours.
- ECMWF IFS is sampled at 3/6/12 hours; AIFS at 6/12 hours. The selected GRIB
  messages currently retain their global grids, so “All available” files can
  be large.

Forecast extension can join a short-range regional provider to GFS. The
preferred provider wins for identical parameter/level/time tuples and GFS
continues the later timeline.

## MET Norway terms and attribution

The MET Norway source requires no account. The integrated endpoint is
`met_forecast_1_0km_nordic_latest.nc`, a Lambert-conformal, MEPS-derived
postprocessed Nordic forecast. It is a continuously replaced “latest” product,
not an archive, and the service has no delivery SLA.

MET Norway states that its open data are licensed under NLOD 2.0 and CC BY 4.0
unless otherwise specified. Generated metadata records the source and licence;
redistribution should retain the attribution “Data from MET Norway” and the
applicable licence link. See [MET Norway licensing and
crediting](https://www.met.no/en/free-meteorological-data/Licensing-and-crediting).

All generated forecasts are model data for planning and experimentation, not
official navigation products.
