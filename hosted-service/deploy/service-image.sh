#!/bin/sh
# Source after changing to the hosted-service directory.
# The ingester, API and image build must use the version of the actual source.
xgrib_service_version=$(python3 -c 'import service; print(service.VERSION)')
: "${XGRIB_IMAGE:=xgrib-grib-service:$xgrib_service_version}"
export XGRIB_IMAGE
unset xgrib_service_version
