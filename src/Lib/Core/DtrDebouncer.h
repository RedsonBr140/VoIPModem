#pragma once

#include <chrono>

namespace voipmodem {

// Filters samples of the DTE's DTR line (polled from the serial port's
// DSR input) so that a brief low glitch -- com0com toggles the lines as
// either end opens or closes its port -- does not count as the DTE
// dropping DTR. A drop is reported only once the line has read low on
// every sample for at least `dropDelay`; a rise is reported at once.
//
// No clock of its own: the caller passes the sample time, so tests can
// drive it without sleeping.
class DtrDebouncer {
public:
    using Clock = std::chrono::steady_clock;

    enum class Change { None, Raised, Dropped };

    explicit DtrDebouncer(std::chrono::milliseconds dropDelay) : dropDelay_(dropDelay) {}

    // Sets the debounced level without reporting a change (e.g. the level
    // read when the port opens).
    void Reset(bool level) {
        level_ = level;
        lowPending_ = false;
    }

    Change Update(bool sample, Clock::time_point now) {
        if (sample) {
            lowPending_ = false;
            if (!level_) {
                level_ = true;
                return Change::Raised;
            }
            return Change::None;
        }
        if (!level_) {
            return Change::None;
        }
        if (!lowPending_) {
            lowPending_ = true;
            lowSince_ = now;
            return Change::None;
        }
        if (now - lowSince_ >= dropDelay_) {
            level_ = false;
            lowPending_ = false;
            return Change::Dropped;
        }
        return Change::None;
    }

    bool Level() const { return level_; }

private:
    std::chrono::milliseconds dropDelay_;
    bool level_ = false;
    bool lowPending_ = false;
    Clock::time_point lowSince_{};
};

} // namespace voipmodem
