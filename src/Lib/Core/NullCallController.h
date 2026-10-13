#pragma once

#include <optional>

#include "ICallController.h"
#include "../Logging/TaggedLogger.h"

namespace voipmodem {

// Phase 1 stub telephony backend, still used for any ModemInstance that
// has no SIP server configured (see docs/ARCHITECTURE.md "SIP integration
// (Phase 2)"). There is no SIP/RTP integration here, so every dial or
// answer attempt honestly fails rather than pretending to place a call.
// Even though it never has anything asynchronous to wait for, it still
// goes through the same PollEvent() seam as PjsipCallController so
// ModemStateMachine never needs to know which one it's talking to.
class NullCallController : public ICallController {
public:
    explicit NullCallController(TaggedLogger& logger) : logger_(logger) {}

    void Dial(const std::string& number) override {
        logger_.Warn("ATD requested dial to '" + number +
                      "' but no SIP backend is configured for this instance.");
        pendingEvent_ = CallEvent{CallEventType::Ended, ResultCode::NoCarrier};
    }

    void Answer() override {
        logger_.Warn("ATA requested answer but no SIP backend is configured for this instance.");
        pendingEvent_ = CallEvent{CallEventType::Ended, ResultCode::NoAnswer};
    }

    void HangUp() override {
        // No active call is ever established by this stub; nothing to tear down.
    }

    CallEvent PollEvent() override {
        if (!pendingEvent_.has_value()) {
            return CallEvent{};
        }
        const CallEvent event = *pendingEvent_;
        pendingEvent_.reset();
        return event;
    }

private:
    TaggedLogger& logger_;
    std::optional<CallEvent> pendingEvent_;
};

} // namespace voipmodem
