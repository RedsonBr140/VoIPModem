# VoIPModem — Claude Code Instructions

## Project Identity

VoIPModem is a Windows-first software modem that emulates a Hayes-compatible serial modem and transports modem calls over an existing SIP/VoIP infrastructure.

The long-term goal is to allow legacy computers, including Windows 98 systems with physical analog modems, to dial into a virtual modem endpoint and establish real modem data connections and PPP networking sessions.

## Before Making Changes

1. Inspect the existing repository, source code, configuration, dependencies, and tests before proposing or implementing changes.
2. Read `docs/PROJECT_SPEC.md` for the complete requirements and intended use case.
3. Read `docs/ARCHITECTURE.md` and `docs/ROADMAP.md` if they exist.
4. Identify implemented, partially implemented, missing, and broken functionality.
5. Preserve working behavior unless a change is justified and explicitly accounted for.

Do not assume the repository matches the proposed architecture or that any feature is already implemented.

## Core Requirements

- Windows-first development, using C++ and Visual Studio.
- Hayes-compatible serial modem emulation.
- Virtual COM port support, initially using com0com.
- Integration with an existing Asterisk SIP infrastructure.
- Incoming calls are mandatory; outgoing calls are also required.
- Actual modem modulation and demodulation are required for end-to-end modem interoperability.
- Windows RAS/PPP integration is a later objective.
- Windows Server 2003 deployment compatibility is an important constraint.

## Architectural Boundaries

Keep these concerns separate:

- Modem state machine and core logic.
- Serial-port I/O.
- Hayes AT command parsing.
- SIP signaling and RTP media.
- Modem DSP and data-link protocols.
- Host networking and PPP integration.

SIP call establishment, modem data-link establishment, and PPP session establishment are distinct states. Never treat success at one layer as proof of success at another.

## Engineering Rules

- Implement small, buildable, testable increments.
- Do not claim a modem standard is supported without implementation and interoperability tests.
- Prefer established protocol libraries over writing complex protocol stacks from scratch.
- Do not replace the existing Asterisk PBX.
- Keep credentials out of logs.
- Add appropriate tests and update documentation when changing behavior.
- Document limitations and distinguish implemented functionality from planned functionality.
- Avoid unnecessary dependencies, speculative abstractions, and unrelated rewrites.
- Maintain compatibility with the repository's existing build system and coding conventions unless there is a concrete reason to change them.

## Initial Priority

Establish the actual repository state before implementing new features. Report what exists, what works, what is incomplete, and the next recommended implementation step.

Use `docs/PROJECT_SPEC.md` as the authoritative source for project goals and requirements. If implementation constraints make a requirement infeasible, explain the limitation and propose alternatives rather than silently changing the requirement.