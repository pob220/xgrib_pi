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

# ARMHF runs the actual 32-bit distro, compiler, tests and packaged helper.
# Register only ARM emulation on this disposable CircleCI VM. Pin the image.
docker_platform=()
if [[ ${XGRIB_EMULATE_ARMHF:-false} == true ]]; then
  docker_platform=(--platform linux/arm/v7)
  docker run --privileged --rm \
    tonistiigi/binfmt@sha256:400a4873b838d1b89194d982c45e5fb3cda4593fbfd7e08a02e76b03b21166f0 \
    --install arm
fi

mkdir -p build
source_commit=$(git rev-parse HEAD)
docker build "${docker_platform[@]}" --build-arg "BASE_IMAGE=${DOCKER_IMAGE}" \
  -f "$dockerfile" -t xgrib-linux-build ci
docker run --rm "${docker_platform[@]}" \
  -e "BUILD_ENV=${BUILD_ENV}" \
  -e "OCPN_TARGET=${OCPN_TARGET}" \
  -e "WX_VER=${WX_VER:-32}" \
  -e "BUILD_GTK3=${BUILD_GTK3:-true}" \
  -e "CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-3}" \
  -e "XGRIB_API_HEADER_VERSION=${XGRIB_API_HEADER_VERSION:-1.21}" \
  -e "XGRIB_SOURCE_COMMIT=${source_commit}" \
  -v "${PWD}:/src:ro" \
  -v "${PWD}/build:/work" \
  xgrib-linux-build \
  /src/ci/build-linux-catalogue.sh

sudo chmod -R a+rw build
