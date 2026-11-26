# Changelog

What each version of VoIPModem changes for the people who run it. Dates
are when the version was tagged. "Verified live" means tried on real
calls; everything else was tried with the unit tests only.

## Unreleased

### Added
- **Linux host support** (Debian 13 verified). A CMake build sits beside
  the Visual Studio solution. It builds PJSIP from its submodule and
  libv90modem with its own Makefile, and CI gains an Ubuntu job.
- **Pseudo-terminal lines on Linux.** Each line is a PTY that VoIPModem
  creates and publishes as a symlink at `ComPort=`. PTYs have no modem
  control lines, so DTR and DCD are emulated: the DTE's DTR counts as on
  while a program has the port open, and dropping carrier hangs up a
  program that follows it (CLOCAL off).
- **The `voipmodem` executable for Linux.** It runs in the foreground,
  stops on SIGINT/SIGTERM and logs to standard output (the journal under
  systemd). A systemd unit is in `config/voipmodem.service`; it has not
  been tried yet.
- **Logging of V.42 disconnects.** When the V.42 link drops during a
  call, the log names the reason and gives the link's counters at that
  moment.
- `CONTRIBUTING.md` and `docs/LINUX.md`.

### Changed
- **libv90modem 0.6.0-rc1 → 0.7.0.** The update includes upstream's fix
  for V.90 start-up failing on every call from a USR Courier behind an
  FXS gateway. With that fix (first shipped in 0.6.1), our Courier
  connected at V.90 49333/7200 with V.42. 0.7.0 also adds HST answering
  and MNP error control. Both are off by default and VoIPModem does not
  turn them on.
- **README rewritten for end users.** Build, CI and release details
  moved to `CONTRIBUTING.md`, and every doc now covers Linux.

### Verified live (Linux)
- Two Linux lines called each other through Asterisk at V.92
  53333/41333 with V.42 LAPM and V.44.
- PPP ran over these calls: pppd to pppd, and mgetty with AutoPPP. The
  emulated carrier drop hung up the answering pppd.
- pppd authenticated callers with PAP, CHAP and MS-CHAPv2 over a modem
  call, and refused a wrong CHAP password. FreeRADIUS is untested.
- BinModem dialled into a Linux line through Asterisk and connected at
  V.90 56000/26400 with V.42 and V.44. It then authenticated to pppd
  with CHAP-MD5, and pings over the link had 0% loss.

## 0.0.2 (2026-11-25)

### Added
- **DCD and DTR follow the modem.** `&C` and `&D` are honored: DCD
  follows the modem data link (`&C1`) or stays on (`&C0`). A DTR drop
  during a call enters online command mode (`&D1`), hangs up (`&D2`),
  or hangs up and resets (`&D3`). While DTR is low there is no S0
  auto-answer. This should fix Unimodem's "CD is high while
  initializing modem" and a hangup about 20 s into RAS calls; it has not
  yet been confirmed against RRAS.
- **Unimodem init strings accepted.** `&K`, `X` and `N` are now
  accepted and stored, so Windows' "Standard 56000 bps Modem" init
  strings (`AT&FE0V1&C1&D2S95=47S0=0`, `ATS7=60L0M1&K3N1X4`) no longer
  fail. In 0.0.1 they returned ERROR, so RAS never answered unless the
  init strings were changed by hand.
- Docs: Windows RAS dial-in guide (`docs/WINDOWS_RAS_DIALIN.md`) and
  screenshots in the README.

### Fixed
- A line that starts with `+` characters before `AT` (Unimodem's
  `+++ATH` after a call has already ended) is now accepted. Before,
  the first `ATH` returned ERROR and Unimodem had to retry.

### Changed
- PJSIP submodules updated to `ec1fa2e`; CI actions updated.

