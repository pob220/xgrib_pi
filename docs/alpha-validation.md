# Alpha validation

This is the reproducible, no-cost validation route for xGRIB 0.2.0.0. It keeps
ordinary validation separate from publication. No Cloudsmith credential is
needed to build or test any target.

## Supported validation matrix

The declared minimum is OpenCPN `ov511` with plugin API 1.21 and wxWidgets 3.2.
The local runtime baseline is OpenCPN 5.14 and the local OpenCPN 5.15
development build. The CI packaging matrix is:

| Target | Architecture | Executor |
| --- | --- | --- |
| Arch Linux rolling | x86_64 | local native |
| Debian 12 and 13 | x86_64 | clean containers |
| Ubuntu 22.04 and 24.04 | x86_64 | clean containers |
| Debian 12 | arm64 | CircleCI native `arm.medium` |
| Flatpak 25.08 | x86_64 and aarch64 | CircleCI machine executors |
| Windows Server 2022 | x86 plugin, x86_64 helper | genuine CircleCI Windows executor |
| macOS / Xcode 16.4 | Apple Silicon arm64 | genuine CircleCI M4 executor |

An Intel macOS runtime is not available in the current free hosted executor.
Do not describe an Apple-Silicon build or cross-build as Intel runtime testing.
A physical Raspberry Pi smoke test remains recommended after the native ARM64
CI package passes.

OpenCPN's checked-in API 1.21 MSVC import library is 32-bit COFF and the
catalogue target is `msvc-x86`. A genuine x64 build was attempted and reached
the final plugin link, where the linker correctly rejected the official x86
import library. Windows x64 is therefore not a supported OpenCPN plugin target
for this matrix; the genuine hosted validation builds the supported x86 ABI.
Current ecCodes explicitly supports only 64-bit platforms. Because the
generator is already isolated from OpenCPN behind a process boundary, Windows
CI builds the in-process plugin and viewer for x86 and the generator plus its
scientific dependencies for x86_64. The package carries both native ABIs and
the dialog launches the 64-bit helper with its private ecCodes and PROJ data.
It also carries the small x86 vcpkg DLL set required by the in-process plugin,
so plugin loading does not depend on an incidental DLL in the host install.
No unsupported dependency build or cross-ABI in-process linking is used.

## Local Arch build and functional test

Install the dependencies listed in `README.md`, then use an out-of-tree build:

```sh
cmake -S . -B build-alpha-arch -DCMAKE_BUILD_TYPE=Release \
  -DBUNDLE_GENERATOR_RUNTIME=ON
cmake --build build-alpha-arch --parallel
ctest --test-dir build-alpha-arch --output-on-failure
scripts/run-functional-merge-test.sh build-alpha-arch artifacts/arch-x86_64
cmake --build build-alpha-arch --target package
scripts/test-catalogue-archive.sh build-alpha-arch
```

For the normal before-push check, prefer the clean wrapper which performs all
of these steps and refuses stale package ambiguity:

```sh
scripts/validate-before-push.sh
```

Use `--keep-build` to retain its temporary build, or `--build-dir DIR` to use a
new explicit directory. Existing non-empty directories are rejected on
purpose. `CMakePresets.json` names the supported native configurations;
`cmake --list-presets=all` validates and lists those available on the host.
The generator submodule has the matching `windows-helper-x64-release` preset.
If the ordinary `build` tree already contains the checksum-verified pinned
Jasper source, preflight reuses that source cache while keeping the new binary
tree clean. A different verified source cache can be selected with
`XGRIB_PREFLIGHT_JASPER_SOURCE`; no compiled objects are reused.

`run-functional-merge-test.sh` generates the full deterministic fixture set,
runs the production merge API through its CLI, checks structured JSON, reopens
the result through xGRIB's production reader and records checksums. Canonical
small inputs are retained in `test/fixtures` so packaged runtimes can be tested
without compiling the fixture generator first.

