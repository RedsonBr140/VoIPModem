#pragma once

#include <string>

namespace voipmodem {

// Outcome of a telephony attempt, expressed as a modem result rather than
// a SIP/call-control concept. The state machine never translates a SIP
// outcome directly into "connected" — see docs/PROJECT_SPEC.md section 4.F
// and the "Architectural Boundaries" section of CLAUDE.md: SIP call
// establishment and modem negotiation are distinct, and a call controller
// reporting a SIP-level success still only hands the state machine a call
// outcome, never a modem CONNECT.
enum class CallOutcome {
    NoCarrier,
    NoAnswer,
    Busy,
    NoDialtone,
    Ringing // an incoming call is alerting; not used until Phase 2
};

// Seam between the modem core and the telephony backend. Phase 1 ships
// only NullCallController, a stub with no SIP/RTP behind it. Phase 2 adds
// a PJSIP-backed implementation of this same interface; ModemStateMachine
// does not change when that happens.
class ICallController {
public:
    virtual ~ICallController() = default;

    virtual CallOutcome Dial(const std::string& number) = 0;
    virtual CallOutcome Answer() = 0;
    virtual void HangUp() = 0;
};

} // namespace voipmodem
