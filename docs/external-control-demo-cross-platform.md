# External Control Demo cross-platform build

This branch rebuilds the xGRIB component used by the OpenCPN External Control
Demo on the native target platforms. The provider implementation is unchanged
from commit `6674c70583d285cdcbc622f3377da810fde0d3ba`; this documentation-only child
commit exists to trigger a fresh validation matrix after the exact pinned
generator revision became publicly fetchable again.

The first practical target is Debian 12 ARM64 for Raspberry Pi 4 and newer.
The existing validation matrix also characterises Debian and Ubuntu x86_64,
Windows, macOS and Flatpak packages. Publication of any resulting binary still
requires the complete demo bundle's architecture, isolation and API-scope
qualification.

