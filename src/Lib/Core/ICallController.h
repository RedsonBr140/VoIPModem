#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
    Connected,    // the modem link (not just the phone call) is up: data can flow
    Ended         // the call finished; `result` is the modem result code to report
};

struct CallEvent {
    CallEventType type = CallEventType::None;
    ResultCode result = ResultCode::NoCarrier; // meaningful only when type == Ended
    int connectRateBps = 0;                    // Connected: the rate this modem receives at; 0 = unknown
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

    // Online-data-mode byte passthrough (Phase 4's first real "modem
    // talks to modem" milestone — see docs/ROADMAP.md). Once a call
    // reaches ModemState::OnlineData, bytes arriving on the serial port
    // are handed here instead of being parsed as AT commands, and bytes
    // the far end's modulation has actually decoded are drained back out
    // via PollReceivedData(). Both are fire-and-forget/non-blocking, same
    // spirit as Dial()/Answer(): a real implementation (PjsipCallController)
    // only *queues* bytes here — whether and when they actually leave over
    // the wire depends on the call's own modem link (e.g. V.21 FSK) having
    // finished negotiating, which can lag slightly behind CONNECT. Once the
    // call's audio bridge exists, nothing queued there is dropped by that
    // wait — it just sits until the link activates. There is one narrow
    // exception: PjsipCallController's own audio bridge is only constructed
    // once PJSIP reports call media active, a moment that can trail the
    // SIP-level CONFIRMED this interface's Connected event already fired
    // on; a byte sent in that brief window (bridge not constructed yet) is
    // dropped, not queued further back than that. The no-SIP-backend stub
    // (NullCallController) never has an active call, so both are no-ops
    // there.
    virtual void SendData(const std::vector<std::uint8_t>& bytes) = 0;
    virtual std::vector<std::uint8_t> PollReceivedData() = 0;
};

} // namespace voipmodem
