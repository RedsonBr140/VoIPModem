#pragma once

#include <cstdint>
#include <vector>

#include <pjsua2.hpp>

#include "../Dsp/V8Handshake.h"
#include "../Logging/TaggedLogger.h"

namespace voipmodem {

// Phase 4's call-media wiring. Runs the real ITU-T V.8 handshake
// (`dsp::V8Handshake`) across real call audio -- ANSam detection/
// generation, CM/JM exchange, CJ -- replacing this class's two earlier
// increments (a single-tone probe, then a raw FSK counter-byte link;
// see git history / docs/ROADMAP.md Phase 4 for both).
//
// This is still NOT a modem: V.8 only negotiates which modulation to
// use next, it doesn't do any modulation itself. The local menu this
// class advertises claims only V.21 (300 baud), the one modulation
// this codebase actually implements (`dsp::FskModulator`/
// `FskDemodulator`) -- it does not claim V.32bis/V.34/etc., per
// CLAUDE.md's rule against claiming a standard before it's
// implemented and interoperability-tested. Once V8Handshake reaches
// Negotiated, this class logs the outcome and stops -- there is no
// sigA/sigC data-mode modulation to hand off to yet.
//
// pjsua2 calls onFrameReceived (RX) and onFrameRequested (TX) as
// separate callbacks, but V8Handshake::ProcessFrame wants both at
// once. This class resolves that by doing the actual handshake work
// inside onFrameReceived (computing this frame's TX output as a side
// effect, buffered in `pendingTxSamples_`) and having onFrameRequested
// just drain that buffer -- silence on the first frame or two if
// onFrameRequested happens to fire before onFrameReceived ever has.
class CallAudioBridge : public pj::AudioMediaPort {
public:
    CallAudioBridge(bool callingParty, TaggedLogger& logger);

    // Creates the port and wires it bidirectionally to `callAudio` (the
    // active call's audio media, as handed to SipCall::onCallMediaState).
    // Throws pj::Error on failure.
    void Start(pj::AudioMedia& callAudio);

protected:
    void onFrameRequested(pj::MediaFrame& frame) override;
    void onFrameReceived(pj::MediaFrame& frame) override;

private:
    bool callingParty_;
    TaggedLogger& logger_;
    dsp::V8Handshake handshake_;
    std::vector<dsp::Sample> pendingTxSamples_;
    bool loggedOutcome_ = false;
};

} // namespace voipmodem
