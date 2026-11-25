# VoIPModem on Linux

Plan, design and status of the Linux port. Linux was added as a supported
host platform after release 0.0.2 (see `PROJECT_SPEC.md` section 3,
"Linux"). Windows remains the primary platform and keeps its Visual
Studio build.

**Status: planned. Nothing Linux-specific has been built yet.** Each item
below says whether it is done, in progress or planned; "done" means it
was built and run on Linux, not only written.

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

Each line's `ComPort=` names a path, for example `/run/voipmodem/ttyVM0`.
VoIPModem opens a PTY with `openpty()`, keeps the master side, closes its
own copy of the slave side, and points the symlink at the slave
(`/dev/pts/N`). pppd, mgetty or a terminal program opens the symlink as
if it were a modem's serial port. The baud rate the program sets has no
effect, as with com0com.

A PTY has no modem control lines: no DTR, DSR or DCD. The `&C`/`&D`
behaviour (`ARCHITECTURE.md`, "Serial control signals") is emulated:

| Signal | com0com (Windows) | PTY (Linux, planned) |
|---|---|---|
| DTE's DTR ("is a program attached?"), for `&D` and auto-answer | Other side's DTR | Whether any program has the slave open: the master sees `POLLHUP` when the last one closes it |
| DTR drop that hangs up under `&D2` | Program drops DTR | Program closes the port (pppd exits, the terminal quits) |
| DCD off (carrier lost) under `&C1` | Our DTR, wired to the other side's DCD | Close the PTY and create a new one behind the same symlink, so the program gets a hangup (SIGHUP, then EIO) |
| DCD on (carrier up) | Our DTR | Cannot be signalled; opening a PTY never waits for carrier |

To be designed and verified, not yet decided:

- **When to force the hangup.** Recreating the PTY also disconnects a
  terminal program that only typed `ATH`, which a real modem would not
  do. The plan is to read the slave's termios from the master and force
  the hangup only when `CLOCAL` is clear, that is when the program asked
  to follow carrier, as pppd does without its `local` option. Whether
  Linux returns the slave's termios through the master needs checking.
- **Output with nobody attached.** `RING` and other output written while
  no program has the port open would queue in the PTY and be read later
  by the next program. It should be dropped instead.
- **pppd and mgetty on a PTY.** pppd may need its `local` option;
  mgetty checks DCD with `TIOCMGET`, which PTYs do not support. Both need
  live testing.

## Increments

| # | Increment | Status |
|---|---|---|
| 1 | CMake build of libv90modem, PJSIP and the VoIPModem library; the unit tests pass on Linux; an Ubuntu CI job | Planned |
| 2 | `PtySerialPort` (`ISerialPort` on a PTY) with the emulation above, and unit tests that open the slave side | Planned |
| 3 | Linux entry point: foreground process, SIGTERM/SIGINT, systemd unit; Linux versions of the platform-specific parts of `SystemInfo` and `Application` | Planned |
| 4 | Live test: AT commands through a terminal program on the symlink, SIP registration with Asterisk, a modem call to `CONNECT` | Planned |
| 5 | A dial-in setup with pppd on the PTY (authentication such as FreeRADIUS), documented | Planned |
| 6 | Release packaging for Linux | Planned |

## Windows-specific code to port

From a survey of `src/App` and `src/Lib` (the Manager stays Windows-only):

| Code | What it uses | Linux plan |
|---|---|---|
| `Lib/Serial/WindowsSerialPort` | Win32 COM API | New `PtySerialPort` (increment 2) |
| `App/main.cpp` | Windows service, console Ctrl+C handler | Signal handling and systemd (increment 3) |
| `Lib/Control/SystemInfo.cpp` | Registry: COM ports and modem drivers | List the lines' PTYs instead (increment 3) |
| `Lib/Audio/WindowsAudioOutput` | waveOut | Left out on Linux (increment 1) |
| `Lib/Sip/PjsipEndpoint.cpp` | `timeBeginPeriod` | Not needed on Linux (increment 1) |
| `App/Application.cpp`, `Lib/Logging/Logger.cpp`, `Lib/Config/IniDocument.cpp` | A few `_WIN32` sections | Linux branches (increments 1 and 3) |
| `Lib/Control/ControlServer.cpp` | Sockets | Already has a POSIX branch; untested on Linux |

## Test environment

- Debian 13 (trixie), x86_64, kernel 6.12, gcc 14.2, CMake 3.31.
- The same Asterisk server used for the Windows tests.

## Not tested

Everything on this page, until the status column says otherwise.
