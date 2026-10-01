# Windows x64 testing build

For the ordinary x86 host, matching release source, RelWithDebInfo builds and
Generate troubleshooting, see [Windows Generate debugging](windows-generate-debugging.md).

xGRIB 0.3.3.0 builds a native Windows x64 package for the OpenCPN 5.14.2 x64
testing host. It is distinct from the Windows x86 package. The x64 OpenCPN
installer is currently distributed from the OpenCPN unstable repository; its
presence does not establish a stable x64 host release.

The CircleCI job downloads the pinned 5.14.2 installer and wxWidgets 3.2.9 x64
archives listed in `ci/windows64-sdk.json` and verifies their SHA256 hashes. It
extracts the host without installing it, generates an AMD64 import library from
the actual `opencpn.exe` export table, then verifies every OpenCPN import in
the built plugin against that same host. The environmental GRIB helper remains
x64 and is exercised in the standalone tests.

Package metadata uses the host's `msvc-64` target, Windows target version `10`,
`x86_64` architecture, and API 1.21 catalogue baseline. CI also compiles the
plugin against API 1.22 headers. Those compatibility jobs retain test artifacts
but only the API 1.21 packages enter the alpha publication workspace.

The x64 build, tests and host import check do not exercise the OpenCPN GUI.
The separate Windows GUI runtime job currently tests the x86 host and package.
