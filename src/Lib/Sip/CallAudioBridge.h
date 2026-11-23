#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

#include <pjsua2.hpp>

#include "../Audio/WindowsAudioOutput.h"
#include "../Dsp/FskDemodulator.h"
#include "../Dsp/FskModulator.h"
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
// DATA MODE (added for Phase 4's first real "modem talks to modem"
// milestone -- see docs/ROADMAP.md): once `handshake_` reaches
// Negotiated and the selected mode is V.21, this class stops running
// V8Handshake (it has nothing left to do) and instead runs a real
// bidirectional V.21 FSK data link -- the same `dsp::FskModulator`/
// `FskDemodulator` pair this class used for an earlier probe-only
// increment, now carrying actual application bytes instead of a
// counter. Channel assignment matches V8Handshake's own convention:
// the calling party transmits on channel 1 (980/1180 Hz) and listens
// on channel 2 (1650/1850 Hz); the answering party is the mirror
// image. `EnqueueTxByte`/`DrainRxBytes` are the only thread-safe
// surface -- called from whatever thread owns the active
// ICallController (ModemInstance's), while the actual modulation/
// demodulation happens on pjsua2's own media thread inside
// onFrameReceived/onFrameRequested. Bytes enqueued before the link
// activates (i.e. before V8 finishes negotiating) simply wait in the
// queue; nothing is dropped, so there is no race to get right between
// CONNECT (reported as soon as SIP media is up -- see
// ModemStateMachine) and the data link actually being ready a moment
// later. V.34 never reaches this: Phase 2 line probing stops at L1
// (see above), so V.34 calls never set dataLinkActive_.
//
// If `enableLocalAudio` is true, also mirrors every frame's RX+TX
// audio to the local PC's default sound output device
// (`WindowsAudioOutput`) -- the actual answer to "how do I hear the
// modem". Per-line, opt-in via `ModemInstanceConfig::enableLocalAudio`
// (ini: `EnableLocalAudio=1`), default false: two VoIPModem instances
// calling each other on the SAME machine (the com0com test rig) would
// otherwise both open the shared default device and mix into one
// indistinguishable stream, so enable it on whichever single line
// you actually want to listen to. Has zero effect on the real call
// either way -- it's purely a local monitor. Which device and how
// loud is entirely Windows' own Sound settings/Volume Mixer, same as
// any other app's audio -- this class has no volume/device control of
// its own, and deliberately does not yet honor ATL/ATM (see
// ModemProfile.h).
class CallAudioBridge : public pj::AudioMediaPort {
public:
    CallAudioBridge(bool callingParty, TaggedLogger& logger, bool enableV34Probing = false,
                     bool enableLocalAudio = false);

    // Creates the port and wires it bidirectionally to `callAudio` (the
    // active call's audio media, as handed to SipCall::onCallMediaState).
    // Throws pj::Error on failure.
    void Start(pj::AudioMedia& callAudio);

    // Enqueues one byte for transmission once the V.21 data link is
    // active (see class comment's "DATA MODE" section). Thread-safe;
    // bytes enqueued before the link activates simply wait.
    void EnqueueTxByte(std::uint8_t byte);

    // Drains and returns every byte decoded from the far end since the
    // last call. Thread-safe; returns empty if nothing new.
    std::vector<std::uint8_t> DrainRxBytes();

protected:
    void onFrameRequested(pj::MediaFrame& frame) override;
    void onFrameReceived(pj::MediaFrame& frame) override;

private:
    // Sums rxSamples and txSamples and writes the mix to localSpeaker_
    // (a no-op if no output device opened). See class comment.
    void PlayLocally(const std::vector<dsp::Sample>& rxSamples, const std::vector<dsp::Sample>& txSamples);

    // Constructs dataModulator_/dataDemodulator_ with the correct
    // channel assignment and sets dataLinkActive_ -- called exactly
    // once, from onFrameReceived, the moment V8Handshake negotiates
    // V.21. See class comment's "DATA MODE" section.
    void StartDataLink();

    // Runs the active V.21 data link for one frame: decodes any new
    // bytes from rxSamples into rxByteQueue_, and refills
    // pendingTxSamples_ (exactly sampleCount samples) from
    // txByteQueue_, or a continuous mark/idle tone when that queue is
    // empty. Called from onFrameReceived once dataLinkActive_ is true.
    void RunDataLink(const std::vector<dsp::Sample>& rxSamples, int sampleCount);

    bool callingParty_;
    TaggedLogger& logger_;
    bool enableV34Probing_;
    dsp::V8Handshake handshake_;
    std::vector<dsp::Sample> pendingTxSamples_;
    bool loggedOutcome_ = false;
    std::optional<dsp::V34Phase2Handshake> phase2Handshake_;
    bool loggedRtd_ = false;
    bool loggedL1_ = false;
    std::optional<WindowsAudioOutput> localSpeaker_; // only constructed if enableLocalAudio was true

    // Data-mode state -- see class comment's "DATA MODE" section and
    // StartDataLink()/RunDataLink()'s own comments.
    bool dataLinkActive_ = false;
    std::optional<dsp::FskModulator> dataModulator_;
    std::optional<dsp::FskDemodulator> dataDemodulator_;
    std::vector<dsp::Sample> dataTxSampleBuffer_; // generated-but-not-yet-sent TX audio
    std::vector<bool> pendingRxBits_;              // decoded bits not yet assembled into a byte
    size_t rxBitCursor_ = 0;                       // how far into pendingRxBits_ TryDecodeAsyncByte has consumed
    std::mutex dataMutex_;                         // guards txByteQueue_/rxByteQueue_ only
    std::deque<std::uint8_t> txByteQueue_;
    std::deque<std::uint8_t> rxByteQueue_;
};

} // namespace voipmodem
