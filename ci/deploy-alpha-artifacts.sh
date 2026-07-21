#!/usr/bin/env bash
# This script is reachable only from the parameter-gated, approval-gated
# CircleCI deployment workflow.  Normal validation never invokes it.
set -euo pipefail
set +x

if [[ -z "${CLOUDSMITH_API_KEY:-}" ]]; then
  echo "CLOUDSMITH_API_KEY is not configured for the deployment context." >&2
  exit 2
fi

repo=${CLOUDSMITH_ALPHA_REPO:-opencpn/xgrib-alpha}
version="0.1.0.1+${CIRCLE_BUILD_NUM:-0}.$(git rev-parse --short=7 HEAD)"
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT HUP INT TERM

find artifacts -mindepth 3 -maxdepth 3 -type f -path '*/package/*.tar.gz' \
  -print0 | while IFS= read -r -d '' archive; do
    package_dir=$(dirname "$archive")
    pair=$(scripts/resolve-package-pair.sh "$package_dir" "$archive")
    metadata=$(printf '%s\n' "$pair" | sed -n '2p')
    target=$(sed -n 's:.*<target> *\([^<]*\) *</target>.*:\1:p' "$metadata")
    test -n "$target"
    filename=$(basename "$archive")
    package_name="xgrib_pi-0.1.0.1-${target}-tarball"
    metadata_name="xgrib_pi-0.1.0.1-${target}-metadata"
    staged_xml="$stage/$(basename "$metadata")"
    sed -e "s|--pkg_repo--|$repo|g" \
        -e "s|--name--|$package_name|g" \
        -e "s|--version--|$version|g" \
        -e "s|--filename--|$filename|g" \
        "$metadata" >"$staged_xml"

    cloudsmith push raw --republish --no-wait-for-sync \
      --name "$metadata_name" --version "$version" \
      --summary "xGRIB OpenCPN Alpha metadata for $target" \
      "$repo" "$staged_xml"
    cloudsmith push raw --republish --no-wait-for-sync \
      --name "$package_name" --version "$version" \
      --summary "xGRIB OpenCPN Alpha package for $target" \
      "$repo" "$archive"
  done
