# Alpha validation

This is the reproducible, no-cost validation route for xGRIB 0.1.0.1. It keeps
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
| Windows Server 2022 | x86_64 | genuine CircleCI Windows executor |
| macOS / Xcode 16.4 | Apple Silicon arm64 | genuine CircleCI M4 executor |

An Intel macOS runtime is not available in the current free hosted executor.
Do not describe an Apple-Silicon build or cross-build as Intel runtime testing.
A physical Raspberry Pi smoke test remains recommended after the native ARM64
CI package passes.

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
XML, checksums, JUnit, structured merge results and logs as artifacts. Rerun
one target using its job rerun control. Rerun the complete matrix by triggering
a pipeline with `run_workflow_deploy=false` (the default). Add a platform by
extending the parameterized `linux-catalogue` or `flatpak` job, or by adding a
genuine native executor job with the same artifact/result contract.

This public repository must remain on CircleCI's Free plan. Do not add payment
details or automatic credit refills. Check **Plan > Plan Usage** before broad
reruns. Use targeted reruns for platform-only fixes and a full matrix after
shared production changes.

Classify failures by their first retained failing log: configure
(dependency/toolchain), build (source portability), CTest/merge (functional),
stage/archive (runtime bundling or packaging), metadata (target naming), or
OpenCPN log (discovery/ABI/runtime loading). Never suppress a valid test merely
to make the workflow green.

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
