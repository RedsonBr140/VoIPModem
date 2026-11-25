# VoIPModem on Linux

Plan, design and status of the Linux port. Linux was added as a supported
host platform after release 0.0.2 (see `PROJECT_SPEC.md` section 3,
"Linux"). Windows remains the primary platform and keeps its Visual
Studio build.

**Status: VoIPModem builds and runs on Linux. Two of its lines, registered
with Asterisk, called each other through the PBX (V.92, V.42 LAPM,
V.44), and pppd, both directly and started by mgetty, ran PPP over that
call, with pppd's local PAP, CHAP and MS-CHAPv2 authentication. Not yet
tested with a physical modem or with FreeRADIUS.** Each item below says whether it is done, in progress
or planned; "done" means it was built and run on Linux, not only
written.

## Building

Needs CMake 3.28 or later, a C and C++17 compiler and make. No other
packages: PJSIP and libv90modem come from the submodules.

```sh
git submodule update --init third_party/pjproject third_party/libv90modem
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure
```

- `CMakeLists.txt` is for Linux only and stops with an error on Windows,
  where `VoIPModem.sln` is the build.
- PJSIP is built with its own CMake build, with VoIPModem's
  `third_party/pjsip-config/config_site.h` copied into its tree (as
  `scripts/setup-pjproject.ps1` does on Windows). Only what VoIPModem
  uses is built: SIP, RTP, G.711 and the null sound device. There is no
  SSL, video, ALSA, or codec that needs a system library.
- libv90modem is built by its own `Makefile`, into the build directory.
- Targets: `voipmodem` (the modem bank), `voipmodem_lib` (static
  library) and `voipmodem_tests` (the Windows unit tests plus the PTY
  tests).

## Running

```sh
build-linux/voipmodem /path/to/voipmodem.ini
```

- The same `voipmodem.ini` as on Windows, except that each line's
  `ComPort=` is the path where VoIPModem publishes the line's port (see
  "PTY serial side"), for example `ComPort=/run/voipmodem/ttyVM0`.
- Runs in the foreground; SIGINT or SIGTERM stops it cleanly. The log
  goes to the `[Logging] File` and to standard output.
- No `--install`. For systemd, `config/voipmodem.service` runs
  `/usr/local/bin/voipmodem /etc/voipmodem/voipmodem.ini` (installed with
  `sudo cmake --install build-linux`), and creates `/run/voipmodem` for
  the ports. Its comments list the steps. **The unit file has not been
  tried yet.**
- The control API works as on Windows (`hello` reports
  `"platform": "Linux, 64-bit"`). The Manager's port and modem-driver
  windows stay empty: there are no COM ports or modem drivers to list.
- `EnableLocalAudio` is ignored, with a warning: there is no modem
  speaker on Linux.

## Scope

On Linux, VoIPModem is the same modem bank as on Windows: SIP/RTP to
Asterisk on one side, and one serial-like port per line on the other,
running Hayes commands and the modem DSP (libv90modem).

Out of scope, as on Windows (`ARCHITECTURE.md`):

- **PPP.** The operator runs pppd (or similar) on each line's port, with
  authentication such as FreeRADIUS through pppd's RADIUS plugin.
  VoIPModem only documents setups that work with it.
- **A Linux GUI.** The VoIPModem Manager stays a Windows program; it can
  manage a Linux host over the control API (`CONTROL_API.md`).
- **Modem-speaker audio** (`EnableLocalAudio`): Windows only for now.

## Decisions

| Topic | Decision | Why |
|---|---|---|
| Serial side | VoIPModem creates a pseudo-terminal (PTY) per line and publishes a symlink to it | No kernel module to install (Linux has no com0com; tty0tty would need one) |
| Build | CMake on Linux, beside the unchanged `VoIPModem.sln` | Standard on Linux; the Windows build stays as it is |
| PJSIP | Built from the `third_party/pjproject` submodule with our `config_site.h` | Same pinned version and media tuning (jitter buffer, no PLC) as Windows; distro packages differ by version |
| libv90modem | Built with its own `Makefile` from the submodule | It already builds on Linux |
| Service | A foreground process for systemd (stops on SIGTERM), plus a unit file | No Windows service on Linux |

