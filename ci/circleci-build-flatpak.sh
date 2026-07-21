#!/usr/bin/env bash

#
# Build the flatpak artifacts. Uses docker to run Fedora on
# in full-fledged VM; the actual build is done in the Fedora
# container.
#
# flatpak-builder can be run in a docker image. However, this
# must then be run in privileged mode, which means it we need
# a full VM to run it.
#

# bailout on errors and echo commands.
set -euo pipefail
set -x

if [ "${CIRCLECI_LOCAL:-}" = "true" ]; then
    if [[ -d ~/circleci-cache ]]; then
        if [[ -f ~/circleci-cache/apt-proxy ]]; then
            cat ~/circleci-cache/apt-proxy | sudo tee -a /etc/apt/apt.conf.d/00aptproxy
            cat /etc/apt/apt.conf.d/00aptproxy
        fi
    fi
fi

# Install extra build libs
ME=$(echo ${0##*/} | sed 's/\.sh//g')
EXTRA_LIBS=./ci/extras/extra_libs.txt
if test -f "$EXTRA_LIBS"; then
    sudo apt update
    while read line; do
        sudo apt-get install $line
    done < $EXTRA_LIBS
fi
EXTRA_LIBS=./ci/extras/${ME}_extra_libs.txt
if test -f "$EXTRA_LIBS"; then
    sudo apt update
    while read line; do
        sudo apt-get install $line
    done < $EXTRA_LIBS
fi

git config --global protocol.file.allow always
git submodule update --init --recursive

if [ -n "${CI:-}" ]; then
    sudo apt update

    # Avoid using outdated TLS certificates, see #210.
    sudo apt install --reinstall  ca-certificates

    # Handle possible outdated key for google packages, see #486
    wget -q -O - https://cli-assets.heroku.com/apt/release.key \
        | sudo apt-key add -
    wget -q -O - https://dl.google.com/linux/linux_signing_key.pub \
        | sudo apt-key add -

    # Install flatpak and flatpak-builder - obsoleted by flathub
    sudo apt install flatpak flatpak-builder

fi

flatpak remote-add --user --if-not-exists \
    flathub https://dl.flathub.org/repo/flathub.flatpakrepo


if [ "$FLATPAK_BRANCH" = "beta" ]; then
    flatpak install --user -y flathub org.freedesktop.Sdk//$SDK_VER >/dev/null
    flatpak remote-add --user --if-not-exists flathub-beta \
        https://dl.flathub.org/beta-repo/flathub-beta.flatpakrepo
    flatpak install --user -y flathub-beta \
        org.opencpn.OpenCPN >/dev/null
else
    flatpak install --user -y flathub org.freedesktop.Sdk//$SDK_VER >/dev/null
    flatpak remote-add --user --if-not-exists flathub \
        https://dl.flathub.org/repo/flathub.flatpakrepo
    flatpak install --user -y flathub \
        org.opencpn.OpenCPN >/dev/null
    FLATPAK_BRANCH='stable'
fi

rm -rf build && mkdir build && cd build
artifact_dir="${PWD}/artifacts/flatpak${SDK_VER}-${BUILD_ARCH}"
log_dir="${artifact_dir}/logs"
test_dir="${artifact_dir}/tests"
package_dir="${artifact_dir}/package"
fixture_dir="${artifact_dir}/fixtures"
mkdir -p "$log_dir" "$test_dir" "$package_dir" "$fixture_dir"
cp -f ../test/fixtures/* "$fixture_dir/"
if [ -n "$WX_VER" ]; then
    SET_WX_VER="-DWX_VER=$WX_VER"
else
    SET_WX_VER=""
fi

if [ "$FLATPAK_BRANCH" = '' ]; then
    cmake -DOCPN_TARGET=$OCPN_TARGET -DBUILD_ARCH=$BUILD_ARCH -DOCPN_FLATPAK_CONFIG=ON -DSDK_VER=$SDK_VER -DFLATPAK_BRANCH='beta' $SET_WX_VER .. 2>&1 | tee "$log_dir/configure.log"
else
    cmake -DOCPN_TARGET=$OCPN_TARGET -DBUILD_ARCH=$BUILD_ARCH -DOCPN_FLATPAK_CONFIG=ON -DSDK_VER=$SDK_VER -DFLATPAK_BRANCH=$FLATPAK_BRANCH $SET_WX_VER .. 2>&1 | tee "$log_dir/configure.log"
fi

manifest="flatpak/org.opencpn.OpenCPN.Plugin.xgrib_pi.yaml"
grep -q 'url: https://github.com/pob220/xgrib_pi' "$manifest"
if [ -n "${CIRCLE_SHA1:-}" ]; then
    grep -q "commit: ${CIRCLE_SHA1}" "$manifest"
fi

make flatpak-build 2>&1 | tee "$log_dir/build-and-test.log"
make flatpak-pkg 2>&1 | tee "$log_dir/package.log"
../scripts/test-flatpak-archive.sh . 2>&1 | tee "$log_dir/archive-validation.log"

extension=/app/extensions/xgrib_pi/share/opencpn/plugins/xgrib_pi
flatpak build \
    --bind-mount=/fixtures="${PWD}/../test/fixtures" \
    --bind-mount=/test-output="$test_dir" \
    app "$extension/bin/environmental-grib" merge-environment-gribs \
    --weather /fixtures/wind-known.grb2 \
    --current /fixtures/current-differing.grb \
    --output /test-output/combined-flatpak.grb2 --overwrite \
    >"$test_dir/merge-result.json"
flatpak build --bind-mount=/test-output="$test_dir" \
    app "$extension/bin/environmental-grib" inspect-grib \
    /test-output/combined-flatpak.grb2 \
    >"$test_dir/combined-inspection.json"
jq -e '.success == true and .output_message_count == 10 and
       .output_inspection.short_name_counts["10u"] == 2 and
       .output_inspection.short_name_counts["10v"] == 2 and
       .output_inspection.current_component_counts.u_49 == 3 and
       .output_inspection.current_component_counts.v_50 == 3' \
    "$test_dir/merge-result.json" >/dev/null

pair=$(../scripts/resolve-package-pair.sh .)
archive_source=$(printf '%s\n' "$pair" | sed -n '1p')
metadata_source=$(printf '%s\n' "$pair" | sed -n '2p')
find "$package_dir" -maxdepth 1 -type f \
    \( -name 'xgrib_pi-*.tar.gz' -o -name 'xgrib_pi-*.xml' \
       -o -name checksums.txt \) -delete
cp -f "$archive_source" "$metadata_source" "$package_dir/"
(cd "$package_dir" && find . -maxdepth 1 -type f ! -name checksums.txt \
    -print0 | sort -z | xargs -0 sha256sum >checksums.txt)
archive="$package_dir/$(basename "$archive_source")"
package_version=$(sed -n \
    's:.*<version>[[:space:]]*\([^[:space:]<]*\)[[:space:]]*</version>.*:\1:p' \
    "$metadata_source")
if [[ ! "$package_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Invalid or missing package version in $metadata_source" >&2
    exit 1
fi
(cd "$artifact_dir" && sha256sum \
    fixtures/wind-known.grb2 \
    fixtures/current-matching.grb \
    fixtures/current-differing.grb \
    fixtures/current-incompatible-area.grb \
    fixtures/current-incompatible-time.grb \
    fixtures/corrupt.grb \
    fixtures/fixture-manifest.json \
    tests/combined-flatpak.grb2 >tests/checksums.txt)

jq -n \
  --arg commit "$(git -C .. rev-parse HEAD)" \
  --arg target "flatpak${SDK_VER}-${BUILD_ARCH}" \
  --arg arch "$BUILD_ARCH" \
  --arg version "$package_version" \
  --arg package "$(basename "$archive")" \
  --arg checksum "$(sha256sum "$archive" | awk '{print $1}')" \
  '{schema: "xgrib-target-result-v1", target: $target,
    xgrib_repository_commit: $commit, xgrib_version: $version,
    opencpn_version: "Flatpak stable runtime; GUI not run in CI",
    operating_system: "Flatpak 25.08", operating_system_version: "25.08",
    architecture: $arch, compiler: "Freedesktop SDK toolchain",
    cmake_version: "recorded in configure.log", wxwidgets_version: "3.2",
    grib_library_versions: {eccodes: "2.43.0", netcdf: "4.9.3"},
    build_status: "passed", test_status: "passed", package_status: "passed",
    metadata_validation_status: "passed", installation_status: "not-run",
    plugin_discovery_status: "not-run", plugin_load_status: "not-run",
    graphical_test_status: "not-run", file_path_display_status: "contract-tested",
    merge_status: "passed", output_validation_status: "passed",
    screenshot_paths: [],
    log_paths: ["logs/configure.log", "logs/build-and-test.log",
                "logs/package.log", "logs/archive-validation.log"],
    package_filename: $package, package_checksum_sha256: $checksum,
    elapsed_time_seconds: null,
    result_classification: "build-and-package-only",
    blocker_or_failure_details:
      "Package helper merge passed in the Flatpak runtime; OpenCPN GUI installation was not run by this build job"}' \
  >"$artifact_dir/result.json"
jq '{target, operating_system, operating_system_version, architecture,
     compiler, cmake_version, wxwidgets_version, grib_library_versions}' \
  "$artifact_dir/result.json" >"$artifact_dir/environment.json"
