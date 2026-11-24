#pragma once

#include <cstdint>
#include <string>

namespace voipmodem {

// What the modem engine reports about the current (or last) call's modem
// link. Filled by CallAudioBridge from libv90modem's state and statistics.
struct ModemLinkStatus {
    bool active = false;      // a modem is running on this call's audio
    bool connected = false;   // data mode reached
    bool retraining = false;
    std::string modulation;   // "V.34", "V.90", ...; empty before connect
    int txBps = 0;
    int rxBps = 0;
    int snrDb10 = -1;         // 0.1 dB; -1 = not measured
    int roundTripSamples = -1;
    unsigned retrains = 0;
    unsigned renegotiations = 0;
    std::string errorControl; // "V.42 LAPM", "none", "detecting", ...
    std::string compression;  // "V.44", "V.42bis", "none"
    unsigned long frameErrors = 0;   // LAPM frames received bad
    unsigned long retransmits = 0;   // LAPM I-frames we resent
};

// A snapshot of one line's telephony side, for status display. All of it
// is informational: nothing decides call flow from it.
struct CallStatus {
    enum class Registration { None, Registering, Registered, Failed, Unregistered };
    Registration registration = Registration::None;
    int registrationCode = 0;        // last SIP status of a REGISTER
    std::string registrationReason;

    bool inCall = false;             // a call exists (alerting, dialing or up)
    bool alerting = false;           // an incoming call is waiting to be answered
    bool mediaActive = false;        // RTP flowing
    std::string remoteParty;         // caller / called user part (or display name)
    std::string codec;               // "PCMU" / "PCMA"
    ModemLinkStatus modem;

    // RTP receive statistics of the current call's audio stream.
    double jitterMeanMs = -1;
    double jitterMaxMs = -1;
    unsigned long rtpPacketsReceived = 0;
    unsigned long rtpPacketsLost = 0;
    std::uint64_t rtpBytesReceived = 0;
    std::uint64_t rtpBytesSent = 0;

    // Cumulative for the line's lifetime: incoming calls refused because
    // the line was already in a call.
    unsigned long rejectedBusy = 0;
};

} // namespace voipmodem
