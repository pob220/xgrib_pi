#!/usr/bin/env bash

# Build a native Linux catalogue artifact in the selected distro container.
# CircleCI's Arm machine executor runs arm64 images natively; x86 jobs use the
# same path, keeping the dependency set and validation identical.
set -euo pipefail
set -x

cd "${HOME}/project"
git submodule update --init --recursive

test -n "${DOCKER_IMAGE:-}"
test -n "${OCPN_TARGET:-}"
test -n "${BUILD_ENV:-}"
dockerfile=${DOCKERFILE:-ci/Dockerfile.linux}
test -f "$dockerfile"

mkdir -p build
source_commit=$(git rev-parse HEAD)
docker build --build-arg "BASE_IMAGE=${DOCKER_IMAGE}" \
  -f "$dockerfile" -t xgrib-linux-build ci
docker run --rm \
  -e "BUILD_ENV=${BUILD_ENV}" \
  -e "OCPN_TARGET=${OCPN_TARGET}" \
  -e "WX_VER=${WX_VER:-32}" \
  -e "BUILD_GTK3=${BUILD_GTK3:-true}" \
  -e "CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-3}" \
  -e "XGRIB_SOURCE_COMMIT=${source_commit}" \
  -v "${PWD}:/src:ro" \
  -v "${PWD}/build:/work" \
  xgrib-linux-build \
  /src/ci/build-linux-catalogue.sh

sudo chmod -R a+rw build
