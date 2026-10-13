#pragma once

#include <string>

#include "../src/Lib/Core/ICallController.h"

namespace voipmodem::test {

// Deterministic, test-only ICallController: returns whatever outcome the
// test configured instead of NullCallController's fixed stub behavior, so
// state-machine tests can exercise every CallOutcome -> ResultCode mapping.
class FakeCallController : public voipmodem::ICallController {
public:
    voipmodem::CallOutcome dialOutcome = voipmodem::CallOutcome::NoCarrier;
    voipmodem::CallOutcome answerOutcome = voipmodem::CallOutcome::NoAnswer;
    std::string lastDialedNumber;
    int hangUpCount = 0;

    voipmodem::CallOutcome Dial(const std::string& number) override {
        lastDialedNumber = number;
        return dialOutcome;
    }

    voipmodem::CallOutcome Answer() override {
        return answerOutcome;
    }

    void HangUp() override {
        ++hangUpCount;
    }
};

} // namespace voipmodem::test