## PTY serial side

`src/Lib/Serial/PtySerialPort`.

Each line's `ComPort=` names a path, for example `/run/voipmodem/ttyVM0`.
VoIPModem creates a PTY with `openpty()`, keeps the master side, closes
its own copy of the slave side, and points a symlink at the slave
(`/dev/pts/N`). pppd, mgetty or a terminal program opens the symlink as
if it were a modem's serial port.

- An existing symlink at the path is replaced (a leftover from a previous
  run); anything else there is an error. The directory must exist.
- Closing the port removes the symlink, if it still points to our PTY.
- The port starts raw, 8-bit, with `CLOCAL` on (carrier ignored). Programs
  set their own modes as usual; the baud rate they set has no effect, as
  with com0com.
- The slave device is owned by the user VoIPModem runs as, mode 0620.
  The program on the other side must run as that user or as root.

### Emulated modem control lines

A PTY has no modem control lines: no DTR, DSR or DCD (`TIOCMGET` fails
with `ENOTTY` on both sides). The `&C`/`&D` behaviour (`ARCHITECTURE.md`,
"Serial control signals") is emulated:

| Signal | com0com (Windows) | PTY (Linux) |
|---|---|---|
| DTE's DTR, read as our DSR, for `&D` and auto-answer | Other side's DTR | On while any program has the port open. The master sees `POLLHUP` when the last one closes it |
| DTR drop that hangs up under `&D2` | Program drops DTR | Program closes the port (pppd exits, the terminal quits) |
| Our DCD off (carrier lost) under `&C1` | Our DTR, wired to the other side's DCD | If the program has `CLOCAL` off (it follows carrier, as pppd does without `local`), a new PTY replaces the old one behind the symlink and the old master is closed: the program gets SIGHUP, then EOF on read and `EIO` on write. A program with `CLOCAL` on is left alone, as a real modem's DCD drop would leave it |
| Our DCD on | Our DTR | Cannot be signalled. Opening a PTY never waits for carrier |

Also:

- **Output while nobody has the port open is dropped.** A PTY would
  otherwise queue it (`RING`, result codes) for the next program.
- **Output a program left unread is flushed when it closes the port.**
  The master cannot flush the slave's queue, so VoIPModem opens the slave
  briefly (`O_NOCTTY`) and flushes it there.
- **A program that stops reading** gets 16 KB queued; after that, a write
  waits up to 200 ms and the rest is dropped and logged.

Verified on the test host (kernel 6.12), by a probe program and the unit
tests in `tests/PtySerialPortTests.cpp`:

- the master reports the slave's termios (`CLOCAL`, and `B0`, the Unix
  way of dropping DTR);
- `POLLHUP` on the master while no slave is open, cleared on reopen;
- closing the master kills a process that has the slave as its
  controlling terminal with SIGHUP; one that ignores SIGHUP gets EOF and
  `EIO`;
- data queued for an unread slave survives the slave's close and is not
  removed by `tcflush` on the master.

Open: **races.** A program that closes the port and reopens it within
microseconds may still find unread output, or, after a carrier drop, may
open the old PTY just before it is closed.

## Dial-in with pppd

What was run live (see "Live results"). The examples below use
`noauth`, which is for testing only. With pppd's local authentication
(`auth`, `name <server>`, and the callers in `/etc/ppp/pap-secrets` and
`/etc/ppp/chap-secrets`), PAP, CHAP and MS-CHAPv2 were each verified over
a call, and a wrong password was refused. FreeRADIUS, through pppd's
RADIUS plugin, has not been tried.

**pppd directly.** pppd waits for `RING` and answers:

```sh
pppd /run/voipmodem/ttyVM1 115200 noauth nodetach \
  10.250.250.1:10.250.250.2 \
  connect '/usr/sbin/chat -v TIMEOUT 5 "" ATZ OK "" TIMEOUT 90 RING ATA TIMEOUT 60 CONNECT ""'
```

