#pragma once

#include <memory>
#include <string>

#include "../Logging/TaggedLogger.h"

// Forward-declare rather than pulling <pjsua2.hpp> into every translation
// unit that touches ModemInstance/Application — PJSIP headers are heavy
// and only PjsipEndpoint.cpp and PjsipCallController.cpp need them. This
// is the same reasoning as ICallController hiding pjsua2 entirely from
// Core: keep the SIP library's own types out of unrelated code.
namespace pj {
class Endpoint;
}

namespace voipmodem {

// Process-wide PJSIP library instance. There is exactly one of these for
// the whole application — pjsua2's Endpoint is a singleton in practice —
// shared by every ModemInstance's PjsipCallController, each of which
// registers its own SIP account (extension) against this one transport.
// See docs/ARCHITECTURE.md "SIP integration (Phase 2)" for why the
// transport/port is process-wide while accounts are per-instance.
class PjsipEndpoint {
public:
    explicit PjsipEndpoint(Logger& logger);
    ~PjsipEndpoint();

    PjsipEndpoint(const PjsipEndpoint&) = delete;
    PjsipEndpoint& operator=(const PjsipEndpoint&) = delete;

    // Creates the UDP transport on `localPort` and starts the library.
    // Returns false (and logs why) on failure. Call once at startup.
    bool Start(int localPort);

    // Must be called once on any thread that will create accounts/calls
    // or otherwise touch pjsua2 objects, other than the thread that
    // constructed this PjsipEndpoint. Safe to call more than once from
    // the same thread (checks libIsThreadRegistered() first).
    void RegisterCurrentThread(const std::string& threadName);

    pj::Endpoint& Raw();

private:
    // Disables every codec except PCMU/PCMA (G.711): per
    // docs/PROJECT_SPEC.md section 4.E/D, modem tones must travel over an
    // uncompressed G.711 path, never a lossy voice codec (Opus/Speex/GSM/
    // iLBC/...) that would corrupt them. Applied process-wide, after
    // libStart(), since PJSIP's codec priority table isn't per-account.
    void RestrictCodecsToG711();

    Logger& logger_;
    std::unique_ptr<pj::Endpoint> endpoint_;
    bool started_ = false;
};

} // namespace voipmodem
