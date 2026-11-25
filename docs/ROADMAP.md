# VoIPModem — Roadmap Status

Progress against the six phases in `PROJECT_SPEC.md` section 6. A phase
is complete only when its acceptance criterion has been exercised, not
just coded. `ARCHITECTURE.md` describes how each part works.

| Phase | Status |
|---|---|
| 1. Serial modem skeleton | **Complete**, verified with a terminal emulator |
| 2. SIP call control | **Complete**, verified against Asterisk |
| 3. RTP audio | **Complete**, verified live; media path retuned for modems in Phase 4 |
| 4. Modem interoperability | **In progress**: up to V.90 with a SIP softmodem; a physical modem through an FXS gateway connects at up to V.34 (V.90 start-up fails) |
| 5. Windows networking (RAS/PPP) | **Started**: RRAS on Server 2022 answered a physical modem; PPP session, internet through NAT, with workarounds; legacy client not yet tested |
| 6. Compatibility and optimization | **Started**: x64 + Win32 builds; XP Win32 build runs on 32-bit XP (V.34 call live); XP x64 / Server 2003 not yet built; Windows service + control API + VoIPModem Manager built |

## Phase 1 — Serial modem skeleton — complete

*Criterion: the application behaves like a basic Hayes-compatible modem
in command mode.*

- `VoIPModemLib` with separate Core, Hayes, Serial, Config and Logging
  modules; `VoIPModem` (app) and `VoIPModem.Tests`.
- INI configuration; one or more modem lines (`[Modem]` or
  `[Modem.<name>]`), each with its own COM port and thread.
- `AT`, `ATI`, `ATZ`, `ATE`, `ATV`, `ATQ`, `ATS<n>=`/`?`,
  `ATD`/`ATDT`/`ATDP`, `ATA`, `ATH`, `AT&F`, chained commands, and the
  V.25ter `+`/`#` extended-command grammar (`+FCLASS`, `+GMI`, `+GMM`,
  `+GMR` answered).
- Verified by hand through a com0com pair and a terminal emulator: echo,
  verbose/numeric/quiet results, S-register round trip, dial/answer/
  hang-up, chained commands, `ERROR` on invalid input, clean Ctrl+C.
  Two bugs found that way and fixed: `ATDT<n>` dialled `T<n>`; and
  Windows' "Query Modem" diagnostic failed because `+`/`#` commands were
  not parsed.
- Windows recognises the line as a modem with the "(Standard Modem
  Types)" driver bound to the other side of the com0com pair.

## Phase 2 — SIP call control — complete

*Criterion: incoming and outgoing SIP calls work reliably, with correct
serial responses.*

- PJSIP 2.17 as a submodule built from source; one process-wide endpoint,
  one registered account per line (`SipServer`, `SipExtension`,
  `SipPassword`, `SipRealm`).
- Asynchronous `ATD`/`ATA`/`ATH` over SIP INVITE/answer/BYE, `RING`
  cadence, S0 auto-answer, SIP status mapped to `BUSY`/`NO ANSWER`/
  `NO CARRIER`.
- Verified against the Asterisk test server: registration, outgoing and
  incoming calls, `ATA`, auto-answer, and two lines registered and in
  calls at the same time. One bug found live and fixed:
  `AutoAnswerRingCount` was parsed but never written to S0.

## Phase 3 — RTP audio — complete

*Criterion: bidirectional modem-like audio can pass through the VoIP path
without the application corrupting it.*

- G.711 only (PCMU/PCMA); every other codec disabled. 8 kHz throughout,
  VAD and echo cancellation off.
- Verified live: a physical analog line through the Cisco FXS gateway and
  Asterisk called a line, and audio passed cleanly both ways (a recorded
  WAV of the caller, and a test tone heard on the analog phone).
- Not every Phase 3 item was finished then: packet-loss/jitter
  diagnostics were not added, and the jitter buffer and PLC were left at
  PJSIP's voice defaults. Phase 4's live tests showed those defaults break
  a modem, and the media path was retuned (see Phase 4).

## Phase 4 — Actual modem interoperability — in progress

*Criterion: a physical client modem establishes a genuine data
connection, not merely an answered telephone call.*

### Done