## Clean Linux container matrix

The CircleCI Linux job and local Docker tests use the same entry point:

```sh
docker build --build-arg BASE_IMAGE=debian:bookworm \
  -f ci/Dockerfile.linux -t xgrib-bookworm ci
docker run --rm -e BUILD_ENV=debian -e OCPN_TARGET=bookworm \
  -v "$PWD:/src:ro" -v "$PWD/container-output:/work" \
  xgrib-bookworm /src/ci/build-linux-catalogue.sh
```

Change the base image and target to `debian:trixie`, `ubuntu:24.04`/`noble`,
or use `ci/Dockerfile.jammy` for Ubuntu 22.04. Every run configures from clean
state, runs CTest and the merge verifier, stages the package, executes the
staged helper, validates the archive and writes `result.json`.

## GUI and package runtime checks

Never use the daily OpenCPN profile for automated testing. Create a disposable
configuration/data directory under `/tmp`, point OpenCPN's `--configdir`, XDG
paths and plugin search path at it, and use a private Xvfb display when testing
headlessly. Capture the initial xGRIB window, each selected Unicode/space path,
merge success and combined-file reopen. Inspect `opencpn.log` after a clean
plugin unload. A 1920x1080 display at 96 DPI is the baseline; add 192 DPI where
the environment supports it.

The smoke hooks `XGRIB_TEST_OPEN_GENERATOR`, `XGRIB_TEST_WEATHER_FILE`,
`XGRIB_TEST_CURRENT_FILE`, `XGRIB_TEST_OUTPUT_FILE`,
`XGRIB_TEST_PLUGIN_DATA_DIR` and `XGRIB_TEST_PRIVATE_DATA_DIR` exercise the
normal production dialog and merge service only in explicitly opted-in test
processes. Selected paths are readonly controls and update on file-picker
acceptance, before merge begins.

## CircleCI

The default `validate` workflow runs all hosted targets and retains packages,
XML, checksums, JUnit, structured merge results and logs as artifacts. JUnit
files are copied to a dedicated `test-results` directory before CircleCI
ingestion so OpenCPN plugin metadata XML is never parsed as a test report. Rerun
one target using its job rerun control. Rerun the complete matrix by triggering
a pipeline with `run_workflow_deploy=false` (the default). Add a platform by
extending the parameterized `linux-catalogue` or `flatpak` job, or by adding a
genuine native executor job with the same artifact/result contract.

The branch `windows-focused-validation` is deliberately excluded from the
normal matrix. It runs `windows-x86` followed by the separate
`windows-opencpn-runtime` job, using the checksum-keyed dependency cache. Use
it for bounded Windows-only diagnosis without rebuilding already validated
Linux, ARM, Flatpak or macOS targets. The build job retains a
`build-and-package-only` result and passes the package through a CircleCI
workspace. The runtime job extracts the checksum-pinned official OpenCPN
5.14.0 NSIS release into a disposable directory without UAC or registry
writes, installs xGRIB into that copy, keeps bundled GRIB disabled, opens the
dialog through its normal smoke hooks, invokes the GUI Generate button using
Windows UI Automation, observes or logs the native helper PID, validates and
reopens the output, and retains the OpenCPN log, process record and screenshots.
Only the runtime job can upgrade the target result to `fully-tested`.

This public repository must remain on CircleCI's Free plan. Do not add payment
details or automatic credit refills. Check **Plan > Plan Usage** before broad
reruns. Use targeted reruns for platform-only fixes and a full matrix after
shared production changes.

Classify failures by their first retained failing log: configure
(dependency/toolchain), build (source portability), CTest/merge (functional),
stage/archive (runtime bundling or packaging), metadata (target naming), or
OpenCPN log (discovery/ABI/runtime loading). Never suppress a valid test merely
to make the workflow green.

