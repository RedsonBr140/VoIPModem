#pragma once

namespace voipmodem {

// Per docs/PROJECT_SPEC.md section 4.A, the core must distinguish command
// mode, dialing/answering, modem negotiation, online data mode, and
// disconnection. Phase 1 (NullCallController) only ever reaches Dialing/
// Answering transiently before falling back to CommandMode. OnlineData is
// reached once a real SIP call connects (see ModemStateMachine's Connected
// event) -- once there, ModemInstance hands serial bytes straight to
// ICallController::SendData/PollReceivedData instead of parsing them as AT
// commands (see docs/ROADMAP.md Phase 4's V.21 data-mode milestone). Note
// this is a deliberate simplification: real serial-level data flow starts
// at SIP CONNECT, not at the moment the call's own modem link (V.8/V.21)
// actually finishes negotiating a moment later -- bytes sent before that
// just wait queued in CallAudioBridge, nothing is lost, but a strictly
// accurate Hayes CONNECT would wait for the modem link instead. There is
// no Negotiating state transition today for a real call (V8Handshake runs
// entirely inside CallAudioBridge, invisible to ModemStateMachine).
enum class ModemState {
    CommandMode,
    Dialing,
    Answering,
    Negotiating,
    OnlineData,
    Disconnecting
};

} // namespace voipmodem
