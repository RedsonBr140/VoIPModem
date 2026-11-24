#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <pjsua2.hpp>

#include "../Audio/WindowsAudioOutput.h"
#include "../Logging/TaggedLogger.h"

// libv90modem's opaque handles (third_party/libv90modem/include/v90modem.h),
// forward-declared so its C header stays out of every includer.
struct v90m;
struct v90m_ec;

namespace voipmodem {

// Phase 4's call-media wiring: runs a real softmodem on the call's audio.
//
// The modem itself is libv90modem (third_party/libv90modem, GPL-2.0-only):
// V.8, then whatever V.8 (or a pre-V.8 answer tone) selects -- V.21,
// V.22/V.22bis, V.32bis, V.34, V.90/V.92 -- with a V.42 endpoint (LAPM,
// V.42bis/V.44, falling back to plain start-stop by itself when the far
// end has no error control) on the modem's bit pump. This replaced the
// hand-written src/Lib/Dsp handshake/V.21 path on 2026-11-24 (see
// docs/ROADMAP.md Phase 4); the old classes are still in the tree but no
// longer used here.
//
// Threading: pjsua2 calls onFrameReceived (RX) and onFrameRequested (TX)
// on its own media thread. The modem, the V.42 endpoint and every
// callback of theirs run only there, inside onFrameReceived: one 20 ms
// frame of received samples in, one frame to send computed and buffered
// in pendingTxSamples_ for the next onFrameRequested. The only
// thread-safe surface is EnqueueTxByte/DrainRxBytes and the status
// getters, used from ModemInstance's thread. Bytes enqueued before the
// modem connects simply wait.
//
// Samples: pjmedia's 16-bit PCM is the same scale as V.90 Table 1, which
// libv90modem works in (0 dBm0 = RMS 15770), so frames pass through
// v90m_process_linear unscaled. V.90/V.92 additionally need the far end
// to receive our exact G.711 codewords: that holds only if nothing
// between us and the far modem (pjmedia's conference bridge level, PLC,
// Asterisk transcoding) alters samples.
//
// Not done yet: when the modem ends (no answer tone, no common mode,
// carrier lost, cleardown), this only logs it and reports it through
// HasModemEnded(); hanging up the SIP call is still left to the far end
// or ATH.
//
// If `enableLocalAudio` is true, every frame's RX+TX mix also goes to
// the PC's default sound output (WindowsAudioOutput), as a modem speaker.
class CallAudioBridge : public pj::AudioMediaPort {
public:
    // `alaw`: the call's negotiated codec is PCMA (else PCMU).
    // `enableV34Probing` is accepted for config compatibility and ignored:
    // libv90modem offers V.34 by default.
    CallAudioBridge(bool callingParty, TaggedLogger& logger, bool enableV34Probing = false,
                     bool enableLocalAudio = false, bool alaw = false, const std::string& modulations = "");
    ~CallAudioBridge() override;

    CallAudioBridge(const CallAudioBridge&) = delete;
    CallAudioBridge& operator=(const CallAudioBridge&) = delete;

    // Creates the port and wires it bidirectionally to `callAudio`.
    // Throws pj::Error on failure.
    void Start(pj::AudioMedia& callAudio);

    // Thread-safe. Bytes wait until the modem's data link is up.
    void EnqueueTxByte(std::uint8_t byte);

    // Thread-safe; every byte received from the far end since last call.
    std::vector<std::uint8_t> DrainRxBytes();

    // Thread-safe: the modem stopped (see class comment).
    bool HasModemEnded() const;

    // libv90modem's getbit/putbit/on_event, reached through C callbacks
    // in the .cpp; media thread only. Not for other callers.
    int ModemGetBit();
    void ModemPutBit(int bit);
    void OnModemEvent(int event);

protected:
    void onFrameRequested(pj::MediaFrame& frame) override;
    void onFrameReceived(pj::MediaFrame& frame) override;

private:
    void PumpErrorControl(unsigned elapsedMs);
    void LogErrorControlState();
    void PlayLocally(const std::vector<std::int16_t>& rx, const std::vector<std::int16_t>& tx);

    bool callingParty_;
    TaggedLogger& logger_;
    std::mutex frameMutex_; // media-thread frame work vs. destruction: see ~CallAudioBridge
    v90m* modem_ = nullptr;
    v90m_ec* ec_ = nullptr;
    bool ecActive_ = false; // reset at V90M_EV_CONNECTED; bits flow through it from then on
    int lastEcState_ = -1;
    std::string modulationsDescription_; // for the start log line
    std::vector<std::int16_t> pendingTxSamples_;
    std::optional<WindowsAudioOutput> localSpeaker_;

    mutable std::mutex dataMutex_; // guards the queues and modemEnded_
    std::deque<std::uint8_t> txByteQueue_;
    std::deque<std::uint8_t> rxByteQueue_;
    bool modemEnded_ = false;
};

} // namespace voipmodem