The Windows job bootstraps the pinned official vcpkg `2026.06.24` checkout if
the executor image does not provide vcpkg. A checksum-keyed CircleCI cache
retains only vcpkg's reusable binary packages, reducing later Windows compute
without caching mutable source or credentials. Custom x86 and x86_64 triplets
build Release dependencies only: the Release-only xGRIB validation never uses
vcpkg's Debug libraries, and omitting them keeps a clean job within the free
executor's one-hour limit. The full generator dependency set is x86_64; the
x86 plugin dependency set contains only its viewer-side libraries. A bounded
dependency-preparation job saves that Release binary cache first; the
dependent validation job restores it before building, testing and packaging
xGRIB. This prevents a clean dependency build from consuming the validation
job's one-hour allowance. Initial installs use bounded resume-safe retries for
transient upstream archive rate limits, retaining every attempt log. The
pinned `libaec` vcpkg overlay uses DKRZ's official GitHub release archive
because its GitLab archive endpoint rate-limits CircleCI's Windows executor;
the archive is locked by SHA-512. The macOS job smoke-tests
Homebrew's `msgfmt` against the real Traditional Chinese catalogue and rebuilds
gettext from its formula source only if the installed Apple-Silicon bottle
crashes. Flatpak manifests use the canonical public repository and pin the
exact CircleCI commit instead of a moving branch.

## Change-safety rules

Keep these invariants when changing or updating source, dependencies or CI:

- The OpenCPN Windows plugin is x86 and the environmental helper is x86_64.
  Never link ecCodes or another 64-bit library into `xgrib_pi.dll`; communicate
  with the helper only through the existing job/result/process boundary.
- Keep the x64 MSVC and OpenMP redistributable DLLs app-local beside the helper.
  The vcpkg Windows ecCodes port enables OpenMP and therefore imports
  `VCOMP140.DLL`. Exercise the staged helper with a PATH containing only its
  own directory and Windows system directories so Visual Studio cannot mask a
  missing package dependency.
- MSVC plugin targets must compile with `MAKING_PLUGIN` so OpenCPN API symbols
  are imported from the host. Keep the architecture and dependency `dumpbin`
  checks; resolve Visual Studio tools using `vswhere`, not an assumed `PATH`.
- Treat file paths as Unicode end to end. Keep the UTF-8 Windows manifest on
  the helper and generator tests, use native/wide file APIs at OS boundaries,
  and retain fixture coverage for spaces and non-ASCII characters.
- Quote child-process arguments with `xgrib::QuoteProcessArgument`; POSIX
  single quotes are not valid Windows `CreateProcess` quoting. Keep the test
  which copies and launches itself from a path containing spaces and Unicode.
- Close GRIB/NetCDF readers before deleting or replacing their files. POSIX
  permits deleting an open file but Windows normally does not; deferred
  cleanup must remain covered by the Windows generator tests.
- Every standalone executable which constructs wxWidgets configuration or UI
  objects must initialize wxWidgets explicitly. Test executables also need the
  matching wx runtime directory on `PATH`; the package itself must obtain its
  DLLs from its declared runtime layout, not the developer machine.
- Preserve forward slashes in gettext source lists such as `po/POTFILES.in`.
  Exercise selected-path updates through the production file-picker handlers,
  and keep the readonly visible-path contract test.
- Visual Studio is a multi-configuration generator. Configure once but always
  build, test and install using `--config Release`; do not infer failure from
  `CMAKE_BUILD_TYPE` output alone.
- Jasper checks for GCC/Clang sanitizers, Unix headers, `ssize_t` and optional
  C features during MSVC configuration. A reported probe failure is benign
  when the subsequent fallback configuration and build succeed. Compiler or
  linker errors, CTest failures and missing package/runtime files are not.
- Parse every nested PowerShell script before expensive work. Pin downloaded
  archives by checksum, retain attempt logs, use bounded retry only for known
  transient failures, and do not add unused package-manager dependencies.
