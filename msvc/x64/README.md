# Native OpenCPN x64 import SDK

`OpenCPN-Windows-x64-plugin-sdk-94cf6909.zip` is the unchanged, verified SDK
export from the OpenCPN Windows x64 Preview build also used by xWeatherRouting.
Provenance and its SHA256 are recorded in `ci/windows64-sdk.json`. The archive
includes OpenCPN/zlib licences and a per-file checksum manifest.

Only the native `lib/opencpn.lib` is used here. Other plugin/helper dependencies
come from the pinned Windows dependency recipe. Do not substitute an x86 import
library or relabel an x86 plugin as x64. See `docs/windows-x64.md`.
