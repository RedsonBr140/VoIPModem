# VoIPModem

A Windows-first software modem that emulates a Hayes-compatible serial
modem and will eventually transport modem calls over SIP/VoIP. See
`docs/PROJECT_SPEC.md` for the full specification, `docs/ARCHITECTURE.md`
for what is actually implemented, and `docs/ROADMAP.md` for phase status.

**Current status: Phase 1 (serial/Hayes command skeleton) only.** There
is no SIP, RTP, or modem DSP implementation yet — `ATD`/`ATA` always
return `NO CARRIER`/`NO ANSWER`.

## Build

Requires Visual Studio with the C++ desktop workload (MSVC toolset
`v145` or adjust `PlatformToolset` in the `.vcxproj` files to match what
you have installed).

```
msbuild VoIPModem.sln /p:Configuration=Debug /p:Platform=x64
```

Or open `VoIPModem.sln` in Visual Studio and build normally. Only
`x64` is configured; there is no Win32 configuration.

## Run

1. Copy `config/voipmodem.ini.sample` to `voipmodem.ini` (next to the
   built `VoIPModem.exe`, or anywhere, and pass its path as the first
   command-line argument) and set `ComPort` to a port you actually have
   — a com0com virtual pair for development, per
   `docs/PROJECT_SPEC.md` section 3.
2. Run `VoIPModem.exe [path-to-ini]`.
3. Open the other side of the COM pair in a terminal emulator and type
   `AT`, `ATI`, `ATZ`, etc.
4. Stop with Ctrl+C.

## Test

```
msbuild tests\VoIPModem.Tests.vcxproj /p:Configuration=Debug /p:Platform=x64
x64\Debug\VoIPModem.Tests.exe
```

Tests use a small hand-rolled framework (`tests/TestFramework.h`) rather
than an external dependency, since the current test surface (the Hayes
parser and the modem state machine) doesn't need more than that.