- **Modem engine**: [libv90modem](https://github.com/georgica/libv90modem)
  0.6.0 (GPL-2.0-only), integrated as a submodule with an MSVC project,
  per the spec's "implement or integrate a suitable modem DSP engine" and
  CLAUDE.md's preference for established libraries. It provides V.8,
  V.21 through V.34, V.90/V.92 and V.42/V.42bis/V.44. Two unit tests
  connect two instances of the MSVC build back to back (V.21, V.34).
- **`CallAudioBridge`** runs one modem and one V.42 endpoint per call on
  the call's 20 ms frames, and moves bytes between them and the serial
  port.
- **Modem-level results**: `CONNECT <rate>` only when the modem reaches
  data mode; the call is hung up, with one `NO CARRIER`, when the modem or
  V.42 ends. `+++` escape to command mode during a call.
- **Media path retuned for modems** (found live, see below): deep fixed
  jitter buffer with no discarding, PLC off, 1 ms timer resolution.
- **Per-line `Modulations=`** setting.

### Live results (2026-11-24)

All calls went through the Asterisk test server. The far end was
**BinModem**, an independent software modem on its own SIP extension
(V.92 capable), not a physical modem.

| Direction | Offer | Result |
|---|---|---|
| VoIPModem calls | far end forced to V.21 | V.21 300 bit/s (pre-V.8 answer), no error control; text both ways |
| VoIPModem calls | V.34 and below | **V.34 31200/14400** via V.8, V.42 LAPM + V.44; text both ways |
| VoIPModem calls | full (V.92) | V.92 Phases 3/4 ran, then our side sent a cleardown (CPu, data rate 0). Not connected; cause unknown |
| VoIPModem answers | full (V.92) | **V.90 56000/26400**, SNR 38 dB, V.42 LAPM + V.44, stable; text received intact for the whole call |

Problems these calls found, each fixed before the next:

1. PJSIP's log never reached our log file (`consoleLevel` was 0).
2. As the answerer, V.42 detection was switched off when libv90modem
   reported `odp_bypass`, but BinModem runs detection on a V.90 call, so
   LAPM never started. Detection now always stays on when answering.
3. The jitter buffer ran empty every 1–3 s with PJSIP's voice defaults
   (no prefetch, progressive discard, PLC). The V.34 upstream lost lock
   at one of those moments, 3 s into data mode, and LAPM timed out
   (N400). With the media path retuned, the next call had no such events
   and stayed up.
4. A dead modem or V.42 link left the SIP call up; it now hangs up.

### Live results with a physical modem (2026-11-24)

The first calls from a physical analog modem: a US Robotics Courier 56K
on the PHONE 1 port of a Cisco SPA112 FXS gateway (firmware
1.4.1(SR5)), registered with Asterisk 20.12, G.711 u-law only. The
Courier called; VoIPModem 0.0.1 answered, under Windows Server 2022
RRAS (Phase 5). The SPA112's settings are in `WINDOWS_RAS_DIALIN.md`.

| Link | Result |
|---|---|
| **V.32bis 14400/14400** | Connected, stable |
| **V.34 31200/26400** | Connected, zero CRC and framing errors counted by RRAS |
| Default offer (V.92, V.90 first) | "libv90modem: modem ended -- start-up failed", every call |

So through the gateway, lines are limited to V.34 (`Modulations=`) for
now. This does not contradict the V.90 result above, which had no
analog leg. VoIPModem calling the physical modem has not been tried.

### Open

- **Physical analog modem**: the acceptance test. Calling into a line,
  it connects at up to V.34 (above). Still open: V.90/V.92 start-up
  through the gateway fails (cause not investigated), and VoIPModem
  calling the physical modem has not been tried.
- **V.92**: answering, calls settle at V.90. On one call (before the
  jitter-buffer fix) our side retrained with Tone B while BinModem was
  setting up its V.92 PCM upstream. Not yet known whether that was the
  bad audio or something else. Calling with V.92 on offer ends in our
  cleardown in Phase 4.
- **Calling an answerer that opens with V.8bis**: libv90modem waits a
  fixed 10 s for the answer tone; BinModem's V.8bis exchange takes about
  9.4 s before ANSam, so it was turned off on BinModem for our calls.
  Raising this needs an upstream API change (`tone_ms` is internal).
- **V.32bis 9600** (the spec's suggested first target) has not been
  tested live by itself; V.32bis connected at 14400 with the physical
  modem.
- **Error handling, disconnects and reconnection** under the criterion:
  clean far-end and near-end hang-ups have been seen; repeated
  back-to-back calls on one line and retrains mid-call have not been
  tested deliberately.
- **Call-audio recording** (both directions, our side) for diagnosis and
  for replay through `v90m-replay`.

## Phase 5 — Windows networking (RAS/PPP) — started

VoIPModem is a modem bank: Windows Server's own RAS terminates PPP
through the modem driver on the com0com port. No PPP code goes into
VoIPModem. The work is configuring RAS on those modem ports and proving a
full dial-up session (the legacy client dialling in and exchanging IP
traffic). The setup, step by step, is in `WINDOWS_RAS_DIALIN.md`.

### Verified (2026-11-24)

- Windows Server 2022 RRAS (dial-up access and NAT) on two VoIPModem
  lines, com0com 3.0.0.0 and the "Standard 56000 bps Modem" driver.
- The Courier 56K through the SPA112 (Phase 4) called from Windows 11
  dial-up networking. RRAS answered on `RING`, the port reached
  "Authenticated" (MS-CHAP v2), the client got an address from RRAS's
  static pool and reached the internet through NAT. The modem link was
  V.34 with V.42 on and V.42bis/V.44 off.
- Dial-in permission both per user and by group through a network
  policy (Windows group and NAS port type Async).

### Workarounds in that session

- **Unimodem init strings**: VoIPModem 0.0.1 answers `ERROR` to `&C1`,
  `&D2`, `&K3`, `N1` and `X4`, so those were removed from the driver's
  registry strings. VoIPModem 0.0.2 accepts them (stock strings not yet
  tested).
- **Compression off**: with V.42bis/V.44 on, PPP authenticated and then
  no more data arrived from the client, with no CRC errors. With
  `Compression=0` traffic flowed. Root cause not investigated.
- **Maximum rate V.34**: see Phase 4.

### Open

- The phase's criterion: a legacy client (Windows 9x, 2000, XP) dialling
  in and exchanging IP traffic. Only Windows 11 has dialled in.
- The V.42bis/V.44 data stall above.
- Hang-up takes about 20 s: DCD is not driven and DTR drops are not acted
  on, so Unimodem falls back to `+++` and `ATH`. VoIPModem 0.0.2 honors
  `&C`/`&D`; not yet confirmed against RRAS.
- "Query Modem" run while the line is ringing shows COMMAND NOT
  SUPPORTED for several identification commands.
- Not tested: Server 2003 or XP as the RAS host, Multilink PPP, more than
  two lines, PSTN callers through a SIP trunk, com0com 2.2.2.0 with Secure
  Boot on.

- **First RRAS session (Windows Server 2022)**: with the generic
  "Standard 56000 bps Modem" driver, RRAS closed the line because
  VoIPModem answered `ERROR` to `&C1`, `&D2`, `&K3`, `N1` and `X4` in the
  driver's init strings. With those removed from the driver's registry
  strings, RRAS answered, the modems trained at V.34 and a PPP session
  completed. VoIPModem now accepts those commands (see `ARCHITECTURE.md`,
  "Windows Unimodem init strings"), so the stock strings return `OK`; an
  RRAS call with the unmodified driver has not been repeated since.
- **DCD and DTR**: that session's modem log showed "CD is high while
  initializing modem", and each hang-up took about 20 s (DTR lowered, CD
  never dropped, then `+++` and `ATH`). `&C` and `&D` are now honored: DCD
  is on only while the data link is up (`&C1`) and a DTR drop hangs up
  (`&D2`); see `ARCHITECTURE.md`, "Serial control signals". Covered by
  unit tests; not yet confirmed against RRAS on a live system.
- Still open: the phase's criterion (the Windows 98 client dialling in
  and exchanging IP traffic) is not yet met.

