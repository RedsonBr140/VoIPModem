#include "CallAudioBridge.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace voipmodem {

namespace {

constexpr int kBitsPerSample = 16;
constexpr int kFrameTimeUsec = 20000; // 20ms -> 160 samples at 8kHz

int16_t FloatToPcm16(float sample) {
    const float clamped = std::clamp(sample, -1.0f, 1.0f);
    return static_cast<int16_t>(clamped * 32767.0f);
}

float Pcm16ToFloat(int16_t sample) {
    return static_cast<float>(sample) / 32768.0f;
}

dsp::V8CallMenu MakeLocalMenu() {
    dsp::V8CallMenu menu;
    menu.callFunction = dsp::V8CallFunction::Data;
    dsp::V8ModulationModes modes;
    modes.v21 = true; // the only modulation this codebase actually implements
    menu.modulationModes = modes;
    return menu;
}

std::string DescribeModulationModes(const dsp::V8ModulationModes& modes) {
    const auto selected = dsp::SelectModulationMode(modes);
    if (!selected.has_value()) {
        return "none in common";
    }
    switch (selected.value()) {
        case dsp::V8ModulationModeItem::V34Duplex: return "V.34 (duplex)";
        case dsp::V8ModulationModeItem::V34HalfDuplex: return "V.34 (half-duplex)";
        case dsp::V8ModulationModeItem::V32bisOrV32: return "V.32bis/V.32";
        case dsp::V8ModulationModeItem::V22bisOrV22: return "V.22bis/V.22";
        case dsp::V8ModulationModeItem::V17: return "V.17";
        case dsp::V8ModulationModeItem::V29HalfDuplex: return "V.29 (half-duplex)";
        case dsp::V8ModulationModeItem::V27ter: return "V.27ter";
        case dsp::V8ModulationModeItem::V26ter: return "V.26ter";
        case dsp::V8ModulationModeItem::V26bis: return "V.26bis";
        case dsp::V8ModulationModeItem::V23Duplex: return "V.23 (duplex)";
        case dsp::V8ModulationModeItem::V23HalfDuplex: return "V.23 (half-duplex)";
        case dsp::V8ModulationModeItem::V21: return "V.21";
    }
    return "unknown";
}

} // namespace

CallAudioBridge::CallAudioBridge(bool callingParty, TaggedLogger& logger)
    : callingParty_(callingParty),
      logger_(logger),
      handshake_(callingParty ? dsp::V8Handshake::Role::CallDce : dsp::V8Handshake::Role::AnswerDce, MakeLocalMenu(),
                 logger) {}

void CallAudioBridge::Start(pj::AudioMedia& callAudio) {
    pj::MediaFormatAudio format;
    format.init(PJMEDIA_FORMAT_PCM, dsp::kSampleRateHz, 1, kFrameTimeUsec, kBitsPerSample);
    createPort("voipmodem-v8-handshake", format);

    // Bidirectional: call audio -> this port (onFrameReceived), and this
    // port -> call audio (onFrameRequested).
    callAudio.startTransmit(*this);
    startTransmit(callAudio);

    logger_.Info(std::string("CallAudioBridge started (") + (callingParty_ ? "calling party" : "answering party") +
                 "); running the real ITU-T V.8 handshake (ANSam/CM/JM/CJ), advertising V.21 only");
}

void CallAudioBridge::onFrameRequested(pj::MediaFrame& frame) {
    const unsigned capacityBytes = frame.size;
    const unsigned sampleCount = capacityBytes / sizeof(int16_t);

    frame.buf.resize(capacityBytes);
    frame.type = PJMEDIA_FRAME_TYPE_AUDIO;

    for (unsigned i = 0; i < sampleCount; ++i) {
        const float sample = (i < pendingTxSamples_.size()) ? pendingTxSamples_[i] : 0.0f;
        const int16_t pcm = FloatToPcm16(sample);
        frame.buf[2 * i] = static_cast<unsigned char>(pcm & 0xFF);
        frame.buf[2 * i + 1] = static_cast<unsigned char>((pcm >> 8) & 0xFF);
    }
}

void CallAudioBridge::onFrameReceived(pj::MediaFrame& frame) {
    const unsigned sampleCount = static_cast<unsigned>(frame.buf.size()) / sizeof(int16_t);
    if (sampleCount == 0) {
        return;
    }

    std::vector<dsp::Sample> samples;
    samples.reserve(sampleCount);
    for (unsigned i = 0; i < sampleCount; ++i) {
        const int16_t pcm = static_cast<int16_t>(frame.buf[2 * i] | (frame.buf[2 * i + 1] << 8));
        samples.push_back(Pcm16ToFloat(pcm));
    }

    handshake_.ProcessFrame(samples, pendingTxSamples_, static_cast<int>(sampleCount));

    if (!loggedOutcome_ && handshake_.GetOutcome() == dsp::V8HandshakeOutcome::Negotiated) {
        loggedOutcome_ = true;
        const auto& remoteMenu = handshake_.GetRemoteMenu();
        std::string modeDescription = "unknown";
        if (remoteMenu.has_value() && remoteMenu.value().modulationModes.has_value()) {
            modeDescription = DescribeModulationModes(remoteMenu.value().modulationModes.value());
        }
        logger_.Info("CallAudioBridge: V.8 handshake negotiated -- selected mode: " + modeDescription);
    }
}

} // namespace voipmodem
