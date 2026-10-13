# VoIPModem — Project Definition and Engineering Specification

## 1. Project Overview

**VoIPModem is a software-based dial-up modem that emulates a physical Hayes-compatible serial modem while transporting modem calls over an existing VoIP/SIP infrastructure.**

The project aims to connect legacy computers and operating systems to a virtual dial-up network without requiring a physical telephone line or a conventional hardware modem at the receiving end.

Instead of communicating over an analog telephone line, VoIPModem uses SIP for call signaling and RTP for audio transport. The software must eventually implement the modem modulation and demodulation required to establish actual data connections over that audio channel.

VoIPModem is not a SIP softphone, a terminal emulator, or merely an application that plays modem sounds. It must behave like a real modem from the perspective of software communicating with its serial interface.

## 2. Primary Use Case: RetroLab

The original target environment is a retrocomputing laboratory with the following topology:

- **Client:** A Pentium 4 computer running Windows 98, equipped with an Agere softmodem.
- **Telephony gateway:** A Cisco 3825 router with FXS voice interfaces, providing an analog telephone interface for the physical client modem.
- **VoIP infrastructure:** An existing Asterisk server providing SIP registration, authentication, call routing, and call establishment.
- **Virtual modem endpoint:** A modern Windows computer running VoIPModem, eventually hosted on Windows Server 2003 in the legacy deployment.
- **Remote networking:** Windows Remote Access Service (RAS) and PPP, allowing the client to establish a dial-up networking session.

The intended connection is:

`Windows 98 → Agere modem → Cisco FXS → VoIP/SIP network → VoIPModem → Windows RAS/PPP → network`

The analog modem on the client side should operate as if it were calling another physical modem. VoIPModem answers the incoming call, negotiates a modem connection, exchanges data, and exposes that connection to the host operating system.

**Incoming calls are mandatory.** VoIPModem must support answering calls initiated by physical dial-up modems. An implementation that only places outgoing calls is insufficient.

## 3. Platform and Development Constraints

### Initial development platform

- Windows 10/11, x64.
- C++ using Visual Studio 2022.
- A modular architecture that separates modem behavior, telephony, and serial-port handling.
- INI-based configuration for initial deployment.
- Asterisk is already available and must be used rather than embedding a replacement PBX.

### Legacy compatibility

Windows Server 2003 is an important deployment target for the virtual modem endpoint. Windows XP and Windows 98 compatibility on the software side are deferred until the modern Windows implementation is functional.

Keep operating-system-specific code isolated so that future compatibility work does not require rewriting the modem protocol implementation.

### Virtual serial ports

The initial prototype uses com0com to create a paired virtual serial connection, currently configured as COM3 and COM4. One side is exposed to the application or test harness, and the other can be opened by a terminal or dial-up networking client.

The serial interface must behave like a modem's COM port, not simply accept commands through a custom TCP socket or graphical interface.

The serial baud rate, initially 115200, is the local DTE-to-modem interface speed. It is not the negotiated modem-to-modem connection speed.

## 4. Core Architecture

Organize the implementation into independent modules.

### A. VoIPModem.Core

Responsible for:

- Modem state and call lifecycle.
- Hayes AT command parsing.
- Command execution and response generation.
- Modem configuration and S-registers.
- Connection status and result codes.
- Coordination between the serial interface, SIP session, and modem DSP.

The core must distinguish between command mode, dialing or answering, modem negotiation, online data mode, and disconnection.

### B. Serial Interface

Responsible for:

- Reading and writing bytes on the virtual COM port.
- Emulating modem responses and serial behavior.
- Handling local DTE baud rate and serial settings.
- Supporting command-mode input and online data-mode transmission.
- Reporting connection state through standard modem result codes.

Begin with com0com for development. An application-level COM-port implementation is acceptable for the prototype; a dedicated virtual COM driver can be considered later if required.

### C. Hayes Command Interpreter

Implement a useful subset of the Hayes AT command set, including:

- `AT` — attention and command synchronization.
- `ATI` — modem identification.
- `ATZ` — reset to a saved or default profile.
- `ATE` — command echo control.
- `ATV` — numeric or verbose result codes.
- `ATQ` — result-code suppression.
- `ATS0=n` — automatic answer after a configurable number of rings.
- `ATD` — dial or initiate an outgoing call.
- `ATA` — answer an incoming call.
- `ATH` — hang up.
- `AT&F` — restore factory defaults.

Support standard responses such as `OK`, `ERROR`, `RING`, `CONNECT`, `NO CARRIER`, `BUSY`, and `NO ANSWER`, where appropriate.

Do not implement commands as isolated string-matching hacks. Use a parser, a command execution layer, and a coherent modem state machine.

### D. SIP and RTP Transport

Integrate with the existing Asterisk installation.

Responsibilities include:

- SIP registration where required by the configured deployment.
- Authentication and account configuration.
- Outgoing call initiation.
- Incoming INVITE handling.
- Call acceptance and rejection.
- SIP dialog management and teardown.
- RTP audio transmission and reception.
- Negotiation and validation of compatible audio codecs.
- Call-state events delivered to the modem core.

Use a suitable SIP library such as PJSIP rather than implementing the SIP protocol from scratch.

The initial audio target is G.711 PCM at 8 kHz. Prefer an uncompressed G.711 media path to avoid unnecessary transcoding and preserve modem audio quality. Disable or avoid audio processing that corrupts modem tones, including noise suppression, echo cancellation, and voice-oriented gain control when these features interfere with data transmission.

### E. Modem DSP and Protocol Engine

This is the technically critical component.

VoIPModem must ultimately implement the modem signaling and data modulation required to communicate with real analog modems over the RTP audio stream.

The DSP layer must eventually support:

