# VoIPModem

A Windows-first software modem that emulates a Hayes-compatible serial
modem and transports modem calls over SIP/VoIP. See
`docs/PROJECT_SPEC.md` for the full specification, `docs/ARCHITECTURE.md`
for what is actually implemented, and `docs/ROADMAP.md` for phase status.

**Current status: Phase 1 (serial/Hayes command skeleton) complete;
Phase 2 (SIP call control) mostly verified** — outgoing calls and SIP
registration are confirmed against a real Asterisk server, but the
incoming-call path (RING/ATA/auto-answer) is unit-tested only so far, not
yet verified against a real incoming call. There is no RTP/audio or
modem DSP implementation yet (Phases 3/4) — a connected call has
signaling only, no media.

## Build

Requires Visual Studio with the C++ desktop workload (MSVC toolset
`v145` or adjust `PlatformToolset` in the `.vcxproj` files to match what
you have installed).

**One-time setup** (PJSIP, vendored as a git submodule and built from
source — see `docs/ARCHITECTURE.md` "SIP integration (Phase 2)"):

```powershell
git submodule update --init --recursive
.\scripts\setup-pjproject.ps1
msbuild third_party\pjproject\pjproject-vs14.sln /t:libpjproject /p:Configuration=Debug-Static /p:Platform=x64 /p:PlatformToolset=v145
msbuild third_party\pjproject\pjproject-vs14.sln /t:libpjproject /p:Configuration=Release-Static /p:Platform=x64 /p:PlatformToolset=v145
```

This only needs to be done once (or again after updating the submodule).
It takes a few minutes — it's building pjlib, pjmedia, pjnath, pjsip,
pjsua2, and several bundled codec libraries from source into one
combined `libpjproject-*.lib` per configuration.

**Then build VoIPModem itself:**

```
msbuild VoIPModem.sln /p:Configuration=Debug /p:Platform=x64
```

Or open `VoIPModem.sln` in Visual Studio and build normally. Only
`x64` is configured; there is no Win32 configuration.

## Run

1. Copy `config/voipmodem.ini.sample` to `voipmodem.ini` (next to the
   built `VoIPModem.exe`, or anywhere, and pass its path as the first
   command-line argument). Set `ComPort` to a port you actually have —
   a com0com virtual pair for development, per `docs/PROJECT_SPEC.md`
   section 3. One `[Modem]` section runs a single line; replace it with
   one `[Modem.<name>]` section per line to run several modems at once,
   each on its own COM port and its own thread (see
   `docs/ARCHITECTURE.md` "Multiple modem instances").
2. To actually place/receive calls, set `[SIP] LocalPort` and each
   instance's `SipServer`/`SipExtension`/`SipPassword` to your own
   Asterisk details (see `docs/ARCHITECTURE.md` "SIP integration (Phase
   2)"). Leave `SipServer` empty to keep an instance on the no-op
   `NullCallController` (`ATD`/`ATA` honestly fail) for pure serial/Hayes
   testing without a SIP server. **Never commit real credentials** —
   `voipmodem.ini` itself is gitignored for exactly that reason; only
   edit your own local copy, not `config/voipmodem.ini.sample`.
3. Run `VoIPModem.exe [path-to-ini]`.
4. Open the other side of each COM pair in a terminal emulator and type
   `AT`, `ATI`, `ATZ`, `ATD<number>`, etc.
5. Stop with Ctrl+C.

## Test

```
msbuild tests\VoIPModem.Tests.vcxproj /p:Configuration=Debug /p:Platform=x64
x64\Debug\VoIPModem.Tests.exe
```

Tests use a small hand-rolled framework (`tests/TestFramework.h`) rather
than an external dependency — it covers the Hayes parser, the modem
state machine (including the async call-event handling SIP needs), and
config loading with fakes/doubles, not a live SIP server. Testing the
real `PjsipCallController` against Asterisk is manual (see
`docs/ROADMAP.md` for what has and hasn't been verified that way).
