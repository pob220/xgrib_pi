# xGRIB hosted GFS subset service

Scheduled NOAA/AWS cache and regional cropping API used by xGRIB 0.3.6
automatic GFS failover. The production endpoint is
`https://grib.agentracert.com`. Deployment is independent of OpenCPN.

The API creates regional GRIB2 files from a scheduled cache of NOAA GFS data
obtained from the public AWS mirror. Public requests cannot initiate upstream
downloads. Requests identify the exact UTC forecast cycle; unavailable coverage
returns HTTP 503 and never substitutes an older run or another model.

## Cached forecasts and retention

Native 0.25-degree grids, 209 forecast times per product: hourly f000 through
f120, then every three hours f123 through f384. This covers 16 forecast days.
GFS cycles are 00, 06, 12 and 18 UTC. The ingester checks every 15 minutes and
downloads each selected file once. Initial bootstrap checks f384 to select two
complete runs. Thereafter, the latest publishing run is advanced independently
for weather and waves, and shorter forecasts can be served before f384 exists.

Cached fields:

| Product | Fields |
| --- | --- |
| Weather | 10 m wind U/V, mean sea-level pressure, 2 m temperature |
| Waves | Significant height, primary-wave period, primary-wave direction |

These cover xGRIB's minimal/routing weather presets and its GFS wave fields.
The marine and all-displayable weather presets require additional fields and
cache sizing to support failover. The service caches the full global forecast horizon
for the selected fields, rather than every atmospheric variable/pressure level.

The measured 2026-10-04 12 UTC run uses 1,096,656,942 bytes for these seven
fields. Two complete runs use about 2.19 GB; an incoming run temporarily brings
the data to about 3.29 GB. A complete-run marker is published only after all
418 files are validated. The oldest complete run is then removed. File locks
pin runs used by active crops so they cannot disappear during a request.

The combined cache has a 4.5 GB admission budget, regional results a 250 MB
eviction budget, and downloads require 200 MB of free-disk headroom. Docker
images, bounded container logs and filesystem overhead use additional space.

## Protection and access

The Python API binds directly to `127.0.0.1:18743` on the server. The dedicated
`xgrib-singe-media` Cloudflare connector runs there and publishes
`grib.agentracert.com`; its route is `http://127.0.0.1:18743`. No Apache or router
changes are needed. Its metrics listen on loopback port 18744.

Public forecast endpoints accept a random 32-hex-character session identifier in
`X-xGRIB-Client-ID`. xGRIB creates this automatically for each hosted job: no
account, shared secret or setup is required. Limits apply independently to the
session and IP, so rotating identifiers cannot bypass IP or service budgets.
Job status belongs to the requesting session. Existing commissioning bearer
tokens remain supported; supplying an invalid token never enables public access.
Only `/healthz` is entirely unauthenticated. Authentication headers and user
regions are not written to access logs. Protected tokens stay outside the image.

Application limits:

- Two crop workers, eight admitted jobs and 24 HTTP request threads.
- Two simultaneous result downloads.
- 8 KB request bodies, 10 million requested values and 64 MB result files.
- Six submissions per 10 seconds, 120 per hour, and 600 API reads per minute,
  enforced independently for each session/token and IP address.
- 1 GB of downloads per UTC day per session/token and IP, and 20 GB per UTC day for
  the entire service. Reservations persist across API restarts; interrupted
  transfers still count. Environment variables `XGRIB_CLIENT_EGRESS_BYTES`
  and `XGRIB_DAILY_EGRESS_BYTES` can adjust those limits.
- Identical requests reuse a cached result; active identical requests from one
  client reuse the same job. Cropping the same result across clients is locked.
- Server containers have CPU, memory, process, filesystem and log limits.

Cloudflare Tunnel supplies the outbound connection and edge protection. The
proxied DNS record, dedicated route and free-plan-compatible edge rate rule
are configured. The rule applies to the exact `/v1/subsets` path: five requests
per 10 seconds per IP, then a 10-second block. A controlled unauthenticated
burst received five HTTP 401 responses, followed by HTTP 429 with
`Retry-After: 10`. The edge rule can be configured in Cloudflare; preserve unrelated domain rules.

