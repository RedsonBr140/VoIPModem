#include "V8Handshake.h"

#include <cmath>

#include "V8Signal.h"

namespace voipmodem::dsp {

namespace {

// V.21 channel assignment: CM/CI always ride channel 1, JM always rides
// channel 2 -- fixed by the signal's identity, not by which party is
// transmitting (unlike CallAudioBridge's TX/RX framing, which swaps by
// role). Confirmed against V.21's frequency table; see FskModulator.h.
constexpr double kChannel1MarkHz = 980.0;
constexpr double kChannel1SpaceHz = 1180.0;
constexpr double kChannel2MarkHz = 1650.0;
constexpr double kChannel2SpaceHz = 1850.0;
constexpr double kBaudRate = 300.0;
constexpr float kFskAmplitude = 0.6f;
constexpr float kAnsamAmplitude = 0.6f;

constexpr double kTeSeconds = 0.5;             // 8.1.1's own stated minimum
constexpr double kAnswerInitialSilenceSeconds = 0.2; // 8.2's own stated minimum
constexpr double kAnsamCarrierHz = 2100.0;
constexpr double kAnsamDetectionThreshold = 0.2;     // well below ANSam's ~0.48-0.72 expected range
constexpr Sample kSyncAmplitudeThreshold = 0.05f; // "real signal, not silence" edge-sync threshold

const std::vector<bool> kCmJmPreamblePattern = [] {
    std::vector<bool> pattern(kV8PreambleOnes, kV8PreambleOnes + 10);
    pattern.insert(pattern.end(), kV8CmJmSyncBits, kV8CmJmSyncBits + 10);
    return pattern;
}();

bool PstnAccessEqual(const V8PstnAccess& a, const V8PstnAccess& b) {
    return a.callDceCellular == b.callDceCellular && a.answerDceCellular == b.answerDceCellular &&
           a.digitalNetwork == b.digitalNetwork;
}

bool PcmAvailabilityEqual(const V8PcmModemAvailability& a, const V8PcmModemAvailability& b) {
    return a.v90OrV92Analogue == b.v90OrV92Analogue && a.v90OrV92Digital == b.v90OrV92Digital && a.v91 == b.v91;
}

bool ModulationModesEqual(const V8ModulationModes& a, const V8ModulationModes& b) {
    return a.pcmModemAvailabilityPresent == b.pcmModemAvailabilityPresent && a.v34Duplex == b.v34Duplex &&
           a.v34HalfDuplex == b.v34HalfDuplex && a.v32bisOrV32 == b.v32bisOrV32 && a.v22bisOrV22 == b.v22bisOrV22 &&
           a.v17 == b.v17 && a.v29HalfDuplex == b.v29HalfDuplex && a.v27ter == b.v27ter && a.v26ter == b.v26ter &&
           a.v26bis == b.v26bis && a.v23Duplex == b.v23Duplex && a.v23HalfDuplex == b.v23HalfDuplex &&
           a.v21 == b.v21;
}

bool MenusEqual(const V8CallMenu& a, const V8CallMenu& b) {
    if (a.callFunction != b.callFunction) return false;
    if (a.modulationModes.has_value() != b.modulationModes.has_value()) return false;
    if (a.modulationModes.has_value() && !ModulationModesEqual(a.modulationModes.value(), b.modulationModes.value()))
        return false;
    if (a.protocol != b.protocol) return false;
    if (a.pstnAccess.has_value() != b.pstnAccess.has_value()) return false;
    if (a.pstnAccess.has_value() && !PstnAccessEqual(a.pstnAccess.value(), b.pstnAccess.value())) return false;
    if (a.pcmModemAvailability.has_value() != b.pcmModemAvailability.has_value()) return false;
    if (a.pcmModemAvailability.has_value() &&
        !PcmAvailabilityEqual(a.pcmModemAvailability.value(), b.pcmModemAvailability.value()))
        return false;
    return true;
}

} // namespace

V8Handshake::V8Handshake(Role role, V8CallMenu localMenu, TaggedLogger& logger)
    : role_(role),
      localMenu_(localMenu),
      logger_(logger),
      state_(role == Role::CallDce ? State::WaitingForAnsam : State::InitialSilence),
      ansamDetector_(kSampleRateHz),
      cmModulator_(kChannel1MarkHz, kChannel1SpaceHz, kBaudRate, kFskAmplitude, kSampleRateHz),
      ansamGenerator_(kAnsamAmplitude, kSampleRateHz),
      jmModulator_(kChannel2MarkHz, kChannel2SpaceHz, kBaudRate, kFskAmplitude, kSampleRateHz) {
    if (role_ == Role::AnswerDce) {
        silenceSamplesRemaining_ = static_cast<int>(kAnswerInitialSilenceSeconds * kSampleRateHz);
    }
}

void V8Handshake::FillSilence(std::vector<Sample>& out, int frameSampleCount) {
    out.assign(static_cast<size_t>(frameSampleCount), static_cast<Sample>(0.0));
}

void V8Handshake::FillFromRepeatingSequence(std::vector<Sample>& out, int frameSampleCount,
                                             std::deque<Sample>& pendingTx, FskModulator& modulator,
                                             const V8CallMenu& menu) {
    while (pendingTx.size() < static_cast<size_t>(frameSampleCount)) {
        const std::vector<Sample> sequence = BuildCmOrJmSequence(modulator, menu);
        pendingTx.insert(pendingTx.end(), sequence.begin(), sequence.end());
    }
    out.clear();
    out.reserve(static_cast<size_t>(frameSampleCount));
    for (int i = 0; i < frameSampleCount; ++i) {
        out.push_back(pendingTx.front());
        pendingTx.pop_front();
    }
}

namespace {

// Lazily constructs `demod` fresh the moment a sample louder than
// silence is first seen, feeding it only the samples from that point
// onward -- see V8Handshake.h's class comment for why this matters
// (there is no real symbol-timing/clock recovery in this codebase; this
// is a pragmatic substitute that only works because whatever precedes
// CM/JM on their own channel is genuine silence here).
//
// `competingHz`/`competingThreshold` (optional, 0.0 disables): a real
// call's RTP/jitter-buffer latency means the *previous* tone on the
// shared audio can still be arriving for a couple hundred ms after the
// far end's own state machine has already moved past it -- found live
// testing two real VoIPModem instances over Asterisk (2026-10-17): the
// call DCE's JM wait kept locking onto trailing ANSam (2100Hz) energy
// that was still in flight when JM actually started, permanently
// misaligning the receiver (no clock recovery to self-correct). When
// set, sync is withheld until energy at `competingHz` has also dropped
// below `competingThreshold`, so sync can't lock onto the tail of a
// tone that's supposed to have already stopped.
std::vector<bool> FeedSyncedDemodulator(std::optional<FskDemodulator>& demod, double markHz, double spaceHz,
                                         const std::vector<Sample>& rxSamples, double competingHz = 0.0,
                                         float competingThreshold = 0.0f) {
    if (demod.has_value()) {
        return demod->ProcessSamples(rxSamples);
    }
    if (rxSamples.empty()) {
        return {};
    }

    // Frequency-selective gate first: ignore loud audio that isn't
    // actually at this channel's own mark/space frequencies -- e.g.
    // ANSam's 2100Hz, which is still playing on the shared audio while
    // this side waits for real CM/JM. Only energy at the target
    // frequencies counts as "real signal has started" for sync
    // purposes; a plain amplitude check would sync to the wrong thing.
    GoertzelDetector markCheck(markHz, kSampleRateHz);
    GoertzelDetector spaceCheck(spaceHz, kSampleRateHz);
    for (Sample s : rxSamples) {
        markCheck.Process(s);
        spaceCheck.Process(s);
    }
    const int blockSize = static_cast<int>(rxSamples.size());
    const float markMag = static_cast<float>(markCheck.Magnitude(blockSize));
    const float spaceMag = static_cast<float>(spaceCheck.Magnitude(blockSize));

    float competingMag = -1.0f;
    if (competingHz > 0.0) {
        GoertzelDetector competingCheck(competingHz, kSampleRateHz);
        for (Sample s : rxSamples) {
            competingCheck.Process(s);
        }
        competingMag = static_cast<float>(competingCheck.Magnitude(blockSize));
    }

    if (markMag < kSyncAmplitudeThreshold && spaceMag < kSyncAmplitudeThreshold) {
        return {}; // nothing at our own frequencies yet -- still "silence" for sync purposes
    }

    if (competingHz > 0.0 && competingMag >= competingThreshold) {
        return {}; // the previous tone hasn't actually died down yet -- not real sync
    }

    // Now find the precise sample-level onset within this frame (frames
    // are homogeneous in content in this implementation -- a state
    // transition always falls on a frame boundary -- so this mostly
    // just confirms the onset is at or near sample 0).
    size_t startIndex = rxSamples.size();
    for (size_t i = 0; i < rxSamples.size(); ++i) {
        if (std::fabs(rxSamples[i]) > kSyncAmplitudeThreshold) {
            startIndex = i;
            break;
        }
    }
    if (startIndex >= rxSamples.size()) {
        return {};
    }

    demod.emplace(markHz, spaceHz, kBaudRate, kSampleRateHz);
    const std::vector<Sample> tail(rxSamples.begin() + static_cast<long>(startIndex), rxSamples.end());
    return demod->ProcessSamples(tail);
}

} // namespace

std::optional<V8CallMenu> V8Handshake::ReceiveMenu(std::optional<FskDemodulator>& demod, double markHz,
                                                     double spaceHz, const std::vector<Sample>& rxSamples,
                                                     std::vector<bool>& bits, size_t& searchResumeIndex,
                                                     double competingHz, float competingThreshold) {
    const std::vector<bool> newBits =
        FeedSyncedDemodulator(demod, markHz, spaceHz, rxSamples, competingHz, competingThreshold);
    bits.insert(bits.end(), newBits.begin(), newBits.end());

    const auto found = FindBitPattern(bits, kCmJmPreamblePattern, searchResumeIndex);
    if (!found.has_value()) {
        searchResumeIndex =
            bits.size() >= kCmJmPreamblePattern.size() ? bits.size() - kCmJmPreamblePattern.size() + 1 : 0;
        return std::nullopt;
    }

    size_t afterPreamble = found.value() + kCmJmPreamblePattern.size();
    const auto menu = ReadCallMenuAfterPreamble(bits, afterPreamble);
    if (!menu.has_value()) {
        // ReadCallMenuAfterPreamble deliberately doesn't distinguish "not
        // enough data buffered yet" from "genuinely not a call-function
        // octet" (see its own header comment) -- so always retry from
        // this SAME preamble position, never skip past it. In this
        // implementation that's safe: silence never happens to produce a
        // string of bits that looks like a preamble (it demodulates to
        // all-false, and a preamble always starts with 10 mark/true
        // bits), so a real false positive essentially can't occur here.
        // A real line with actual noise could still defeat this, and
        // there's no timeout to recover if it ever does -- an already
        // documented limitation, not new to this fix.
        searchResumeIndex = found.value();
        return std::nullopt;
    }

    bits.erase(bits.begin(), bits.begin() + static_cast<long>(afterPreamble));
    searchResumeIndex = 0;
    return menu;
}

void V8Handshake::ProcessFrame(const std::vector<Sample>& rxSamples, std::vector<Sample>& txSamplesOut,
                                int frameSampleCount) {
    if (role_ == Role::CallDce) {
        ProcessCallDceFrame(rxSamples, txSamplesOut, frameSampleCount);
    } else {
        ProcessAnswerDceFrame(rxSamples, txSamplesOut, frameSampleCount);
    }
}

void V8Handshake::ProcessCallDceFrame(const std::vector<Sample>& rxSamples, std::vector<Sample>& txSamplesOut,
                                       int frameSampleCount) {
    switch (state_) {
        case State::WaitingForAnsam: {
            FillSilence(txSamplesOut, frameSampleCount);
            ansamDetector_.Reset();
            for (Sample s : rxSamples) {
                ansamDetector_.Process(s);
            }
            if (!rxSamples.empty() &&
                ansamDetector_.Magnitude(static_cast<int>(rxSamples.size())) > kAnsamDetectionThreshold) {
                logger_.Info("V8Handshake: ANSam detected, entering silent Te period");
                state_ = State::SilentTe;
                silenceSamplesRemaining_ = static_cast<int>(kTeSeconds * kSampleRateHz);
            }
            return;
        }
        case State::SilentTe: {
            FillSilence(txSamplesOut, frameSampleCount);
            silenceSamplesRemaining_ -= frameSampleCount;
            if (silenceSamplesRemaining_ <= 0) {
                logger_.Info("V8Handshake: Te elapsed, transmitting CM");
                state_ = State::TransmittingCmWaitingForJm;
            }
            return;
        }
        case State::TransmittingCmWaitingForJm: {
            FillFromRepeatingSequence(txSamplesOut, frameSampleCount, pendingCmTx_, cmModulator_, localMenu_);

            const auto menu = ReceiveMenu(jmDemodulator_, kChannel2MarkHz, kChannel2SpaceHz, rxSamples, jmBits_,
                                           jmSearchResumeIndex_, kAnsamCarrierHz, kSyncAmplitudeThreshold);
            if (menu.has_value()) {
                if (lastJmMenu_.has_value() && MenusEqual(lastJmMenu_.value(), menu.value())) {
                    ++identicalJmCount_;
                } else {
                    identicalJmCount_ = 1;
                    lastJmMenu_ = menu;
                }
                logger_.Info("V8Handshake: JM sequence received (" + std::to_string(identicalJmCount_) +
                             " identical in a row)");

                if (identicalJmCount_ >= 2) {
                    remoteMenu_ = lastJmMenu_;
                    pendingCjTx_.clear();
                    const std::vector<Sample> cj = BuildCjSequence(cmModulator_);
                    pendingCjTx_.insert(pendingCjTx_.end(), cj.begin(), cj.end());
                    state_ = State::SendingCj;
                    logger_.Info("V8Handshake: JM matched twice, sending CJ");
                }
            }
            return;
        }
        case State::SendingCj: {
            txSamplesOut.clear();
            int i = 0;
            for (; i < frameSampleCount && !pendingCjTx_.empty(); ++i) {
                txSamplesOut.push_back(pendingCjTx_.front());
                pendingCjTx_.pop_front();
            }
            for (; i < frameSampleCount; ++i) {
                txSamplesOut.push_back(static_cast<Sample>(0.0));
            }
            if (pendingCjTx_.empty()) {
                outcome_ = V8HandshakeOutcome::Negotiated;
                state_ = State::Negotiated;
                logger_.Info("V8Handshake: CJ sent, negotiation complete");
            }
            return;
        }
        case State::Negotiated:
        default:
            FillSilence(txSamplesOut, frameSampleCount);
            return;
    }
}

void V8Handshake::ProcessAnswerDceFrame(const std::vector<Sample>& rxSamples, std::vector<Sample>& txSamplesOut,
                                         int frameSampleCount) {
    switch (state_) {
        case State::InitialSilence: {
            FillSilence(txSamplesOut, frameSampleCount);
            silenceSamplesRemaining_ -= frameSampleCount;
            if (silenceSamplesRemaining_ <= 0) {
                logger_.Info("V8Handshake: initial silence elapsed, transmitting ANSam");
                state_ = State::TransmittingAnsamWaitingForCm;
                ansamGenerator_.Reset();
            }
            return;
        }
        case State::TransmittingAnsamWaitingForCm: {
            txSamplesOut.clear();
            txSamplesOut.reserve(static_cast<size_t>(frameSampleCount));
            for (int i = 0; i < frameSampleCount; ++i) {
                txSamplesOut.push_back(ansamGenerator_.NextSample());
            }

            const auto menu =
                ReceiveMenu(cmDemodulator_, kChannel1MarkHz, kChannel1SpaceHz, rxSamples, cmBits_, cmSearchResumeIndex_);
            if (menu.has_value()) {
                if (lastCmMenu_.has_value() && MenusEqual(lastCmMenu_.value(), menu.value())) {
                    ++identicalCmCount_;
                } else {
                    identicalCmCount_ = 1;
                    lastCmMenu_ = menu;
                }
                logger_.Info("V8Handshake: CM sequence received (" + std::to_string(identicalCmCount_) +
                             " identical in a row)");

                if (identicalCmCount_ >= 2) {
                    remoteMenu_ = lastCmMenu_;

                    // 8.2.3: JM should echo CM's call function if available locally;
                    // this implementation only ever offers Data, so that's always
                    // what goes out, whether or not it matches what CM asked for.
                    jmMenu_.callFunction = localMenu_.callFunction;
                    if (lastCmMenu_->modulationModes.has_value() && localMenu_.modulationModes.has_value()) {
                        jmMenu_.modulationModes = IntersectModulationModes(lastCmMenu_->modulationModes.value(),
                                                                            localMenu_.modulationModes.value());
                    }
                    if (lastCmMenu_->protocol == V8Protocol::Lapm && localMenu_.protocol == V8Protocol::Lapm) {
                        jmMenu_.protocol = V8Protocol::Lapm;
                    }

                    pendingJmTx_.clear();
                    cmBits_.clear(); // next thing on channel 1 is CJ, not another CM preamble
                    cmSearchResumeIndex_ = 0;
                    state_ = State::TransmittingJmWaitingForCj;
                    logger_.Info("V8Handshake: CM matched twice, transmitting JM");
                }
            }
            return;
        }
        case State::TransmittingJmWaitingForCj: {
            FillFromRepeatingSequence(txSamplesOut, frameSampleCount, pendingJmTx_, jmModulator_, jmMenu_);

            // cmDemodulator_ is guaranteed synced by now -- reaching this
            // state requires having already matched 2 CM sequences via it.
            const std::vector<bool> newBits = cmDemodulator_->ProcessSamples(rxSamples);
            cmBits_.insert(cmBits_.end(), newBits.begin(), newBits.end());

            // Incremental scan: each bit position is checked at most once
            // across the handshake's lifetime (cmSearchResumeIndex_ only
            // ever advances), the same bounded-growth discipline as
            // ReceiveMenu's preamble search -- checking only position 0
            // (an earlier version of this code did) would silently stop
            // matching forever the moment any stray bit shifted alignment.
            bool foundCj = false;
            while (cmBits_.size() >= cmSearchResumeIndex_ + 30) {
                if (LooksLikeCj(cmBits_, cmSearchResumeIndex_)) {
                    foundCj = true;
                    break;
                }
                ++cmSearchResumeIndex_;
            }

            if (foundCj) {
                outcome_ = V8HandshakeOutcome::Negotiated;
                state_ = State::Negotiated;
                logger_.Info("V8Handshake: CJ received, negotiation complete");
            } else if (cmSearchResumeIndex_ > 0) {
                cmBits_.erase(cmBits_.begin(), cmBits_.begin() + static_cast<long>(cmSearchResumeIndex_));
                cmSearchResumeIndex_ = 0;
            }
            return;
        }
        case State::Negotiated:
        default:
            FillSilence(txSamplesOut, frameSampleCount);
            return;
    }
}

} // namespace voipmodem::dsp
