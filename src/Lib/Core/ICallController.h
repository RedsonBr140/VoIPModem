#pragma once

#include <string>

#include "ResultCodes.h"

namespace voipmodem {

// A call controller cannot resolve Dial()/Answer() synchronously once it is
// backed by real SIP signaling (INVITE/ringing/answer resolve later, on the
// telephony backend's own schedule) — exactly the distinction
// CLAUDE.md's "Architectural Boundaries" section calls out: SIP call
// establishment and modem negotiation are different states, and one
// completing is never proof the other has. Dial()/Answer()/HangUp() below
// are therefore fire-and-forget; the eventual outcome arrives later via
// PollEvent().
enum class CallEventType {
    None,         // nothing pending
    IncomingCall, // a new call is alerting; caller should start a RING cadence
    Connected,    // the call (incoming or outgoing) is now up
    Ended         // the call finished; `result` is the modem result code to report
};

struct CallEvent {
    CallEventType type = CallEventType::None;
    ResultCode result = ResultCode::NoCarrier; // meaningful only when type == Ended
};

// Seam between the modem core and the telephony backend. Phase 1 shipped
// only NullCallController, a stub with no SIP/RTP behind it. Phase 2 adds
// PjsipCallController, a real PJSIP-backed implementation of this same
// interface; ModemStateMachine does not change when that happens — it only
// ever talks to ICallController.
class ICallController {
public:
    virtual ~ICallController() = default;

    // Starts dialing `number`. Does not block and does not return an
    // outcome — call PollEvent() (repeatedly, non-blocking) to learn what
    // happened once it does.
    virtual void Dial(const std::string& number) = 0;

    // Answers the call currently alerting (if any). No-op if nothing is
    // ringing. Like Dial(), the outcome arrives later via PollEvent().
    virtual void Answer() = 0;

    virtual void HangUp() = 0;

    // Non-blocking. Returns the next queued event, or {None} if nothing is
    // pending. Safe to call frequently (e.g. once per serial read-loop
    // iteration) — implementations must not block here.
    virtual CallEvent PollEvent() = 0;
};

} // namespace voipmodem
