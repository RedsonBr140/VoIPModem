#pragma once

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "PjsipEndpoint.h"
#include "../Core/ICallController.h"
#include "../Logging/TaggedLogger.h"

namespace voipmodem {

struct SipAccountConfig {
    std::string server;    // Asterisk host[:port], e.g. "10.174.7.172:5060"
    std::string extension; // AOR user part, e.g. "1003"
    std::string password;
    std::string realm = "*";
    bool enableLocalAudio = false; // see ModemInstanceConfig::enableLocalAudio's own comment
    std::string modulations;       // see ModemInstanceConfig::modulations's own comment
    bool errorControl = true;      // ModemInstanceConfig::errorControl
    bool compression = true;       // ModemInstanceConfig::compression
    std::string transport = "UDP"; // AppConfig::sipTransport
    int registerExpirySeconds = 300;
    bool registerOnStart = true;   // false: Start() creates the account unregistered
    int rtpPort = 4000;            // first local RTP port (AppConfig::rtpPort)
    int rtpPortRange = 0;          // 0 = no limit
};

// Real, PJSIP-backed ICallController: one SIP account (one modem
// instance's extension) registered against the process-wide
// PjsipEndpoint, with at most one call active at a time — matching a
// modem line's actual capability. See docs/ARCHITECTURE.md "SIP
// integration (Phase 2)" for the event model and the thread-safety
// reasoning (PJSIP callbacks fire on its own worker thread, never the
// ModemInstance thread that calls Dial()/Answer()/HangUp()/PollEvent()).
//
// pjsua2 types (pj::Account, pj::Call, ...) are deliberately confined to
// the .cpp — nothing outside this class needs to know PJSIP exists,
// exactly like NullCallController before it.
class PjsipCallController : public ICallController {
public:
    PjsipCallController(PjsipEndpoint& endpoint, SipAccountConfig config, TaggedLogger& logger);
    ~PjsipCallController() override;

    PjsipCallController(const PjsipCallController&) = delete;
    PjsipCallController& operator=(const PjsipCallController&) = delete;

    // Registers the SIP account. Returns false (and logs why) on failure
    // to even start registering (e.g. malformed config) — a rejected or
    // slow registration is reported asynchronously via onRegState logging,
    // not this return value, since registration itself is async in SIP.
    // Without a successful Start() the controller has no account: Dial()
    // ends in NO CARRIER and no call arrives, like the stub. ModemInstance
    // calls it once the line's COM port is open.
    bool Start();

    void Dial(const std::string& number) override;
    void Answer() override;
    void HangUp() override;
    CallEvent PollEvent() override;
    void SendData(const std::vector<std::uint8_t>& bytes) override;
    std::vector<std::uint8_t> PollReceivedData() override;
    CallStatus GetStatus() override;
    bool SetRegistered(bool registered) override;

    // Implementation detail exposed only so the nested pjsua2 Account/Call
    // subclasses (defined in the .cpp) can report back into this
    // controller. Not part of ICallController; not for use by anything
    // else.
    class Impl;

private:
    PjsipEndpoint& endpoint_;
    SipAccountConfig config_;
    TaggedLogger& logger_;
    std::unique_ptr<Impl> impl_;
};

} // namespace voipmodem