- pppd's default `modem` mode works: it does **not** need `local`. When
  the call ends, the carrier-drop emulation hangs it up and it logs
  "Modem hangup".
- The line's `AutoAnswerRingCount` should be 0 (the default); pppd's
  chat answers.

**mgetty with AutoPPP.** mgetty answers and starts pppd when the caller
sends PPP frames:

- mgetty only opens devices under `/dev`. Link one there:
  `ln -s /run/voipmodem/ttyVM1 /dev/ttyVM1`, and run `mgetty ttyVM1`. The
  link follows VoIPModem's symlink, so it survives the PTY being
  replaced.
- In `/etc/mgetty/mgetty.config`, a section for the port (`port ttyVM1`,
  `data-only yes`, `speed 115200`).
- The `/AutoPPP/` line of `login.config` starts pppd (the test used
  `/usr/sbin/pppd noauth 10.250.250.1:10.250.250.2`).
- mgetty logs `TIOCMGET failed` and `TIOCMBIC failed: Inappropriate ioctl
  for device` (it reads the modem lines and drops DTR to reset the
  modem). Both are harmless: it carries on, sends its init string, waits
  for `RING`, answers with `ATA` and starts pppd.
- When the call ends, the pppd that mgetty started gets SIGHUP ("Modem
  hangup"), as with a real modem.

## Live results (2026-11-25)

### Through Asterisk

One VoIPModem process on the test host with two lines, each registered
with the Asterisk server used for the Windows tests (PJSIP extensions,
G.711 u-law). Line A dialled line B's extension through Asterisk. Both
modems are libv90modem.

| Test | Result |
|---|---|
| Registration | Both lines 200 OK within 2 s of start |
| A calls B, B answered with `ATA` | `RING`, codec PCMU, then **V.92** 53333/41333 bit/s, **V.42 LAPM with V.44**; data intact in both directions; `+++`, `ATH`; B gets `NO CARRIER` |
| pppd client (A, in its own network namespace) to pppd server (B) | IPCP up in 20 s; ping over the modem link, 0% loss, RTT about 600 ms |
| The client pppd killed (no LCP terminate) | A hangs up (`&D2`); B's carrier drop hangs up the server pppd ("Modem hangup") |
| mgetty on B (AutoPPP) instead of the pppd server | mgetty answers; its pppd brings IPCP up in 23 s; ping 0% loss; hung up with SIGHUP ("Modem hangup") when the client is killed |
| pppd server with local authentication (`auth require-pap`, `require-chap`, `require-mschap-v2`; secrets in `/etc/ppp`) | Each method: peer authenticated (MS-CHAPv2 confirmed in LCP as `<auth chap MS-v2>`), IPCP up in 20 s, ping 0% loss |
| The same with a wrong client password (CHAP) | Refused: "Peer vmtest failed CHAP authentication", no address, connection terminated |
| Passwords | SIP passwords not in the log, at `Level=Debug` with the SIP trace; the PPP secret not in VoIPModem's log either |

At shutdown, each line logs `Unable to create/send REGISTER: Object is
busy (PJSIP_EBUSY)` while it un-registers; it is the same harmless
message as on Windows (`WINDOWS_RAS_DIALIN.md`, Known issues).

### Without Asterisk

The first runs, before Asterisk extensions were available: line A calling
line B (`ATDT200`) through VoIPModem's own SIP endpoint
(`[SIP] Server=127.0.0.1:<LocalPort>`, `RegisterOnStart=0`).

| Test | Result |
|---|---|
| AT commands over the PTY (terminal program in raw mode) | `AT`, `ATI`, `AT&C1&D2`, `ATS0?`, `ATH` answered; open and close seen as the DTE's DTR |
| Control API `hello` | Answered; platform "Linux, 64-bit" |
| SIGTERM | Clean shutdown; symlinks removed |
| A calls B, B auto-answers (S0=1) | `RING`, then **V.92** 53333/41333 bit/s both ways; data intact in both directions; `+++`, `ATH`; B gets `NO CARRIER` |
| pppd client (A, in its own network namespace) to pppd server (B) | IPCP up in 20 s; ping over the modem link, 0% loss, RTT 400-600 ms |
| The client pppd killed (no LCP terminate) | A hangs up (`&D2`); B's carrier drop hangs up the server pppd ("Modem hangup") |
| mgetty on B (AutoPPP) instead of pppd | mgetty answers; its pppd brings IPCP up in 23 s; ping 0% loss; hung up with SIGHUP when the client is killed |

The round trip time includes both lines' jitter buffers (160 ms each
way by default) and the modems' own delay.

