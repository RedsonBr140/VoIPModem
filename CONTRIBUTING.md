# Contributing to VoIPModem

How to build, test and release VoIPModem as a developer. The README
covers building and running it as a user; read `docs/PROJECT_SPEC.md`
(requirements), `docs/ARCHITECTURE.md` (how it works) and
`docs/ROADMAP.md` (what is done and tested) before changing it, and
`docs/LINUX.md` for anything Linux-specific.

## Build details

### Windows

- `VoIPModem.sln` builds the service, the Manager, the tests and
  libv90modem (`third_party/libv90modem-build/libv90modem.vcxproj`, an
  MSVC build of the submodule).
- PJSIP is built from source with its own solution, once per platform
  and configuration you need (`Release-Static`, or `Debug-Static` for
  Debug builds). `scripts/setup-pjproject.ps1` first copies
  `third_party/pjsip-config/config_site.h` into the pjproject tree.
- Output goes to `<Platform>\<Configuration>\` at the repository root,
  e.g. `Win32\Release\VoIPModem.exe`.
- Shared build settings (output folders, PJSIP paths and library names)
  live in `build/VoIPModem.props`, which every project imports. The
  toolset per configuration is in `build/VoIPModem.Toolset.props`:
  `v145` for the modern builds (Windows Vista or later), `v141_xp` for
  `Debug-XP` and `Release-XP`.
- The XP configurations build against a separate PJSIP checkout,
  `third_party/pjproject-xp`; `scripts/build-xp.ps1` builds it, then the
  solution (`-SkipPjproject` skips PJSIP on later runs).

### Linux

- `CMakeLists.txt` is for Linux only and stops with an error on Windows.
- It builds PJSIP from the `third_party/pjproject` submodule with
  pjproject's own CMake build and our `config_site.h`, and libv90modem
  with its own `Makefile`. Targets: `voipmodem`, `voipmodem_lib` and
  `voipmodem_tests`.
- Details in `docs/LINUX.md`, "Building".

## Tests

Windows:

```
msbuild tests\VoIPModem.Tests.vcxproj /p:Configuration=Debug /p:Platform=x64
x64\Debug\VoIPModem.Tests.exe
```

Linux, after a CMake build:

```sh
ctest --test-dir build-linux --output-on-failure
```

A small hand-rolled framework (`tests/TestFramework.h`), no external
dependency. It covers the Hayes parser, the state machine (with a fake
call controller), config loading and the ini writer, the control API
server (over a real loopback socket, password included), the call
statistics, com0com pairing, and libv90modem (two modems connected back
to back). On Linux the same tests run, plus the PTY serial port's
(`tests/PtySerialPortTests.cpp`).

Calls through real SIP, links with real modems, and the Manager's window
are tested by hand; `docs/ROADMAP.md` and `docs/LINUX.md` record what
has been.

## Continuous integration

`.github/workflows/ci.yml` runs on every push and pull request:

- **Windows**: Debug and Release for x64 and Win32 on GitHub's
  `windows-2022` runner, with the tests; the Release binaries are kept as
  artifacts. The runner has Visual Studio 2022, so CI overrides the
  projects' `v145` toolset with `v143`. pjproject's build is cached by
  submodule commit, toolset, configuration and `config_site.h`.
- **Linux**: Debug and Release with CMake on `ubuntu-24.04`, with the
  tests.

The XP configurations are not built in CI.

## Releases

Pushing a `v*` tag runs `.github/workflows/release.yml`, which publishes
a GitHub release with a zip per Windows platform (`win-x64`, `win-x86`).
Each has both programs, the sample config, the README and the licence
texts of libv90modem, PJSIP and JSON for Modern C++. There is no Linux
release package yet.

Dependabot proposes weekly updates to the actions and the submodules.
libv90modem's API is not stable before 1.0, so submodule updates are
reviewed and tested live, never merged on CI alone.
