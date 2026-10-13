#pragma once

namespace voipmodem {

// Per docs/PROJECT_SPEC.md section 4.A, the core must distinguish command
// mode, dialing/answering, modem negotiation, online data mode, and
// disconnection. Phase 1 has no telephony or DSP backend, so Dialing,
// Answering, and Negotiating are only reachable transiently via
// NullCallController before falling back to CommandMode; nothing today
// reaches OnlineData.
enum class ModemState {
    CommandMode,
    Dialing,
    Answering,
    Negotiating,
    OnlineData,
    Disconnecting
};

} // namespace voipmodem
