#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
docker run -d --name xgrib-grib-service --restart unless-stopped \
    --cpus 2 --memory 1500m --pids-limit 64 --read-only \
    --cap-drop ALL --security-opt no-new-privileges \
    --tmpfs /tmp:rw,noexec,nosuid,size=64m \
    --log-opt max-size=5m --log-opt max-file=2 \
    --user "$(id -u):$(id -g)" \
    --network host -e XGRIB_TRUST_PROXY=1 -e XGRIB_PUBLIC_ACCESS=1 \
    -v "$PWD/data:/data" -v "$PWD/secrets:/run/xgrib:ro" \
    xgrib-grib-service:0.2.1 serve --bind 127.0.0.1
