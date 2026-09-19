# Windows x64 Preview build

xGRIB 0.2.5.7 adds a native Windows x64 CircleCI package alongside the existing
Windows x86 package. It is for the **OpenCPN Windows x64 Preview**, not for the
ordinary 32-bit OpenCPN installed on a 64-bit Windows machine.

The `windows-x64` job uses the same build/test script as x86, with an explicit
architecture selection. Both retain the isolated x64 environmental GRIB helper.
The x64 plugin uses wxWidgets 3.2.8 x64 and the native host import SDK recorded
in `ci/windows64-sdk.json`. The small SDK archive under `msvc/x64` includes its
source revision, licences and per-file checksums. CI checks archive and file
SHA256 values and AMD64 import-library headers before using it. The official
wxWidgets x64 downloads are also pinned by SHA256.

Package metadata uses the distinct `msvc-wx32-x64` target, Windows target version
`10`, and `x86_64` architecture. CI rejects incorrect architecture/metadata and
treats plugin pointer-truncation warnings as errors. Linux, macOS, Android and
the ordinary Windows x86 SDK selection are unaffected.

Both Android arm64 and Windows x64 run in the normal validation matrix and the
separately approval-gated release workflow. A normal push does **not** publish
packages. Android manual-import archives are retained separately from catalogue
archive/XML pairs to avoid duplicate or mismatched publication.

Build success and standalone tests are not an OpenCPN GUI test. The existing
Windows runtime job exercises only the x86 host; a native x64 host GUI test is
still needed before treating the new package as runtime-qualified.

Host SDK provenance:
https://github.com/pob220/OpenCPN-Chart-Aware/actions/runs/35443540536
