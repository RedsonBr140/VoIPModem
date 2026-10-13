#pragma once

#include <deque>
#include <string>

#include "../src/Lib/Core/ICallController.h"

namespace voipmodem::test {

// Deterministic, test-only ICallController. Dial()/Answer() queue
// whatever CallEvent the test configured instead of PjsipCallController's
// real (and genuinely asynchronous) SIP signaling, so state-machine
// tests can exercise ModemStateMachine::HandleCallEvent() for every
// CallEventType without needing a live SIP server.
class FakeCallController : public voipmodem::ICallController {
public:
    voipmodem::CallEvent dialEvent{voipmodem::CallEventType::Ended, voipmodem::ResultCode::NoCarrier};
    voipmodem::CallEvent answerEvent{voipmodem::CallEventType::Ended, voipmodem::ResultCode::NoAnswer};
    std::string lastDialedNumber;
    int hangUpCount = 0;
    std::deque<voipmodem::CallEvent> queuedEvents;

    void Dial(const std::string& number) override {
        lastDialedNumber = number;
        queuedEvents.push_back(dialEvent);
    }

    void Answer() override {
        queuedEvents.push_back(answerEvent);
    }

    void HangUp() override {
        ++hangUpCount;
    }

    voipmodem::CallEvent PollEvent() override {
        if (queuedEvents.empty()) {
            return voipmodem::CallEvent{};
        }
        const voipmodem::CallEvent event = queuedEvents.front();
        queuedEvents.pop_front();
        return event;
    }
};

} // namespace voipmodem::test
