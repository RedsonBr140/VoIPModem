#include "CallAudioBridge.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

namespace voipmodem {

namespace {

constexpr int kBitsPerSample = 16;
constexpr int kFrameTimeUsec = 20000; // 20ms -> 160 samples at 8kHz
constexpr dsp::Sample kV34NominalAmplitude = 0.8f;

// Same V.21 channel table and baud rate V8Handshake already uses
// (confirmed against V.21's own frequency table, not re-derived here --
// see FskModulator.h's own class comment) -- duplicated rather than
// shared across translation units, matching this codebase's existing
// style (V8Handshake.cpp keeps its own copy too).
constexpr double kChannel1MarkHz = 980.0;
constexpr double kChannel1SpaceHz = 1180.0;
constexpr double kChannel2MarkHz = 1650.0;
constexpr double kChannel2SpaceHz = 1850.0;
constexpr double kDataBaudRate = 300.0;
constexpr dsp::Sample kDataAmplitude = 0.6f;

int16_t FloatToPcm16(float sample) {
    const float clamped = std::clamp(sample, -1.0f, 1.0f);
    return static_cast<int16_t>(clamped * 32767.0f);
}

float Pcm16ToFloat(int16_t sample) {
    return static_cast<float>(sample) / 32768.0f;
}

dsp::V8CallMenu MakeLocalMenu(bool advertiseV34) {
    dsp::V8CallMenu menu;
    menu.callFunction = dsp::V8CallFunction::Data;
    dsp::V8ModulationModes modes;
    modes.v21 = true; // the only modulation this codebase implements end to end
    // Only advertised when explicitly opted in -- see
    // ModemInstanceConfig::enableV34Probing's comment for why this
    // isn't on by default: a real V.34-capable remote modem would
    // commit to training this codebase can't finish past Phase 2's
    // first RTD/L1 exchange.
    modes.v34Duplex = advertiseV34;
    menu.modulationModes = modes;
    return menu;
}

bool IsV34Mode(dsp::V8ModulationModeItem item) {
    return item == dsp::V8ModulationModeItem::V34Duplex || item == dsp::V8ModulationModeItem::V34HalfDuplex;
}

std::string DescribeModulationModes(dsp::V8ModulationModeItem selected) {
    switch (selected) {
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

CallAudioBridge::CallAudioBridge(bool callingParty, TaggedLogger& logger, bool enableV34Probing, bool enableLocalAudio)
    : callingParty_(callingParty),
      logger_(logger),
      enableV34Probing_(enableV34Probing),
      handshake_(callingParty ? dsp::V8Handshake::Role::CallDce : dsp::V8Handshake::Role::AnswerDce,
                 MakeLocalMenu(enableV34Probing), logger) {
    if (enableLocalAudio) {
        localSpeaker_.emplace();
    }
}

void CallAudioBridge::Start(pj::AudioMedia& callAudio) {
    pj::MediaFormatAudio format;
    format.init(PJMEDIA_FORMAT_PCM, dsp::kSampleRateHz, 1, kFrameTimeUsec, kBitsPerSample);
    createPort("voipmodem-v8-handshake", format);

    // Bidirectional: call audio -> this port (onFrameReceived), and this
    // port -> call audio (onFrameRequested).
    callAudio.startTransmit(*this);
    startTransmit(callAudio);

    logger_.Info(std::string("CallAudioBridge started (") + (callingParty_ ? "calling party" : "answering party") +
                 "); running the real ITU-T V.8 handshake (ANSam/CM/JM/CJ), advertising V.21" +
                 (enableV34Probing_ ? "+V.34 (probing only, not a connection)" : " only") +
                 (localSpeaker_.has_value() ? "; local audio monitor enabled" : ""));
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

void CallAudioBridge::PlayLocally(const std::vector<dsp::Sample>& rxSamples,
                                   const std::vector<dsp::Sample>& txSamples) {
    if (!localSpeaker_.has_value() || !localSpeaker_->IsAvailable()) {
        return;
    }
    // A real phone line acoustically mixes both directions onto the one
    // wire a monitoring speaker taps; mirror that by summing RX (the
    // far end's tones) and TX (this side's own) rather than playing
    // only one direction.
    std::vector<dsp::Sample> mixed(std::max(rxSamples.size(), txSamples.size()), 0.0f);
    for (size_t i = 0; i < rxSamples.size(); ++i) {
        mixed[i] += rxSamples[i];
    }
    for (size_t i = 0; i < txSamples.size(); ++i) {
        mixed[i] += txSamples[i];
    }
    localSpeaker_->Write(mixed);
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

    // Once V8 has handed off to V.34 Phase 2, that state machine owns
    // this call's audio from here on -- V8Handshake has nothing left
    // to do once Negotiated.
    if (phase2Handshake_.has_value()) {
        phase2Handshake_->ProcessFrame(samples, pendingTxSamples_, static_cast<int>(sampleCount));
        PlayLocally(samples, pendingTxSamples_);

        if (!loggedRtd_ && phase2Handshake_->GetOutcome() == dsp::V34Phase2Outcome::RoundTripDelayMeasured) {
            loggedRtd_ = true;
            const double rtdMs = phase2Handshake_->GetRoundTripDelaySeconds() * 1000.0;
            logger_.Info("CallAudioBridge: V.34 Phase 2 round-trip delay measured: " + std::to_string(rtdMs) + "ms");
        }
        if (!loggedL1_ && phase2Handshake_->GetHasReceivedL1()) {
            loggedL1_ = true;
            logger_.Info("CallAudioBridge: V.34 Phase 2 received L1 (not yet implemented past this point)");
        }
        return;
    }

    // Once V8 has handed off to the real V.21 data link, that owns this
    // call's audio from here on too -- V8Handshake has nothing left to
    // do once Negotiated. See class comment's "DATA MODE" section.
    if (dataLinkActive_) {
        RunDataLink(samples, static_cast<int>(sampleCount));
        PlayLocally(samples, pendingTxSamples_);
        return;
    }

    handshake_.ProcessFrame(samples, pendingTxSamples_, static_cast<int>(sampleCount));
    PlayLocally(samples, pendingTxSamples_);

    if (!loggedOutcome_ && handshake_.GetOutcome() == dsp::V8HandshakeOutcome::Negotiated) {
        loggedOutcome_ = true;
        const auto& remoteMenu = handshake_.GetRemoteMenu();
        std::optional<dsp::V8ModulationModeItem> selected;
        if (remoteMenu.has_value() && remoteMenu.value().modulationModes.has_value()) {
            selected = dsp::SelectModulationMode(remoteMenu.value().modulationModes.value());
        }
        const std::string modeDescription = selected.has_value() ? DescribeModulationModes(selected.value())
                                                                   : "none in common";
        logger_.Info("CallAudioBridge: V.8 handshake negotiated -- selected mode: " + modeDescription);

        if (enableV34Probing_ && selected.has_value() && IsV34Mode(selected.value())) {
            phase2Handshake_.emplace(callingParty_ ? dsp::V34Phase2Handshake::Role::CallDce
                                                    : dsp::V34Phase2Handshake::Role::AnswerDce,
                                      kV34NominalAmplitude, logger_);
            logger_.Info("CallAudioBridge: starting V.34 Phase 2 line probing");
        } else if (selected.has_value() && selected.value() == dsp::V8ModulationModeItem::V21) {
            StartDataLink();
        }
    }
}

void CallAudioBridge::StartDataLink() {
    // Same channel assignment V8Handshake already uses: the calling
    // party transmits on channel 1 and listens on channel 2; the
    // answering party is the mirror image.
    const double txMark = callingParty_ ? kChannel1MarkHz : kChannel2MarkHz;
    const double txSpace = callingParty_ ? kChannel1SpaceHz : kChannel2SpaceHz;
    const double rxMark = callingParty_ ? kChannel2MarkHz : kChannel1MarkHz;
    const double rxSpace = callingParty_ ? kChannel2SpaceHz : kChannel1SpaceHz;

    dataModulator_.emplace(txMark, txSpace, kDataBaudRate, kDataAmplitude, dsp::kSampleRateHz);
    dataDemodulator_.emplace(rxMark, rxSpace, kDataBaudRate, dsp::kSampleRateHz);
    dataLinkActive_ = true;
    logger_.Info("CallAudioBridge: V.21 data link active -- real byte passthrough now running");
}

void CallAudioBridge::RunDataLink(const std::vector<dsp::Sample>& rxSamples, int sampleCount) {
    // RX: decode new bits, assemble async-framed bytes from them, and
    // queue whatever comes out for the application to drain.
    const auto newBits = dataDemodulator_->ProcessSamples(rxSamples);
    pendingRxBits_.insert(pendingRxBits_.end(), newBits.begin(), newBits.end());
    for (;;) {
        if (pendingRxBits_.size() - rxBitCursor_ < 10) {
            break; // not enough bits yet for even one async-framed byte
        }
        const auto decoded = dsp::FskDemodulator::TryDecodeAsyncByte(pendingRxBits_, rxBitCursor_);
        if (decoded.has_value()) {
            std::lock_guard<std::mutex> lock(dataMutex_);
            rxByteQueue_.push_back(*decoded);
            continue;
        }
        ++rxBitCursor_; // genuine framing mismatch (wrong start/stop bit) -- resync by one bit
    }
    // Periodically compact pendingRxBits_ so it doesn't grow without
    // bound over a long call -- same technique this codebase already
    // used for this exact purpose in an earlier increment (see
    // docs/ROADMAP.md Phase 4).
    if (rxBitCursor_ > 4096) {
        pendingRxBits_.erase(pendingRxBits_.begin(), pendingRxBits_.begin() + static_cast<long>(rxBitCursor_));
        rxBitCursor_ = 0;
    }

    // TX: keep dataTxSampleBuffer_ topped up to at least sampleCount,
    // generating from queued application bytes when there are any, or
    // a continuous mark ("idle") tone otherwise -- exactly what a real
    // async line does between bytes.
    while (dataTxSampleBuffer_.size() < static_cast<size_t>(sampleCount)) {
        std::optional<std::uint8_t> nextByte;
        {
            std::lock_guard<std::mutex> lock(dataMutex_);
            if (!txByteQueue_.empty()) {
                nextByte = txByteQueue_.front();
                txByteQueue_.pop_front();
            }
        }
        if (nextByte.has_value()) {
            dataModulator_->ModulateByteAsync(dataTxSampleBuffer_, *nextByte);
        } else {
            dataModulator_->ModulateBit(dataTxSampleBuffer_, /*bit=*/true); // mark = idle
        }
    }

    pendingTxSamples_.assign(dataTxSampleBuffer_.begin(), dataTxSampleBuffer_.begin() + sampleCount);
    dataTxSampleBuffer_.erase(dataTxSampleBuffer_.begin(), dataTxSampleBuffer_.begin() + sampleCount);
}

void CallAudioBridge::EnqueueTxByte(std::uint8_t byte) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    txByteQueue_.push_back(byte);
}

std::vector<std::uint8_t> CallAudioBridge::DrainRxBytes() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    std::vector<std::uint8_t> result(rxByteQueue_.begin(), rxByteQueue_.end());
    rxByteQueue_.clear();
    return result;
}

} // namespace voipmodem
