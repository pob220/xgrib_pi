#!/bin/sh
# Run from the source directory on singe.media. No website changes or public
# listener: a dedicated Cloudflare Tunnel will be configured separately.
set -eu
cd "$(dirname "$0")/.."
mkdir -p data secrets
chmod 700 secrets
if ! test -s secrets/tokens; then
    python3 -c 'import secrets; print(secrets.token_urlsafe(48))' > secrets/tokens
    chmod 600 secrets/tokens
fi
docker build -t xgrib-grib-service:0.2.1 .
sh deploy/start-api.sh
sh deploy/start-ingest.sh
