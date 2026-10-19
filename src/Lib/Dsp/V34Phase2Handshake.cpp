#include "V34Phase2Handshake.h"

#include <algorithm>

#include "V34RoundTripDelay.h"

namespace voipmodem::dsp {

V34Phase2Handshake::V34Phase2Handshake(Role role, Sample nominalAmplitude, TaggedLogger& logger, int sampleRateHz)
    : role_(role),
      nominalAmplitude_(nominalAmplitude),
      sampleRateHz_(sampleRateHz),
      logger_(logger),
      silenceSamplesRemaining_(static_cast<long long>(kSilentPeriodSeconds * sampleRateHz)),
      info0aReceiver_(kAnswerModemInfoCarrierHz, sampleRateHz),
      l1PresenceDetector_(kL1PresenceBeaconHz, sampleRateHz),
      info0cReceiver_(kCallModemInfoCarrierHz, sampleRateHz),
      toneBPresenceDetector_(kCallModemInfoCarrierHz, sampleRateHz) {}

void V34Phase2Handshake::ProcessFrame(const std::vector<Sample>& rxSamples, std::vector<Sample>& txSamplesOut,
                                       int frameSampleCount) {
    txSamplesOut.clear();
    if (role_ == Role::CallDce) {
        ProcessCallDceFrame(rxSamples, txSamplesOut, frameSampleCount);
    } else {
        ProcessAnswerDceFrame(rxSamples, txSamplesOut, frameSampleCount);
    }
    elapsedSamples_ += frameSampleCount;
}

void V34Phase2Handshake::FillSilence(std::vector<Sample>& out, int frameSampleCount) {
    out.insert(out.end(), static_cast<size_t>(frameSampleCount), Sample(0));
}

// --- Call DCE -------------------------------------------------------

void V34Phase2Handshake::ProcessCallDceFrame(const std::vector<Sample>& rxSamples, std::vector<Sample>& txSamplesOut,
                                              int frameSampleCount) {
    // RX: 11.2.1.1.1/.2 -- receive INFO0a, then watch for Tone A's
    // reversals. The detector is constructed the moment INFO0a is
    // decoded but, deliberately, isn't fed any of THIS frame's
    // samples (see class comment's "known timing imprecision") --
    // it only starts seeing audio from the next frame onward.
    if (!info0aReceived_) {
        if (const std::optional<V34Info0> decoded = info0aReceiver_.ProcessSamples(rxSamples)) {
            info0aReceived_ = true;
            remoteInfo0_ = decoded;
            logger_.Info("V34Phase2Handshake(CallDce): received INFO0a");
            toneADetector_.emplace(kAnswerModemInfoCarrierHz, sampleRateHz_);
            toneADetectorStartAbs_ = elapsedSamples_ + frameSampleCount;
        }
    } else if (toneAEventsSeen_ < 2 && toneADetector_.has_value()) {
        const std::vector<long long> events = toneADetector_->ProcessSamples(rxSamples);
        for (long long relativeIndex : events) {
            const long long absoluteIndex = toneADetectorStartAbs_ + relativeIndex;
            ++toneAEventsSeen_;
            if (toneAEventsSeen_ == 1) {
                // 11.2.1.1.3: schedule our own Tone B reversal exactly
                // 40ms after this one.
                ownReversalScheduled_ = true;
                ownReversalScheduledAbs_ =
                    absoluteIndex + static_cast<long long>(kReversalEchoDelaySeconds * sampleRateHz_);
                logger_.Info("V34Phase2Handshake(CallDce): detected first Tone A reversal");
            } else if (toneAEventsSeen_ == 2 && ownReversalApplied_ &&
                       outcome_ == V34Phase2Outcome::InProgress) {
                // 11.2.1.1.4.
                roundTripDelaySeconds_ =
                    ComputeCallModemRoundTripDelaySeconds(ownReversalAppliedAbs_, absoluteIndex, sampleRateHz_);
                outcome_ = V34Phase2Outcome::RoundTripDelayMeasured;
                logger_.Info("V34Phase2Handshake(CallDce): RTDEc computed");
            }
        }
    } else if (outcome_ == V34Phase2Outcome::RoundTripDelayMeasured && !l1Received_) {
        // 11.2.1.1.4/.5: receiver reconditioned to receive L1 (and,
        // not yet implemented, L2 -- see class comment). L1PresenceDetector_
        // only tells us L1-shaped energy has started; it isn't a
        // channel-response measurement of any kind.
        if (!l1PresenceDetected_) {
            l1PresenceDetector_.Reset();
            for (Sample sample : rxSamples) {
                l1PresenceDetector_.Process(sample);
            }
            const double magnitude = l1PresenceDetector_.Magnitude(static_cast<int>(rxSamples.size()));
            if (magnitude > kTonePresenceThreshold * nominalAmplitude_) {
                l1PresenceDetected_ = true;
                l1DetectionStartAbs_ = elapsedSamples_;
                logger_.Info("V34Phase2Handshake(CallDce): L1 presence detected");
            }
        } else if (elapsedSamples_ + frameSampleCount - l1DetectionStartAbs_ >=
                   static_cast<long long>(kL1DurationSeconds * sampleRateHz_)) {
            l1Received_ = true;
            logger_.Info("V34Phase2Handshake(CallDce): L1's 160ms duration elapsed");
        }
    }

    // TX: silence for the first 75ms, then INFO0c followed by Tone B.
    if (silenceSamplesRemaining_ > 0) {
        const int silenceNow = static_cast<int>(std::min<long long>(silenceSamplesRemaining_, frameSampleCount));
        FillSilence(txSamplesOut, silenceNow);
        silenceSamplesRemaining_ -= silenceNow;
        const int remainder = frameSampleCount - silenceNow;
        if (remainder > 0) {
            GenerateCallDceTxChunk(txSamplesOut, remainder);
        }
    } else {
        GenerateCallDceTxChunk(txSamplesOut, frameSampleCount);
    }
}

void V34Phase2Handshake::GenerateCallDceTxChunk(std::vector<Sample>& out, int sampleCount) {
    if (!info0cTxQueued_) {
        std::vector<Sample> info0cSamples;
        EncodeInfo0ToSamples(MakeDefaultLocalInfo0(), kCallModemInfoCarrierHz, info0cSamples, sampleRateHz_);
        pendingInfo0cTx_.insert(pendingInfo0cTx_.end(), info0cSamples.begin(), info0cSamples.end());
        info0cTxQueued_ = true;
    }

    int remaining = sampleCount;
    while (remaining > 0 && !pendingInfo0cTx_.empty()) {
        out.push_back(pendingInfo0cTx_.front());
        pendingInfo0cTx_.pop_front();
        --remaining;
    }
    if (remaining > 0) {
        GenerateCallDceToneB(out, remaining);
    }
}

void V34Phase2Handshake::GenerateCallDceToneB(std::vector<Sample>& out, int sampleCount) {
    if (!toneBGenerator_.has_value()) {
        toneBGenerator_.emplace(nominalAmplitude_, sampleRateHz_);
    }

    long long cursor = elapsedSamples_ + static_cast<long long>(out.size());
    const long long chunkEnd = cursor + sampleCount;
    const long long holdSamples = static_cast<long long>(kReversalHoldSeconds * sampleRateHz_);

    while (cursor < chunkEnd) {
        if (ownReversalScheduled_ && !ownReversalApplied_ && cursor >= ownReversalScheduledAbs_) {
            toneBGenerator_->ReversePhase();
            ownReversalApplied_ = true;
            ownReversalAppliedAbs_ = cursor;
        }

        long long nextEventAbs = chunkEnd;
        if (ownReversalScheduled_ && !ownReversalApplied_) {
            nextEventAbs = std::min(nextEventAbs, ownReversalScheduledAbs_);
        }
        if (ownReversalApplied_ && !postReversalHoldDone_) {
            nextEventAbs = std::min(nextEventAbs, ownReversalAppliedAbs_ + holdSamples);
        }

        const long long chunkLen = nextEventAbs - cursor;
        if (chunkLen <= 0) {
            postReversalHoldDone_ = true; // exactly at the hold boundary
            continue;
        }

        if (postReversalHoldDone_) {
            out.insert(out.end(), static_cast<size_t>(chunkLen), Sample(0));
        } else {
            toneBGenerator_->GenerateSamples(out, static_cast<int>(chunkLen));
        }
        cursor += chunkLen;

        if (ownReversalApplied_ && !postReversalHoldDone_ && cursor >= ownReversalAppliedAbs_ + holdSamples) {
            postReversalHoldDone_ = true;
        }
    }
}

// --- Answer DCE -------------------------------------------------------

void V34Phase2Handshake::ProcessAnswerDceFrame(const std::vector<Sample>& rxSamples,
                                                std::vector<Sample>& txSamplesOut, int frameSampleCount) {
    // RX: 11.2.1.2.1/.2 -- receive INFO0c, then watch for Tone B's
    // presence, then (after our own reversal) its reversal.
    if (!info0cReceived_) {
        if (const std::optional<V34Info0> decoded = info0cReceiver_.ProcessSamples(rxSamples)) {
            info0cReceived_ = true;
            remoteInfo0_ = decoded;
            logger_.Info("V34Phase2Handshake(AnswerDce): received INFO0c");
        }
    } else if (!toneBPresenceDetected_) {
        toneBPresenceDetector_.Reset();
        for (Sample sample : rxSamples) {
            toneBPresenceDetector_.Process(sample);
        }
        const double magnitude = toneBPresenceDetector_.Magnitude(static_cast<int>(rxSamples.size()));
        if (magnitude > kTonePresenceThreshold * nominalAmplitude_) {
            toneBPresenceDetected_ = true;
            logger_.Info("V34Phase2Handshake(AnswerDce): Tone B presence detected");
        }
    } else if (toneBDetector_.has_value()) {
        const std::vector<long long> events = toneBDetector_->ProcessSamples(rxSamples);
        for (long long relativeIndex : events) {
            const long long absoluteIndex = toneBDetectorStartAbs_ + relativeIndex;
            if (!firstReversalApplied_) {
                // Shouldn't happen (we only construct toneBDetector_
                // once our own first reversal is applied), but guard
                // against acting on a stray event if it ever does.
                continue;
            }
            if (!secondReversalScheduled_ && outcome_ == V34Phase2Outcome::InProgress) {
                // 11.2.1.2.4.
                roundTripDelaySeconds_ =
                    ComputeAnswerModemRoundTripDelaySeconds(firstReversalAppliedAbs_, absoluteIndex, sampleRateHz_);
                outcome_ = V34Phase2Outcome::RoundTripDelayMeasured;
                logger_.Info("V34Phase2Handshake(AnswerDce): RTDEa computed");

                // 11.2.1.2.5: schedule the second Tone A reversal 40ms
                // after this one -- needed so the call DCE's own
                // .1.4 has a second reversal to detect, even though
                // our own RTDEa is already known.
                secondReversalScheduled_ = true;
                secondReversalScheduledAbs_ =
                    absoluteIndex + static_cast<long long>(kReversalEchoDelaySeconds * sampleRateHz_);
            }
        }
    }

    // TX: silence for the first 75ms, then INFO0a followed by Tone A.
    if (silenceSamplesRemaining_ > 0) {
        const int silenceNow = static_cast<int>(std::min<long long>(silenceSamplesRemaining_, frameSampleCount));
        FillSilence(txSamplesOut, silenceNow);
        silenceSamplesRemaining_ -= silenceNow;
        const int remainder = frameSampleCount - silenceNow;
        if (remainder > 0) {
            GenerateAnswerDceTxChunk(txSamplesOut, remainder);
        }
    } else {
        GenerateAnswerDceTxChunk(txSamplesOut, frameSampleCount);
    }
}

void V34Phase2Handshake::GenerateAnswerDceTxChunk(std::vector<Sample>& out, int sampleCount) {
    if (!info0aTxQueued_) {
        std::vector<Sample> info0aSamples;
        EncodeInfo0ToSamples(MakeDefaultLocalInfo0(), kAnswerModemInfoCarrierHz, info0aSamples, sampleRateHz_);
        pendingInfo0aTx_.insert(pendingInfo0aTx_.end(), info0aSamples.begin(), info0aSamples.end());
        info0aTxQueued_ = true;
    }

    int remaining = sampleCount;
    while (remaining > 0 && !pendingInfo0aTx_.empty()) {
        out.push_back(pendingInfo0aTx_.front());
        pendingInfo0aTx_.pop_front();
        --remaining;
    }
    if (remaining > 0) {
        if (!toneAGenerator_.has_value()) {
            toneAGenerator_.emplace(nominalAmplitude_, sampleRateHz_);
            toneATxStartedAbs_ = elapsedSamples_ + static_cast<long long>(out.size());
        }
        GenerateAnswerDceToneA(out, remaining);
    }
}

void V34Phase2Handshake::GenerateAnswerDceToneA(std::vector<Sample>& out, int sampleCount) {
    long long cursor = elapsedSamples_ + static_cast<long long>(out.size());
    const long long chunkEnd = cursor + sampleCount;
    const long long holdSamples = static_cast<long long>(kReversalHoldSeconds * sampleRateHz_);
    const long long minToneADurationSamples =
        static_cast<long long>(kAnswerMinToneADurationSeconds * sampleRateHz_);

    // 11.2.1.2.3: the condition for the FIRST reversal (Tone B
    // detected AND >=50ms of our own Tone A sent) isn't a fixed
    // schedule the way the second reversal's 40ms delay is -- check it
    // here, right before generating, so it fires at the earliest
    // sample where both are true.
    if (!firstReversalScheduled_ && toneBPresenceDetected_ && cursor >= toneATxStartedAbs_ + minToneADurationSamples) {
        firstReversalScheduled_ = true;
        firstReversalScheduledAbs_ = cursor;
    }

    while (cursor < chunkEnd) {
        if (firstReversalScheduled_ && !firstReversalApplied_ && cursor >= firstReversalScheduledAbs_) {
            toneAGenerator_->ReversePhase();
            firstReversalApplied_ = true;
            firstReversalAppliedAbs_ = cursor;
            // 11.2.1.2.3: "condition its receiver to detect a Tone B
            // phase reversal" -- arm the reversal detector now, same
            // next-frame-onward timing simplification as the call
            // DCE's toneADetector_.
            toneBDetector_.emplace(kCallModemInfoCarrierHz, sampleRateHz_);
            // Deliberately starts seeing audio only from the NEXT
            // frame onward (chunkEnd == this frame's absolute end),
            // same "no same-frame splitting between RX decoders"
            // simplification as the call DCE's toneADetector_.
            toneBDetectorStartAbs_ = chunkEnd;
            logger_.Info("V34Phase2Handshake(AnswerDce): applied first Tone A reversal");
        }
        if (secondReversalScheduled_ && !secondReversalApplied_ && cursor >= secondReversalScheduledAbs_) {
            toneAGenerator_->ReversePhase();
            secondReversalApplied_ = true;
            secondReversalAppliedAbs_ = cursor;
            logger_.Info("V34Phase2Handshake(AnswerDce): applied second Tone A reversal");
        }
        if (secondReversalHoldDone_ && !l1TxStarted_) {
            // 11.2.1.2.5: hold just finished -- start L1 for its fixed
            // 160ms (L2 deliberately not implemented, see class
            // comment).
            l1TxStarted_ = true;
            l1TxEndAbs_ = cursor + static_cast<long long>(kL1DurationSeconds * sampleRateHz_);
            l1Generator_.emplace(nominalAmplitude_, sampleRateHz_);
            logger_.Info("V34Phase2Handshake(AnswerDce): starting L1 transmission");
        }

        long long nextEventAbs = chunkEnd;
        if (firstReversalScheduled_ && !firstReversalApplied_) {
            nextEventAbs = std::min(nextEventAbs, firstReversalScheduledAbs_);
        }
        if (secondReversalScheduled_ && !secondReversalApplied_) {
            nextEventAbs = std::min(nextEventAbs, secondReversalScheduledAbs_);
        }
        if (secondReversalApplied_ && !secondReversalHoldDone_) {
            nextEventAbs = std::min(nextEventAbs, secondReversalAppliedAbs_ + holdSamples);
        }
        if (l1TxStarted_ && cursor < l1TxEndAbs_) {
            nextEventAbs = std::min(nextEventAbs, l1TxEndAbs_);
        }

        const long long chunkLen = nextEventAbs - cursor;
        if (chunkLen <= 0) {
            secondReversalHoldDone_ = true; // exactly at the hold boundary
            continue;
        }

        if (!secondReversalHoldDone_) {
            toneAGenerator_->GenerateSamples(out, static_cast<int>(chunkLen));
        } else if (l1TxStarted_ && cursor < l1TxEndAbs_) {
            l1Generator_->GenerateL1Samples(out, static_cast<int>(chunkLen));
        } else {
            out.insert(out.end(), static_cast<size_t>(chunkLen), Sample(0));
        }
        cursor += chunkLen;

        if (secondReversalApplied_ && !secondReversalHoldDone_ && cursor >= secondReversalAppliedAbs_ + holdSamples) {
            secondReversalHoldDone_ = true;
        }
    }
}

} // namespace voipmodem::dsp
