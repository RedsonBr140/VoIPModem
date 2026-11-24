#include "ServiceStats.h"

#include <algorithm>

namespace voipmodem {

void ServiceStats::IncomingOffered() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++s_.incomingOffered;
}

void ServiceStats::InboundAnswered() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++s_.callsAnswered;
}

void ServiceStats::OutboundDialled() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++s_.outgoingDialled;
}

void ServiceStats::Connected(bool inbound, const std::string& modulation, int txBps, int rxBps) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (inbound) {
        ++s_.trained;
    } else {
        ++s_.outgoingConnected;
    }
    const std::string key = (modulation.empty() ? std::string("?") : modulation) + " " +
                            std::to_string(std::max(txBps, rxBps));
    ++s_.connectsByMode[key];
}

void ServiceStats::FailedToTrain() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++s_.failedToTrain;
}

void ServiceStats::CallerHungUpEarly() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++s_.hungUpEarly;
}

void ServiceStats::RejectedBusy(unsigned long count) {
    std::lock_guard<std::mutex> lock(mutex_);
    s_.rejectedBusy += count;
}

void ServiceStats::CallCompleted(double connectedSeconds, unsigned retrains, unsigned renegotiations) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++s_.completedCalls;
    s_.totalConnectedSeconds += connectedSeconds;
    s_.longestCallSeconds = std::max(s_.longestCallSeconds, connectedSeconds);
    s_.retrains += retrains;
    s_.renegotiations += renegotiations;
}

void ServiceStats::Bytes(std::uint64_t in, std::uint64_t out) {
    std::lock_guard<std::mutex> lock(mutex_);
    s_.bytesIn += in;
    s_.bytesOut += out;
}

void ServiceStats::AtCommands(unsigned long count) {
    std::lock_guard<std::mutex> lock(mutex_);
    s_.atCommands += count;
}

void ServiceStats::ObserveBusyLines(int busy) {
    std::lock_guard<std::mutex> lock(mutex_);
    s_.peakBusyLines = std::max(s_.peakBusyLines, busy);
}

ServiceStats::Snapshot ServiceStats::Get() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return s_;
}

} // namespace voipmodem
