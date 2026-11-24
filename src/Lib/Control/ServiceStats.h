#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace voipmodem {

// Service-wide call statistics since the service started, for the
// Manager's "Inbound call statistics" and "Negotiation & carrier" panes.
// Every line reports into one instance; all methods are thread-safe.
class ServiceStats {
public:
    struct Snapshot {
        unsigned long incomingOffered = 0;  // incoming calls that rang a line
        unsigned long callsAnswered = 0;    // incoming calls answered (ATA / auto-answer)
        unsigned long trained = 0;          // answered calls whose modems connected
        unsigned long failedToTrain = 0;    // answered calls whose modems never connected
        unsigned long rejectedBusy = 0;     // incoming calls refused: the line was in a call
        unsigned long hungUpEarly = 0;      // incoming calls that ended before being answered
        unsigned long outgoingDialled = 0;
        unsigned long outgoingConnected = 0;
        unsigned long completedCalls = 0;   // calls that connected and have ended
        double totalConnectedSeconds = 0;
        double longestCallSeconds = 0;
        int peakBusyLines = 0;
        std::uint64_t bytesIn = 0;          // received from the far end, to the DTEs
        std::uint64_t bytesOut = 0;
        unsigned long atCommands = 0;
        unsigned long retrains = 0;
        unsigned long renegotiations = 0;
        // Connections by "<modulation> <rate>" (e.g. "V.34 31200"), both
        // directions; rate = the faster direction.
        std::map<std::string, unsigned long> connectsByMode;
    };

    void IncomingOffered();
    void InboundAnswered();
    void OutboundDialled();
    void Connected(bool inbound, const std::string& modulation, int txBps, int rxBps);
    void FailedToTrain();
    void CallerHungUpEarly();
    void RejectedBusy(unsigned long count);
    void CallCompleted(double connectedSeconds, unsigned retrains, unsigned renegotiations);
    void Bytes(std::uint64_t in, std::uint64_t out);
    void AtCommands(unsigned long count);
    void ObserveBusyLines(int busy);

    Snapshot Get() const;

private:
    mutable std::mutex mutex_;
    Snapshot s_;
};

} // namespace voipmodem
