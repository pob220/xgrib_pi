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
artifact_root=${XGRIB_DEPLOY_ARTIFACT_ROOT:-artifacts}
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT HUP INT TERM

find "$artifact_root" -mindepth 3 -maxdepth 3 -type f -path '*/package/*.tar.gz' \
  -print0 | while IFS= read -r -d '' archive; do
    package_dir=$(dirname "$archive")
    pair=$(scripts/resolve-package-pair.sh "$package_dir" "$archive")
    metadata=$(printf '%s\n' "$pair" | sed -n '2p')
    target=$(sed -n 's:.*<target>[[:space:]]*\([^[:space:]<]*\)[[:space:]]*</target>.*:\1:p' "$metadata")
    target_version=$(sed -n 's:.*<target-version>[[:space:]]*\([^[:space:]<]*\)[[:space:]]*</target-version>.*:\1:p' "$metadata")
    plugin_version=$(sed -n 's:.*<version>[[:space:]]*\([^[:space:]<]*\)[[:space:]]*</version>.*:\1:p' "$metadata")
    test -n "$target"
    test -n "$target_version"
    if [[ ! "$plugin_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
      echo "Invalid or missing plugin version in $metadata" >&2
      exit 1
    fi
    cloudsmith_version="${plugin_version}+${CIRCLE_BUILD_NUM:-0}.$(git rev-parse --short=7 HEAD)"
    filename=$(basename "$archive")
    package_name="xgrib_pi-${plugin_version}-${target}-${target_version}-tarball"
    metadata_name="xgrib_pi-${plugin_version}-${target}-${target_version}-metadata"
    staged_xml="$stage/$(basename "$metadata")"
    sed -e "s|--pkg_repo--|$repo|g" \
        -e "s|--name--|$package_name|g" \
        -e "s|--version--|$cloudsmith_version|g" \
        -e "s|--filename--|$filename|g" \
        "$metadata" >"$staged_xml"

    cloudsmith push raw --republish --no-wait-for-sync \
      --name "$metadata_name" --version "$cloudsmith_version" \
      --summary "xGRIB OpenCPN Alpha metadata for $target" \
      "$repo" "$staged_xml"
    cloudsmith push raw --republish --no-wait-for-sync \
      --name "$package_name" --version "$cloudsmith_version" \
      --summary "xGRIB OpenCPN Alpha package for $target" \
      "$repo" "$archive"
  done