## Increments

| # | Increment | Status |
|---|---|---|
| 1 | CMake build of libv90modem, PJSIP and the VoIPModem library; the unit tests pass on Linux; an Ubuntu CI job | **Done** 2026-11-25: 160 tests pass on Debian 13 (Release and Debug) |
| 2 | `PtySerialPort` (`ISerialPort` on a PTY) with the emulation above, and unit tests that open the slave side | **Done** 2026-11-25: 10 PTY tests (170 in all) pass on Debian 13, 30 runs without a failure |
| 3 | Linux entry point: foreground process, SIGTERM/SIGINT, systemd unit; Linux versions of the platform-specific parts of `Application` | **Done** 2026-11-25, except that the systemd unit has not been tried |
| 4 | Live test: AT commands through a terminal program on the symlink, SIP registration with Asterisk, a modem call to `CONNECT` | **Done** 2026-11-25: AT commands; registration; V.92 calls between two lines through Asterisk |
| 5 | A dial-in setup with pppd on the PTY (authentication such as FreeRADIUS), documented | **Partly done**: pppd and mgetty + AutoPPP verified through Asterisk; pppd's local PAP, CHAP and MS-CHAPv2 authentication verified. FreeRADIUS not yet tested |
| 6 | Release packaging for Linux | Planned |

## Windows-specific code to port

From a survey of `src/App` and `src/Lib` (the Manager stays Windows-only):

| Code | What it uses | Linux |
|---|---|---|
| `Lib/Serial/WindowsSerialPort` | Win32 COM API | Done: `PtySerialPort`, chosen in `ModemInstance.h` |
| `App/main.cpp` | Windows service, console Ctrl+C handler | Done: `App/main_linux.cpp` (signals) and `config/voipmodem.service` |
| `Lib/Control/SystemInfo.cpp` | Registry: COM ports and modem drivers | Left empty on Linux; the lines' PTYs are not listed yet |
| `Lib/Audio/WindowsAudioOutput` | waveOut | Done: not built on Linux; `EnableLocalAudio` is logged and ignored |
| `Lib/Sip/PjsipEndpoint.cpp` | `timeBeginPeriod` | Done: Windows only |
| `Lib/Logging/Logger.cpp`, `Lib/Config/IniDocument.cpp` | A few `_WIN32` sections | Done: they already had POSIX branches, now built on Linux |
| `App/Application.cpp` | Host name, CPU time, processor count, platform name | Done: POSIX branches |
| `Lib/Control/ControlServer.cpp` | Sockets | Done: its POSIX branch passes the control API tests over loopback |
| `tests/ControlTests.cpp` | Winsock test client | Done: POSIX branch |

## Test environment

- Debian 13 (trixie), x86_64, kernel 6.12, gcc 14.2, CMake 3.31.
- ppp 2.5.2 and mgetty 1.2.1 from Debian.
- The Asterisk server used for the Windows tests, on the same LAN.

## Not tested

- A far end other than VoIPModem itself: a physical modem through the
  FXS gateway, a legacy client dialling in, a SIP softmodem such as
  BinModem.
- FreeRADIUS (pppd's RADIUS plugin). Authentication through mgetty's
  AutoPPP (only pppd started directly was tried with authentication).
- The systemd unit; running VoIPModem as a user other than the one that
  opened the ports in the tests.
- Several simultaneous calls on Linux beyond the two-line loop.
- Distributions other than Debian 13, and CPUs other than x86_64.
