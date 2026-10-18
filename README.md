# VoIPModem

A Windows-first software modem that emulates a Hayes-compatible serial
modem and transports modem calls over SIP/VoIP. See
`docs/PROJECT_SPEC.md` for the full specification, `docs/ARCHITECTURE.md`
for what is actually implemented, and `docs/ROADMAP.md` for phase status.

**Current status: Phases 1-3 complete and verified live** against a
real Asterisk server — serial/Hayes command mode, SIP registration,
outgoing and incoming calls, RING, `ATA`, S0 auto-answer, and two
simultaneously registered extensions reaching `CONNECT`, are all
confirmed working. Phase 3 (RTP audio) is verified via a physical
analog line dialed into a registered extension: G.711-only codec
negotiation and modem-tone-preserving media settings (no VAD/echo
cancellation, native 8kHz) are in place, and call audio — recorded to a
WAV file and a looping test tone played back, rather than a live sound
device, since this dev machine has no microphone at the Windows API
level and pjsua2's device layer is duplex-only (see
`docs/ARCHITECTURE.md` "RTP audio (Phase 3)") — came through clean and
audible. **Phase 4 (modem DSP) is in progress, from scratch** — a V.21
FSK modulator/demodulator and a full ITU-T V.8 handshake (ANSam/CM/JM/
CJ) are implemented and unit-tested (including an in-process loopback
simulation that reaches a negotiated outcome end to end), and wired
into real call audio. **Live-tested between two real VoIPModem
instances over Asterisk**: reached a negotiated outcome on both sides
at least once, but **not reliably** — real audio jitter intermittently
breaks the handshake's receiver sync in ways the loopback simulation
never exercised, because this codebase still has no real symbol-timing/
clock recovery, just a one-shot sync heuristic. See `docs/ROADMAP.md`
Phase 4 for the live-test findings and what's next. There is still no
actual V.32bis/V.34
modulation (QAM, scrambler, equalizer, trellis coding) — V.8 only
negotiates which modulation to use next, it doesn't implement any of
them.

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
5. Once a call connects, both sides run the real ITU-T V.8 handshake
   over the call audio (ANSam, CM/JM, CJ) and log the negotiated
   outcome — each side currently only ever advertises V.21 (the one
   modulation actually implemented), not a real modem connection yet
   (see `docs/ARCHITECTURE.md` "Modem DSP (Phase 4)"). A single
   instance calling a plain phone cannot exercise this meaningfully —
   a phone never transmits V.8 back — so this needs two VoIPModem
   instances calling each other to actually negotiate anything.
6. Stop with Ctrl+C.

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