- Resolve a package archive and its metadata as one unambiguous pair. Prefer a
  same-basename XML, but permit Frontend2's distinct Flatpak metadata filename
  only when it is the sole same-version `xgrib_pi-*.xml` candidate. Clean jobs
  must produce exactly one archive; a local directory containing stale
  archives must fail unless the intended archive is passed explicitly. Never
  use `find ... -print -quit` to choose a release or deployment input.
- Keep CircleCI JUnit ingestion separate from retained package metadata. Only
  `ctest.xml` and `*-ctest.xml` belong in `test-results`; the complete artifact
  tree still belongs in CircleCI artifact storage.
- A Windows-only CI/runtime change uses the focused branch first. A shared C++
  source, CMake, dependency, packaging or metadata change requires the full
  Linux, ARM, Flatpak, Windows and macOS validation matrix after the focused
  defect is resolved. Documentation-only changes do not justify hosted reruns.
- Automated GUI work must use disposable OpenCPN data and profile directories.
  Never point CI or smoke hooks at the daily OpenCPN 5.15 profile.
- On Windows, invoke wx controls through the UI Automation pattern they expose.
  Do not call `SetFocus()` unless `IsKeyboardFocusable` is true; the xGRIB
  Generate button supports `InvokePattern` but rejects keyboard focus on the
  CircleCI desktop.
- Treat modal success UI as a synchronization boundary: inspect and close the
  dialog before requiring newly emitted lines in `opencpn.log`, then verify the
  flushed reopen record after clean OpenCPN shutdown.
- Match OpenCPN plugin-load evidence semantically and against `xgrib_pi.dll`;
  supported 5.14 builds use both `Loading PlugIn` and `Initializing PlugIn`.
- Retain OpenCPN's original native frame handle for automated shutdown and post
  `WM_CLOSE` to that exact HWND. Once xGRIB opens a top-level dialog,
  `Process.CloseMainWindow()` may target the plugin dialog, while the OpenCPN
  frame does not expose UI Automation `WindowPattern`; both are unsuitable as
  the primary close mechanism.
- Close every helper job/result file before deleting it. POSIX permits unlinking
  an open file, but Windows reports sharing error 32 and leaves the temporary
  file behind.
- Keep Windows host shutdown separate from functional status. If the CircleCI
  desktop cannot gracefully close OpenCPN after every retained UI, merge,
  reopen and log check has passed, force cleanup and classify the target
  `runtime-tested`; never promote it to `fully-tested`.

Before pushing any source change, run `scripts/validate-before-push.sh` (or the
Windows counterpart). It includes `git diff --check`, a clean configure, the
complete local CTest suite, deterministic merge/reopen, staged-helper and exact
archive validation.
Do not remove a contract because a platform fails; fix the portability or
runtime assumption and retain the failure evidence.

## Results and classifications

Evidence uses `artifacts/<target>/{package,logs,tests,screenshots}` plus
`environment.json` and `result.json`. Use only `fully-tested`, `runtime-tested`,
`build-and-package-only`, `build-only`, `not-run`, or `blocked`.
`fully-tested` requires installation, discovery, load, visible UI, path checks,
deterministic merge, verified reopen, screenshots and clean logs.

## Alpha publication gate

`run_workflow_deploy` defaults to `false`. When explicitly set to `true`, a
separate workflow first rebuilds every target, then stops at the
`hold-for-alpha-approval` manual approval job. Only `deploy-alpha` can access
the restricted `xgrib-deployment` context and upload to `opencpn/xgrib-alpha`.
Ordinary branch/tag builds cannot deploy.

The later deployment context needs only:

- `CLOUDSMITH_API_KEY`: Cloudsmith upload token; required by `deploy-alpha`,
  never by validation.

Create it in **Organization Settings > Contexts** as the restricted context
`xgrib-deployment`. Do not put its value in source, commands, logs or issue
comments. Catalogue PR creation, releases and deployment remain separate,
explicitly authorised operations.