### Verified live (Windows, on 0.0.1 with hand-edited init strings)
- A USR Courier 56K, behind a Cisco SPA112 FXS gateway and registered
  with Asterisk, called in and connected at V.32bis 14400 and V.34
  31200/26400. Lines behind the gateway had to be limited to V.34,
  because V.90/V.92 start-up failed on every call; see Unreleased for
  the fix.
- Windows Server 2022 RRAS answered that Courier over a VoIPModem line:
  MS-CHAP v2 authentication, an address from RRAS's static pool, and
  internet access through NAT.

## 0.0.1 (2026-11-24)

The first release.

### Added
- **Hayes-compatible modem on a virtual COM port** (com0com), with any
  number of lines in one process. AT command parser and modem state
  machine; dialling (`ATD`, `T`/`P` modifiers), answering (`ATA`, S0
  auto-answer), `+++` escape, `ATH`, `ATZ`, `AT&F`, `E`, `V`, `Q`, `I`,
  S registers, `L`/`M` speaker commands, and V.25ter `+FCLASS`, `+GMI`,
  `+GMM`, `+GMR`. There is no `ATO`.
- **SIP and RTP through PJSIP.** Each line registers with an existing
  SIP server (Asterisk) and can call and be called. Media is G.711
  only, set up for modem tones. A deep fixed jitter buffer and no packet
  loss concealment avoid audio being invented or dropped, which breaks
  modem links. SIP stays on UDP.
- **Real modem modulation with libv90modem 0.6.0** (GPL-2.0-only,
  submodule): V.8, V.21, V.22/V.22bis, V.32bis, V.34, V.90 and V.92, and
  Bell 103/212A. V.42 LAPM with V.42bis/V.44 runs on top, and falls back
  to plain characters when the far end has no error control. This
  replaced the project's own experimental DSP code, which was removed.
- **`CONNECT <rate>` when the modem link is up,** not when the SIP call
  is answered. If the modem or V.42 link ends, the SIP call is hung up
  and NO CARRIER is reported once.
- Per-line `Modulations=`, `ErrorControl=`, `Compression=` and
  `InService=` settings.
- **Windows service mode** (`--install`, `--uninstall`, `--service`).
  The service has a control API: JSON over TCP, `127.0.0.1:7880` by
  default, with an optional password (`docs/CONTROL_API.md`).
- **VoIPModem Manager.** This Windows GUI is a client of the control
  API: line status and statistics, the event log, answering and
  hanging up, and adding, editing and removing lines and settings. The
  Manager never dials.
- **Settings written back to the ini.** Changes go to `voipmodem.ini`
  with its comments kept. A line whose COM port is missing is retried
  every 30 s. Logs rotate.
- Optional local monitoring of a line's call audio on the PC's speakers
  (`EnableLocalAudio`), with a mock dial tone and DTMF while dialling.
- **Builds:** x64 and Win32, and a Windows XP / Server 2003 build
  (Win32, `v141_xp`, `scripts/build-xp.ps1`).
- CI on GitHub Actions, release zips from `v*` tags, and Dependabot.

### Changed
- No made-up default line: an ini with no `[Modem...]` sections has no
  lines, and a line without `ComPort=` opens nothing.

### Verified live
- BinModem, a SIP software modem, called VoIPModem through Asterisk and
  connected at V.90 56000/26400 with V.42. VoIPModem called BinModem and
  connected at V.34 31200/14400 with V.42 and V.44.
- On Windows XP (32-bit): registration and a V.34 call; the service and
  the Manager ran.

### Known limits
- When VoIPModem calls with V.92 offered, the call clears down after
  Phase 4 (cause not found). Offering up to V.34 works.
- Windows RAS needs hand-edited Unimodem init strings (fixed in 0.0.2).

[Unreleased]: https://github.com/RedsonBr140/VoIPModem/compare/v0.0.2...HEAD
[0.0.2]: https://github.com/RedsonBr140/VoIPModem/compare/v0.0.1...v0.0.2
[0.0.1]: https://github.com/RedsonBr140/VoIPModem/releases/tag/v0.0.1
