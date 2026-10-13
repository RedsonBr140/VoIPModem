#pragma once

#include "ICallController.h"
#include "../Logging/Logger.h"

namespace voipmodem {

// Phase 1 stub telephony backend. There is no SIP/RTP integration yet, so
// every dial or answer attempt honestly fails rather than pretending to
// place a call. This exists so the modem state machine's call-handling
// states (Dialing/Answering -> result code) are real and testable before
// Phase 2 wires in PJSIP.
class NullCallController : public ICallController {
public:
    explicit NullCallController(Logger& logger) : logger_(logger) {}

    CallOutcome Dial(const std::string& number) override {
        logger_.Warn("ATD requested dial to '" + number +
                      "' but no SIP backend is configured (Phase 2 work).");
        return CallOutcome::NoCarrier;
    }

    CallOutcome Answer() override {
        logger_.Warn("ATA requested answer but no SIP backend is configured (Phase 2 work).");
        return CallOutcome::NoAnswer;
    }

    void HangUp() override {
        // No active call is ever established by this stub; nothing to tear down.
    }

private:
    Logger& logger_;
};

} // namespace voipmodem