- Answer tones and modem negotiation.
- Modulation and demodulation.
- Symbol timing, carrier detection, and synchronization.
- Data framing and error recovery.
- Appropriate modem connection and failure detection.
- Compatibility with the supported modulation standards.

Begin with a modest, explicitly defined target such as V.32bis at 9600 bit/s. Expand to faster standards only after reliable end-to-end interoperability has been demonstrated.

V.34, V.90, V.42, and V.42bis are possible later targets, not requirements to claim as supported before they are implemented and tested.

Do not confuse receiving and transmitting modem audio with implementing a modem. SIP and RTP transport audio; they do not perform the modem's data modulation or negotiation.

### F. Host Networking Integration

The eventual objective is to make a successful modem connection usable by Windows Dial-Up Networking.

Integrate with Windows RAS and PPP so that the client can establish a network session after the modem connection succeeds.

Keep the modem link and PPP session conceptually separate:

1. The modem establishes a data connection.
2. The serial interface carries the negotiated data stream.
3. The host networking stack establishes PPP.
4. The remote-access service assigns network parameters and routes traffic.

A SIP call being connected does not, by itself, mean that a modem connection or PPP session has been established.

## 5. Configuration

Use an INI configuration file initially. At minimum, provide settings for:

- SIP server address and port.
- SIP username and authentication credentials.
- SIP transport and registration behavior.
- Local virtual COM port.
- DTE baud rate.
- Modem identification string.
- Automatic-answer behavior and ring count.
- Supported modem standards and maximum connection speed.
- RTP codec preferences.
- Logging level and log-file destination.

Current prototype defaults include:

`ComPort=COM3`

`BaudRate=115200`

Treat these as initial defaults rather than hard-coded assumptions. Keep credentials out of diagnostic logs.

## 6. Development Roadmap

Build the project incrementally. Each phase must produce a testable result.

### Phase 1 — Serial modem skeleton

- Create the C++ solution and module boundaries.
- Read configuration from an INI file.
- Open the configured virtual COM port.
- Parse `AT`, `ATI`, `ATE`, `ATZ`, and basic configuration commands.
- Return correct responses and implement basic modem states.
- Test with a terminal emulator through com0com.

Acceptance criterion: the application behaves like a basic Hayes-compatible modem in command mode.

### Phase 2 — SIP call control

- Integrate PJSIP.
- Register with Asterisk when configured.
- Initiate outgoing calls.
- Receive incoming calls and generate `RING`.
- Answer using `ATA` or automatic answer using `ATS0`.
- Disconnect using `ATH`.
- Translate SIP call states into appropriate modem result codes.

Acceptance criterion: incoming and outgoing SIP calls work reliably, with correct serial responses.

### Phase 3 — RTP audio

- Negotiate G.711 audio.
- Send and receive RTP audio.
- Verify sample rate, packetization, timing, and codec compatibility.
- Preserve modem tones without inappropriate voice processing.
- Add packet-loss, jitter, and session diagnostics.

Acceptance criterion: bidirectional modem-like audio can pass through the VoIP path without the application corrupting it.

### Phase 4 — Actual modem interoperability

- Implement or integrate a suitable modem DSP engine.
- Establish a real data connection between a physical analog modem and VoIPModem.
- Start with V.32bis at 9600 bit/s.
- Verify bidirectional data transfer, error handling, disconnects, and reconnection.

Acceptance criterion: a physical client modem establishes a genuine data connection, not merely an answered telephone call.

### Phase 5 — Windows networking

- Connect the modem's serial data stream to the host networking arrangement.
- Integrate with Windows RAS/PPP.
- Configure authentication, address assignment, routing, and DNS as required.
- Test a complete dial-up networking session from the Windows 98 client.

Acceptance criterion: the legacy client can dial in and exchange IP traffic through the remote-access service.

### Phase 6 — Compatibility and optimization

- Improve negotiation reliability.
- Test against different physical modems and softmodems.
- Add additional modulation standards only after the baseline is stable.
- Evaluate Windows Server 2003 deployment compatibility.
- Evaluate the need for a dedicated virtual COM driver.
- Document installation, configuration, troubleshooting, and known limitations.

## 7. Engineering Rules

1. **Build a working prototype before attempting high-speed modem standards.**
2. Do not claim V.32bis, V.34, V.90, V.42, or V.42bis support until the corresponding implementation has been tested.
3. Keep SIP call state, modem connection state, and PPP session state independent.
4. Implement incoming calls as a first-class requirement.
5. Use structured logging for serial commands, modem state transitions, SIP dialogs, RTP statistics, and DSP negotiation.
6. Use clean module boundaries and documented interfaces.
7. Do not replace the existing Asterisk server with a new PBX.
8. Do not assume that a successful SIP call proves modem interoperability.
9. Prefer a working, limited-speed implementation over a broad but nonfunctional feature set.
10. Preserve the Windows-first development strategy and defer legacy client-side compatibility work.

## 8. What Claude Should Build First

Start by inspecting the existing repository, identifying its current modules and implementation status, and preserving working functionality.

Then produce:

1. A concise assessment of what already works and what is missing.
2. A proposed module structure and dependency plan.
3. A prioritized implementation plan with measurable acceptance criteria.
4. The first complete, buildable increment of the application.

The first increment should establish a reliable Hayes-compatible virtual serial modem with configuration, logging, and a proper state machine. Next, add SIP incoming/outgoing call control. Do not begin by attempting to implement every modem standard or PPP support simultaneously.

**Definition of success:** A legacy Windows 98 computer dials a configured number using its physical modem, reaches VoIPModem through the Cisco FXS and Asterisk infrastructure, negotiates a real modem data connection, and eventually establishes a functional PPP network session.