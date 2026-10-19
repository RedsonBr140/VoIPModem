#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <pjsua2.hpp>

#include "../Audio/WindowsAudioOutput.h"
#include "../Dsp/V34Phase2Handshake.h"
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
// this codebase actually implements end to end
// (`dsp::FskModulator`/`FskDemodulator`) -- it does not claim
// V.32bis/etc., per CLAUDE.md's rule against claiming a standard
// before it's implemented and interoperability-tested.
//
// If the negotiated mode turns out to be V.34 AND `enableV34Probing`
// was passed true (see ModemInstanceConfig::enableV34Probing's own
// comment for exactly why this is opt-in, not automatic), this class
// hands off from `dsp::V8Handshake` to `dsp::V34Phase2Handshake` for
// the rest of the call's audio, driving clause 11.2's actual Phase 2
// line-probing procedure (INFO0 exchange, the first Tone A/B
// reversal exchange, RTDEc/RTDEa, and L1) over real RTP audio --
// logging each milestone as it's reached. This is still not a
// connection: nothing past L1 is implemented (see
// V34Phase2Handshake.h and docs/ROADMAP.md), so a real call that gets
// this far simply goes quiet again once L1 is received, the same
// honest dead end a non-V.34 call already hits right after V8
// negotiation.
//
// pjsua2 calls onFrameReceived (RX) and onFrameRequested (TX) as
// separate callbacks, but V8Handshake::ProcessFrame wants both at
// once. This class resolves that by doing the actual handshake work
// inside onFrameReceived (computing this frame's TX output as a side
// effect, buffered in `pendingTxSamples_`) and having onFrameRequested
// just drain that buffer -- silence on the first frame or two if
// onFrameRequested happens to fire before onFrameReceived ever has.
//
// Also mirrors every frame's RX+TX audio to the local PC's default
// sound output device (`WindowsAudioOutput`) -- the actual answer to
// "how do I hear the modem" -- so this is always-on, not gated behind
// `enableV34Probing` or any other flag; it's purely a local monitor
// with zero effect on the real call. Which device and how loud is
// entirely Windows' own Sound settings/Volume Mixer, same as any other
// app's audio -- this class has no volume/device control of its own,
// and deliberately does not yet honor ATL/ATM (see ModemProfile.h).
class CallAudioBridge : public pj::AudioMediaPort {
public:
    CallAudioBridge(bool callingParty, TaggedLogger& logger, bool enableV34Probing = false);

    // Creates the port and wires it bidirectionally to `callAudio` (the
    // active call's audio media, as handed to SipCall::onCallMediaState).
    // Throws pj::Error on failure.
    void Start(pj::AudioMedia& callAudio);

protected:
    void onFrameRequested(pj::MediaFrame& frame) override;
    void onFrameReceived(pj::MediaFrame& frame) override;

private:
    // Sums rxSamples and txSamples and writes the mix to localSpeaker_
    // (a no-op if no output device opened). See class comment.
    void PlayLocally(const std::vector<dsp::Sample>& rxSamples, const std::vector<dsp::Sample>& txSamples);

    bool callingParty_;
    TaggedLogger& logger_;
    bool enableV34Probing_;
    dsp::V8Handshake handshake_;
    std::vector<dsp::Sample> pendingTxSamples_;
    bool loggedOutcome_ = false;
    std::optional<dsp::V34Phase2Handshake> phase2Handshake_;
    bool loggedRtd_ = false;
    bool loggedL1_ = false;
    WindowsAudioOutput localSpeaker_;
};

} // namespace voipmodem
