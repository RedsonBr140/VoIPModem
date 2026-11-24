# VoIPModem

[![CI](https://github.com/RedsonBr140/VoIPModem/actions/workflows/ci.yml/badge.svg)](https://github.com/RedsonBr140/VoIPModem/actions/workflows/ci.yml)

A modem bank in software for Windows. Each line is a Hayes-compatible
modem on a (virtual) COM port on one side and a SIP extension on the
other. When a call comes in or goes out, the call's G.711 audio runs
through a real softmodem,
[libv90modem](https://github.com/georgica/libv90modem): V.8, then V.21
up to V.34 and V.90/V.92, with V.42 error control and V.42bis/V.44
compression. The resulting data stream is what the COM port carries, so
Windows' own modem driver and RAS can use the line like a hardware modem.

- `docs/PROJECT_SPEC.md`: requirements and goals.
- `docs/ARCHITECTURE.md`: how it works.
- `docs/ROADMAP.md`: phase status and test results.

## Status

Serial/Hayes command mode, SIP calls (both directions, multiple lines)
and RTP audio are complete and verified against Asterisk. Real modem data
links work: answering, VoIPModem connected at **V.90 56000/26400** with
V.42 LAPM and V.44 compression; calling, at **V.34 31200/14400** and V.21.
Those tests used a SIP software modem (BinModem) as the far end. A
physical analog modem through the FXS gateway has not been tried yet,
and V.92 does not connect yet. RAS/PPP (Phase 5) is not started. Details
in `docs/ROADMAP.md`.

## Licence

VoIPModem links libv90modem, which is GPL-2.0-only, and PJSIP (GPL-2.0
or later). A distributed VoIPModem binary is therefore under GPL-2.0.

## Build

Requires Visual Studio with the C++ desktop workload (MSVC toolset
`v145`, or change `PlatformToolset` in the `.vcxproj` files to yours).
Only `x64` is configured.

**One-time setup** (PJSIP is built from source with its own solution):

```powershell
git submodule update --init --recursive
.\scripts\setup-pjproject.ps1
msbuild third_party\pjproject\pjproject-vs14.sln /t:libpjproject /p:Configuration=Debug-Static /p:Platform=x64 /p:PlatformToolset=v145
msbuild third_party\pjproject\pjproject-vs14.sln /t:libpjproject /p:Configuration=Release-Static /p:Platform=x64 /p:PlatformToolset=v145
```

**Then build VoIPModem** (this also builds libv90modem, which is a
project in the same solution):

```
msbuild VoIPModem.sln /p:Configuration=Debug /p:Platform=x64
```

## Run

1. Install com0com and create one port pair per line. VoIPModem opens
   one side; bind Windows' "(Standard Modem Types)" driver to the other
   side (Device Manager, Add legacy hardware, Modem). See
   `docs/ARCHITECTURE.md` "Windows integration".
2. Copy `config/voipmodem.ini.sample` to `voipmodem.ini` next to
   `VoIPModem.exe` (or pass its path as the first argument). Set each
   line's `ComPort`, and `[SIP] LocalPort` plus each line's `SipServer`,
   `SipExtension` and `SipPassword`. **Never commit real credentials**:
   `voipmodem.ini` is gitignored for that reason.
3. Run `VoIPModem.exe [path-to-ini]`. Stop with Ctrl+C.
4. Use the line from the modem-driver side: RAS, or a terminal on the
   paired COM port. `ATDT<extension>` dials; `ATA` or `ATS0=<n>` answers.
   `CONNECT <rate>` means the modem link is up; after that the port
   carries data. `+++` (with a second of silence before and after)
   returns to command mode, and `ATH` hangs up.

To see what a call is doing, set `[Logging] Level=Debug`: the log then
has the SIP trace, the modem's connect line (mode, rates, SNR, round
trip) and the V.42 state.

## Test

```
msbuild tests\VoIPModem.Tests.vcxproj /p:Configuration=Debug /p:Platform=x64
x64\Debug\VoIPModem.Tests.exe
```

A small hand-rolled framework (`tests/TestFramework.h`), no external
dependency. It covers the Hayes parser, the state machine (with a fake
call controller), config loading, and the MSVC build of libv90modem (two
modems connected back to back). Calls through real SIP, and links with
real modems, are tested by hand; `docs/ROADMAP.md` records what has been.

## Continuous integration

`.github/workflows/ci.yml` builds Debug and Release on GitHub's
`windows-2022` runner and runs the tests on every push and pull request;
the Release binary is kept as an artifact. The runner has Visual Studio
2022, so CI overrides the projects' `v145` toolset with `v143`. pjproject's
build is cached by submodule commit, toolset, configuration and
`config_site.h`. Pushing a `v*` tag runs `release.yml`, which publishes a
GitHub release with a zip of the binary, the sample config and the
licence texts of libv90modem and PJSIP. Dependabot proposes weekly
updates to the actions and both submodules.
