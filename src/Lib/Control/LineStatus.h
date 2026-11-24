#pragma once

#include <cstdint>
#include <string>

namespace voipmodem {

// One modem line as the control API reports it (docs/CONTROL_API.md,
// "status"). Built by ModemInstance from its modem state machine, its call
// controller's CallStatus and its own call bookkeeping.
struct LineStatus {
    std::string id;           // the line's name ([Modem.<id>]); "" for a bare [Modem]
    std::string comPort;
    std::string extension;
    std::string server;

    // What the line is doing, for display, and its class:
    //   "up"   connected (data mode, or command mode with the call up)
    //   "neg"  ringing, answering, dialing, training
    //   "idle" waiting for calls
    //   "err"  port closed, not registered, last call failed
    //   "off"  out of service
    std::string state = "Starting";
    std::string kind = "idle";

    std::string direction;    // "in", "out" or "" (no call)
    std::string remoteParty;
    std::string modulation;   // "V.34", ...; "" until connected
    int txBps = 0;
    int rxBps = 0;
    double snrDb = -1;        // -1 = not measured
    long long callSeconds = -1; // since the call began (ringing / dialing); -1 = no call
    std::uint64_t bytesTx = 0;  // DTE data of the current or last call
    std::uint64_t bytesRx = 0;
    unsigned retrains = 0;
    unsigned long errors = 0;   // LAPM frames received bad (or start-stop framing errors)
    std::string errorControl;   // "V.42 LAPM", "none", ...
    std::string compression;    // "V.44", "V.42bis", "none"
    std::string codec;
    double jitterMeanMs = -1;
    double jitterMaxMs = -1;
    unsigned long rtpPacketsReceived = 0;
    unsigned long rtpPacketsLost = 0;
    std::uint64_t rtpBytes = 0;  // both directions, current call

    std::string registration;   // "registered", "registering", "failed", "unregistered", "none"
    int registrationCode = 0;
    bool inService = true;
    bool portOpen = false;
    std::string fault;          // why the line is faulted, if it is
    int autoAnswerRings = 0;    // S0 now
    bool canAnswer = false;     // an incoming call is ringing
    bool canHangUp = false;     // a call exists
};

} // namespace voipmodem