HTTP 429/503 responses include `Retry-After`; the client must obey it. Native
clients should receive explicit blocks, without browser CAPTCHA challenges.
Clients must send an identifying User-Agent. Cloudflare's existing browser
integrity check rejects Python's generic default User-Agent; the commissioning
client sends `xGRIBHostedCheck/0.1` and succeeds without changing zone protection.

## API

`POST /v1/subsets`, with `Content-Type: application/json` and `X-xGRIB-Client-ID`:

```json
{
  "cycle": "2026100412",
  "hours": [0, 3, 6, 9, 12],
  "bbox": {"west": -8.5, "south": 50.5, "east": -2.5, "north": 56.5},
  "fields": {
    "weather": ["10u", "10v", "prmsl", "2t"],
    "waves": ["swh", "perpw", "dirpw"]
  },
  "stride": 1
}
```

Returns HTTP 202 with a `status_url`. Poll that URL no more than once per
second. A completed job contains the authenticated download URL, byte count,
SHA-256 digest, exact request and source metadata. Status records expire.

`GET /v1/capabilities` lists fields, native forecast times and complete cycles.
Partially ingested cycles can be served when all specifically requested files
are cached. Missing coverage is explicit. Only allowlisted fields can be served.

The ecCodes C++ helper clones metadata and crops native cells; it performs no
interpolation. Longitude bounds are west-inclusive/east-exclusive, and both
latitude bounds are inclusive, matching NOMADS wave subsetting. Date-line and
zero-meridian crossing regions and native-grid strides 1..8 are supported.

## Validation

```sh
g++ -O2 -std=c++17 -Wall -Wextra grib_crop.cpp -leccodes -o grib-crop
python3 -m unittest discover -s tests -v
```

Tests cover calendar/coverage validation, oversized requests, crop values and
missing cells, longitude boundaries, scanning direction, retention while pinned,
authentication, job lifecycle and persistent download-budget enforcement.

The production service was checked against independently decoded AWS data and
same-cycle NOMADS wave results, using ecCodes 2.28 and 2.47. The 99-message
96-hour wave comparison preserved all cells and missing masks within native
packing precision. Cold generation took 31 seconds; cached retrieval took
1.43 seconds. Client failover also passed through the public HTTPS endpoint.

## Operations

First installation: `sh deploy/start-private.sh`. This builds the image and
starts the API and persistent ingestion worker with automatic Docker restart.
Secrets and data are outside the source/image build context. The connector uses
Cloudflare's official image pinned to the pulled digest. Its setup token was
rotated during commissioning before use.

The server runs two independent processes: `sh deploy/start-api.sh` and
`sh deploy/start-ingest.sh`. Upgrade by building and testing the image, then
replacing these containers while keeping `data/` and `secrets/` intact. Keep
the previous image/container for rollback until the replacement passes checks.
The Cloudflare configuration token is needed only for setup; the connector
uses its separate protected tunnel token at runtime.

Useful read-only checks on the server:

```sh
curl -fsS http://127.0.0.1:18743/healthz
docker logs --tail 20 xgrib-grib-ingest
docker ps --filter name=xgrib
du -sh /home/singes/xgrib-hosted-service/data
```

## xGRIB integration

xGRIB 0.3.6 switches GFS weather/waves on connection failures, rate limits,
failing redirects and service errors. Healthy NOAA requests keep their existing
path; ordinary missing publishing files retain cycle selection. A paired job
remembers a NOAA outage to avoid repeatedly contacting it for the other GFS
component. Other providers keep their existing behaviour.

The client requests only unfinished timesteps at the selected exact cycle,
retains valid NOAA downloads, checks the service coverage before submission and
honours `Retry-After`. It validates the SHA-256 digest, exact request echo and
GRIB cycle, fields, levels, native grid and geographical coverage before merging.
Missing cached coverage may advance an automatically selected cycle; service
failures terminate the job rather than trying many older cycles. Unsupported
marine/all presets report an explicit limitation without discarding fields.

## Primary references

- https://registry.opendata.aws/noaa-gfs-bdp-pds/
- https://www.emc.ncep.noaa.gov/emc/pages/numerical_forecast_systems/gfs.php
- https://www.emc.ncep.noaa.gov/emc/pages/numerical_forecast_systems/wavemodels.php
- https://developers.cloudflare.com/tunnel/get-started/
- https://developers.cloudflare.com/waf/rate-limiting-rules/