## Phase 6 — Compatibility and optimization — started

Includes: more modems (physical and soft), Windows XP and Server 2003
deployment, a dedicated virtual COM driver if com0com proves
insufficient, and installation and troubleshooting documentation.

**Windows targets.** Targets are 32-bit and 64-bit Windows, including XP
and Server 2003, with separate binaries (and third-party library
versions) per Windows generation where needed.

- **Done: modern build, x64 and Win32.** Both build with no warnings and
  pass all tests (including libv90modem back to back) locally (v145)
  and in CI (v143). Their PE headers require Windows 6.0 (Vista), so
  XP (5.1) and Server 2003 (5.2) refuse to load them.
- **Done: XP build, Win32** (`Release-XP`, `scripts/build-xp.ps1`), with
  `v141_xp` from Visual Studio 2022 Build Tools (Windows 7.1A SDK):
  - PJSIP 2.17, unmodified, in its own checkout `third_party/pjproject-xp`
    (pjproject's library names ignore the toolset, so one tree can't hold
    both builds). One fix in our `config_site.h`: the 7.1A SDK has no
    `_WIN32_WINNT_WIN8`, which made pjlib's `#if PJ_WIN32_WINNT >=
    _WIN32_WINNT_WIN8` compare against 0 and pick Windows 8 code.
  - libv90modem in MSVC's default C mode (v141 has no C11 switch): compiles
    unchanged.
  - Strict conformance mode (`/permissive-`) is off for XP builds; the 7.1A
    SDK's `objbase.h` does not compile under it.
  - Verified on the development machine: no warnings, 85/85 tests pass
    (libv90modem back to back included); the PE header says Windows 5.01;
    imports only KERNEL32, WS2_32, ole32 and WINMM, whose one post-XP-RTM
    function (`GetLogicalProcessorInformation`, from the C runtime) exists
    from XP SP3. So: **needs XP SP3**, no redistributable.
  - **Verified live on 32-bit Windows XP** (VM, 2026-11-24): PJSIP
    started, the COM port opened, the line registered with Asterisk, and a
    modem call connected at **V.34**.
  - Seen on XP: PJSIP logged "Unsupported transport" when an INVITE was
    1300 bytes or more: it switches such requests to TCP, which VoIPModem
    does not open, then fell back to UDP. Fixed: TCP switching is now off
    (`disable_tcp_switch`), so requests stay on UDP.
- **Next**: the x64 XP build (XP x64 / Server 2003 x64, untested so far),
  Server 2003 itself, then CI, if GitHub's runner image has `v141_xp`.

**Operations: Windows service, control API, VoIPModem Manager.** Built
after `docs/design` (a mock-up in the Windows Server 2003 style).

- **Done**:
  - `VoIPModem.exe --install` / `--uninstall` / `--service`: a Windows
    service (automatic start, restart on failure) running the same
    application as the console.
  - The control API (`CONTROL_API.md`): status, statistics and the event
    log; answer, hang up, reset, in/out of service, re-register; add,
    edit and remove lines and change settings, written into the ini with
    its comments kept and applied to idle lines (or after a restart,
    which the API itself can do); a loopback self-test; the host's COM
    ports, com0com pairs and modem drivers. Optional password.
  - `VoIPModemManager.exe`, a Win32 program on that API only.
  - New settings behind them: `[Defaults]`, `[Media]`, `[Control]`,
    `[SIP] Transport/Server/Realm/RegisterExpiry/RegisterOnStart`,
    `[Logging] RotateKB/Keep/TraceAT`, per line `ErrorControl`,
    `Compression`, `InService`. A line whose port is missing is retried
    every 30 s instead of being dropped.
- **Verified** (2026-11-24, on the development machine): 101 unit tests
  (ini writer, defaults, statistics, com0com pairing, the API server over
  a socket with and without a password); the API end to end against a
  running service (status, line add, settings saved into the ini,
  in/out of service, reset, restart in-process with PJSIP recreated,
  stop); registration status against an unreachable registrar (408 shown
  as "Not registered"); the Manager driven against a running service
  (main window, settings, line properties, new line, ports and drivers,
  self-test passing at V.34, about). XP Win32 build of both programs:
  101 tests pass, PE 5.01, no import newer than XP SP3.
- **Verified live on Windows XP** (2026-11-24, `docs/images/manager-xp-v90-call.png`):
  VoIPModem running as a Windows service with four lines, all registered
  with Asterisk, watched in the Manager on the same machine; an incoming
  call on one line connected at **V.90 56000/26400** with V.42/V.44, and
  the Manager showed it live (state, caller, rates, duration, data,
  retrains, errors, statistics, carrier mix, jitter and loss, RTP rate).
- **Not yet verified**: controlling a live call from the Manager (hang up,
  out of service mid-call); Server 2003; the Manager against a service on
  another computer.

## Recommended next step

Find the **V.42bis/V.44 data stall** under PPP (Phase 5): its cause is
unknown, and turning compression off costs throughput on every call. Then, in order: a legacy
client dialling in through RRAS (Phase 5's criterion); V.90 start-up
through the FXS gateway and V.92, with call-audio recording on our side
to diagnose both; VoIPModem calling the physical modem; repeated calls
and retrains.
