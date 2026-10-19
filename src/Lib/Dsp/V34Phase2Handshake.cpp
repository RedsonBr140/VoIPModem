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
      toneAPresenceDetector_(kAnswerModemInfoCarrierHz, sampleRateHz),
      info0cReceiver_(kCallModemInfoCarrierHz, sampleRateHz),
      toneBPresenceDetector_(kCallModemInfoCarrierHz, sampleRateHz),
      toneBPresenceDetector2_(kCallModemInfoCarrierHz, sampleRateHz) {}

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
    // toneADetector_ is fed unconditionally, every frame, for as long
    // as it exists -- regardless of callPhase_. It's a long-lived
    // detector that reports THREE separate reversal events over the
    // full procedure (toneAEventsSeen_ 1/2/3); its own internal sample
    // clock (toneADetectorStartAbs_'s reference point) would desync
    // from real elapsed time if ProcessSamples() were ever skipped for
    // a phase or two, which is exactly the bug this avoids -- dispatch
    // on toneAEventsSeen_ below, never by only feeding it during
    // "relevant" phases.
    std::vector<long long> newToneAEvents;
    if (toneADetector_.has_value()) {
        newToneAEvents = toneADetector_->ProcessSamples(rxSamples);
    }

    switch (callPhase_) {
        case CallDcePhase::AwaitingInfo0a: {
            if (const std::optional<V34Info0> decoded = info0aReceiver_.ProcessSamples(rxSamples)) {
                remoteInfo0_ = decoded;
                logger_.Info("V34Phase2Handshake(CallDce): received INFO0a");
                toneADetector_.emplace(kAnswerModemInfoCarrierHz, sampleRateHz_);
                toneADetectorStartAbs_ = elapsedSamples_ + frameSampleCount;
                callPhase_ = CallDcePhase::AwaitingFirstToneAReversal;
            }
            break;
        }
        case CallDcePhase::AwaitingFirstToneAReversal:
        case CallDcePhase::AwaitingSecondToneAReversal: {
            for (long long relativeIndex : newToneAEvents) {
                const long long absoluteIndex = toneADetectorStartAbs_ + relativeIndex;
                ++toneAEventsSeen_;
                if (toneAEventsSeen_ == 1) {
                    // 11.2.1.1.3: schedule our own Tone B reversal
                    // exactly 40ms after this one.
                    ownReversalScheduled_ = true;
                    ownReversalScheduledAbs_ =
                        absoluteIndex + static_cast<long long>(kReversalEchoDelaySeconds * sampleRateHz_);
                    logger_.Info("V34Phase2Handshake(CallDce): detected first Tone A reversal");
                    callPhase_ = CallDcePhase::AwaitingSecondToneAReversal;
                } else if (toneAEventsSeen_ == 2 && ownReversalApplied_) {
                    // 11.2.1.1.4.
                    roundTripDelaySeconds_ =
                        ComputeCallModemRoundTripDelaySeconds(ownReversalAppliedAbs_, absoluteIndex, sampleRateHz_);
                    outcome_ = V34Phase2Outcome::RoundTripDelayMeasured;
                    logger_.Info("V34Phase2Handshake(CallDce): RTDEc computed");
                    callL1L2Tracker_.emplace(sampleRateHz_);
                    callPhase_ = CallDcePhase::ReceivingL1;
                }
            }
            break;
        }
        case CallDcePhase::ReceivingL1:
        case CallDcePhase::ReceivingL2: {
            L1L2PresenceTracker& tracker = *callL1L2Tracker_;
            if (!tracker.presenceDetected) {
                tracker.detector.Reset();
                for (Sample sample : rxSamples) {
                    tracker.detector.Process(sample);
                }
                const double magnitude = tracker.detector.Magnitude(static_cast<int>(rxSamples.size()));
                if (magnitude > kTonePresenceThreshold * nominalAmplitude_) {
                    tracker.presenceDetected = true;
                    tracker.detectionStartAbs = elapsedSamples_;
                    logger_.Info("V34Phase2Handshake(CallDce): L1 presence detected");
                }
            } else if (callPhase_ == CallDcePhase::ReceivingL1 &&
                       elapsedSamples_ + frameSampleCount - tracker.detectionStartAbs >=
                           static_cast<long long>(kL1DurationSeconds * sampleRateHz_)) {
                l1Received_ = true;
                tracker.l1Done = true;
                logger_.Info("V34Phase2Handshake(CallDce): L1's 160ms duration elapsed");
                callPhase_ = CallDcePhase::ReceivingL2;
            } else if (callPhase_ == CallDcePhase::ReceivingL2 &&
                       elapsedSamples_ + frameSampleCount - tracker.detectionStartAbs >=
                           static_cast<long long>((kL1DurationSeconds + kL2ReceiveCapSeconds) * sampleRateHz_)) {
                // 11.2.1.1.5: "may receive L2 for up to 500ms" -- then
                // transmit Tone B regardless (the cap, not a detected
                // end-of-L2 event -- this project doesn't attempt to
                // detect L2 ending, same as it never analyses L1/L2's
                // content for anything beyond presence).
                logger_.Info("V34Phase2Handshake(CallDce): L2 receive window elapsed; sending Tone B");
                callPhase_ = CallDcePhase::SendingToneBAwaitingThirdToneAReversal;
            }
            break;
        }
        case CallDcePhase::SendingToneBAwaitingThirdToneAReversal: {
            for (long long relativeIndex : newToneAEvents) {
                if (toneAEventsSeen_ != 2) {
                    continue; // already consumed what we needed; ignore stray events defensively
                }
                const long long absoluteIndex = toneADetectorStartAbs_ + relativeIndex;
                ++toneAEventsSeen_; // -> 3
                ownSecondReversalScheduled_ = true;
                ownSecondReversalScheduledAbs_ =
                    absoluteIndex + static_cast<long long>(kReversalEchoDelaySeconds * sampleRateHz_);
                logger_.Info("V34Phase2Handshake(CallDce): detected third Tone A reversal");
                callPhase_ = CallDcePhase::SendingOwnL1L2AwaitingToneAPresence;
            }
            break;
        }
        case CallDcePhase::SendingOwnL1L2AwaitingToneAPresence: {
            if (!callOwnL2TxStarted_) {
                break; // haven't started our own L2 yet; nothing to detect against
            }
            if (!toneAPresenceDetected_) {
                toneAPresenceDetector_.Reset();
                for (Sample sample : rxSamples) {
                    toneAPresenceDetector_.Process(sample);
                }
                const double magnitude = toneAPresenceDetector_.Magnitude(static_cast<int>(rxSamples.size()));
                if (magnitude > kTonePresenceThreshold * nominalAmplitude_) {
                    toneAPresenceDetected_ = true;
                    logger_.Info("V34Phase2Handshake(CallDce): detected answer's plain Tone A");
                }
            }
            const long long capAbs =
                callOwnL2TxStartAbs_ + static_cast<long long>(kL2LocalEchoCapSeconds * sampleRateHz_ +
                                                                roundTripDelaySeconds_ * sampleRateHz_);
            if (toneAPresenceDetected_ || elapsedSamples_ + frameSampleCount >= capAbs) {
                // 11.2.1.1.7: send INFO1c.
                callPhase_ = CallDcePhase::SendingInfo1cAwaitingInfo1a;
                logger_.Info("V34Phase2Handshake(CallDce): sending INFO1c");
            }
            break;
        }
        case CallDcePhase::SendingInfo1cAwaitingInfo1a: {
            if (!info1aReceiver_.has_value()) {
                break; // not yet armed (armed once INFO1c TX queue drains -- see TX side)
            }
            if (const std::optional<V34Info1a> decoded = info1aReceiver_->ProcessSamples(rxSamples)) {
                remoteInfo1a_ = decoded;
                outcome_ = V34Phase2Outcome::Phase2Complete;
                logger_.Info("V34Phase2Handshake(CallDce): received INFO1a; Phase 2 complete");
                callPhase_ = CallDcePhase::Phase2Complete;
            }
            break;
        }
        case CallDcePhase::Phase2Complete:
            break;
    }

    // TX: silence for the first 75ms, then everything else is driven
    // by callPhase_.
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
    switch (callPhase_) {
        case CallDcePhase::AwaitingInfo0a: {
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
            break;
        }
        case CallDcePhase::AwaitingFirstToneAReversal:
        case CallDcePhase::AwaitingSecondToneAReversal:
            GenerateCallDceToneB(out, sampleCount);
            break;
        case CallDcePhase::ReceivingL1:
        case CallDcePhase::ReceivingL2:
            FillSilence(out, sampleCount);
            break;
        case CallDcePhase::SendingToneBAwaitingThirdToneAReversal:
            toneBGenerator_->GenerateSamples(out, sampleCount); // plain Tone B, no reversal
            break;
        case CallDcePhase::SendingOwnL1L2AwaitingToneAPresence: {
            long long cursor = elapsedSamples_ + static_cast<long long>(out.size());
            const long long chunkEnd = cursor + sampleCount;
            const long long holdSamples = static_cast<long long>(kReversalHoldSeconds * sampleRateHz_);

            while (cursor < chunkEnd) {
                if (ownSecondReversalScheduled_ && !ownSecondReversalApplied_ &&
                    cursor >= ownSecondReversalScheduledAbs_) {
                    toneBGenerator_->ReversePhase();
                    ownSecondReversalApplied_ = true;
                    ownSecondReversalAppliedAbs_ = cursor;
                    logger_.Info("V34Phase2Handshake(CallDce): applied second Tone B reversal");
                }
                if (ownSecondReversalHoldDone_ && !callOwnL1TxStarted_) {
                    callOwnL1TxStarted_ = true;
                    callOwnL1TxEndAbs_ = cursor + static_cast<long long>(kL1DurationSeconds * sampleRateHz_);
                    callL1L2Generator_.emplace(nominalAmplitude_, sampleRateHz_);
                    logger_.Info("V34Phase2Handshake(CallDce): starting own L1 transmission");
                }
                if (callOwnL1TxStarted_ && !callOwnL2TxStarted_ && cursor >= callOwnL1TxEndAbs_) {
                    callOwnL2TxStarted_ = true;
                    callOwnL2TxStartAbs_ = cursor;
                    logger_.Info("V34Phase2Handshake(CallDce): starting own L2 transmission");
                }

                long long nextEventAbs = chunkEnd;
                if (ownSecondReversalScheduled_ && !ownSecondReversalApplied_) {
                    nextEventAbs = std::min(nextEventAbs, ownSecondReversalScheduledAbs_);
                }
                if (ownSecondReversalApplied_ && !ownSecondReversalHoldDone_) {
                    nextEventAbs = std::min(nextEventAbs, ownSecondReversalAppliedAbs_ + holdSamples);
                }
                if (callOwnL1TxStarted_ && !callOwnL2TxStarted_) {
                    nextEventAbs = std::min(nextEventAbs, callOwnL1TxEndAbs_);
                }

                const long long chunkLen = nextEventAbs - cursor;
                if (chunkLen <= 0) {
                    ownSecondReversalHoldDone_ = true; // exactly at the hold boundary
                    continue;
                }

                if (!ownSecondReversalHoldDone_) {
                    toneBGenerator_->GenerateSamples(out, static_cast<int>(chunkLen));
                } else if (callOwnL1TxStarted_ && !callOwnL2TxStarted_) {
                    callL1L2Generator_->GenerateL1Samples(out, static_cast<int>(chunkLen));
                } else if (callOwnL2TxStarted_) {
                    callL1L2Generator_->GenerateL2Samples(out, static_cast<int>(chunkLen));
                } else {
                    out.insert(out.end(), static_cast<size_t>(chunkLen), Sample(0));
                }
                cursor += chunkLen;

                if (ownSecondReversalApplied_ && !ownSecondReversalHoldDone_ &&
                    cursor >= ownSecondReversalAppliedAbs_ + holdSamples) {
                    ownSecondReversalHoldDone_ = true;
                }
            }
            break;
        }
        case CallDcePhase::SendingInfo1cAwaitingInfo1a: {
            if (!info1cTxQueued_) {
                std::vector<Sample> info1cSamples;
                EncodeInfo1cToSamples(MakeDefaultLocalInfo1c(), kCallModemInfoCarrierHz, info1cSamples,
                                       sampleRateHz_);
                pendingInfo1cTx_.insert(pendingInfo1cTx_.end(), info1cSamples.begin(), info1cSamples.end());
                info1cTxQueued_ = true;
            }
            int remaining = sampleCount;
            while (remaining > 0 && !pendingInfo1cTx_.empty()) {
                out.push_back(pendingInfo1cTx_.front());
                pendingInfo1cTx_.pop_front();
                --remaining;
            }
            if (remaining > 0) {
                if (!info1aReceiver_.has_value()) {
                    info1aReceiver_.emplace(kAnswerModemInfoCarrierHz, sampleRateHz_);
                }
                FillSilence(out, remaining);
            }
            break;
        }
        case CallDcePhase::Phase2Complete:
            FillSilence(out, sampleCount);
            break;
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
    // toneBDetector_ is fed unconditionally, every frame, for as long
    // as it exists -- same reasoning as toneADetector_'s own comment
    // in ProcessCallDceFrame: it reports TWO separate reversal events
    // over the full procedure (toneBEventsSeen_ 0/1), and skipping
    // ProcessSamples() for any phase would desync its internal sample
    // clock from toneBDetectorStartAbs_'s reference point.
    std::vector<long long> newToneBEvents;
    if (toneBDetector_.has_value()) {
        newToneBEvents = toneBDetector_->ProcessSamples(rxSamples);
    }

    switch (answerPhase_) {
        case AnswerDcePhase::AwaitingInfo0c: {
            if (const std::optional<V34Info0> decoded = info0cReceiver_.ProcessSamples(rxSamples)) {
                remoteInfo0_ = decoded;
                logger_.Info("V34Phase2Handshake(AnswerDce): received INFO0c");
                answerPhase_ = AnswerDcePhase::AwaitingToneBPresence;
            }
            break;
        }
        case AnswerDcePhase::AwaitingToneBPresence: {
            toneBPresenceDetector_.Reset();
            for (Sample sample : rxSamples) {
                toneBPresenceDetector_.Process(sample);
            }
            const double magnitude = toneBPresenceDetector_.Magnitude(static_cast<int>(rxSamples.size()));
            if (magnitude > kTonePresenceThreshold * nominalAmplitude_) {
                toneBPresenceDetected_ = true;
                logger_.Info("V34Phase2Handshake(AnswerDce): Tone B presence detected");
                answerPhase_ = AnswerDcePhase::AwaitingFirstToneBReversal;
            }
            break;
        }
        case AnswerDcePhase::AwaitingFirstToneBReversal: {
            for (long long relativeIndex : newToneBEvents) {
                if (toneBEventsSeen_ != 0) {
                    continue;
                }
                const long long absoluteIndex = toneBDetectorStartAbs_ + relativeIndex;
                ++toneBEventsSeen_; // -> 1
                roundTripDelaySeconds_ =
                    ComputeAnswerModemRoundTripDelaySeconds(firstReversalAppliedAbs_, absoluteIndex, sampleRateHz_);
                outcome_ = V34Phase2Outcome::RoundTripDelayMeasured;
                logger_.Info("V34Phase2Handshake(AnswerDce): RTDEa computed");

                secondReversalScheduled_ = true;
                secondReversalScheduledAbs_ =
                    absoluteIndex + static_cast<long long>(kReversalEchoDelaySeconds * sampleRateHz_);
                answerPhase_ = AnswerDcePhase::SendingOwnL1L2AwaitingToneBPresence2;
            }
            break;
        }
        case AnswerDcePhase::SendingOwnL1L2AwaitingToneBPresence2: {
            if (!l2TxStarted_) {
                break; // haven't started our own L2 yet; nothing to detect against
            }
            if (!toneBPresenceDetected2_) {
                toneBPresenceDetector2_.Reset();
                for (Sample sample : rxSamples) {
                    toneBPresenceDetector2_.Process(sample);
                }
                const double magnitude = toneBPresenceDetector2_.Magnitude(static_cast<int>(rxSamples.size()));
                if (magnitude > kTonePresenceThreshold * nominalAmplitude_) {
                    toneBPresenceDetected2_ = true;
                    logger_.Info("V34Phase2Handshake(AnswerDce): detected call's second Tone B presence");
                }
            }
            const long long capAbs =
                l2TxStartAbs_ + static_cast<long long>(kL2LocalEchoCapSeconds * sampleRateHz_ +
                                                         roundTripDelaySeconds_ * sampleRateHz_);
            if ((toneBPresenceDetected2_ || elapsedSamples_ + frameSampleCount >= capAbs) && !thirdReversalPending_ &&
                !thirdReversalApplied_) {
                thirdReversalPending_ = true;
                thirdReversalScheduledAbs_ =
                    elapsedSamples_ + frameSampleCount + static_cast<long long>(kToneAPreReversal3DelaySeconds *
                                                                                 sampleRateHz_);
                logger_.Info("V34Phase2Handshake(AnswerDce): scheduling third Tone A reversal");
                answerPhase_ = AnswerDcePhase::SendingThirdToneAAwaitingSecondToneBReversal;
            }
            break;
        }
        case AnswerDcePhase::SendingThirdToneAAwaitingSecondToneBReversal: {
            for (size_t i = 0; i < newToneBEvents.size(); ++i) {
                if (toneBEventsSeen_ != 1) {
                    continue;
                }
                ++toneBEventsSeen_; // -> 2
                logger_.Info("V34Phase2Handshake(AnswerDce): detected second Tone B reversal");
                answerL1L2Tracker_.emplace(sampleRateHz_);
                answerPhase_ = AnswerDcePhase::ReceivingL1;
            }
            break;
        }
        case AnswerDcePhase::ReceivingL1:
        case AnswerDcePhase::ReceivingL2: {
            L1L2PresenceTracker& tracker = *answerL1L2Tracker_;
            if (!tracker.presenceDetected) {
                tracker.detector.Reset();
                for (Sample sample : rxSamples) {
                    tracker.detector.Process(sample);
                }
                const double magnitude = tracker.detector.Magnitude(static_cast<int>(rxSamples.size()));
                if (magnitude > kTonePresenceThreshold * nominalAmplitude_) {
                    tracker.presenceDetected = true;
                    tracker.detectionStartAbs = elapsedSamples_;
                    logger_.Info("V34Phase2Handshake(AnswerDce): L1 presence detected");
                }
            } else if (answerPhase_ == AnswerDcePhase::ReceivingL1 &&
                       elapsedSamples_ + frameSampleCount - tracker.detectionStartAbs >=
                           static_cast<long long>(kL1DurationSeconds * sampleRateHz_)) {
                tracker.l1Done = true;
                logger_.Info("V34Phase2Handshake(AnswerDce): L1's 160ms duration elapsed");
                answerPhase_ = AnswerDcePhase::ReceivingL2;
            } else if (answerPhase_ == AnswerDcePhase::ReceivingL2 &&
                       elapsedSamples_ + frameSampleCount - tracker.detectionStartAbs >=
                           static_cast<long long>((kL1DurationSeconds + kL2ReceiveCapSeconds) * sampleRateHz_)) {
                logger_.Info("V34Phase2Handshake(AnswerDce): L2 receive window elapsed; sending Tone A");
                answerPhase_ = AnswerDcePhase::SendingToneAAwaitingInfo1c;
            }
            break;
        }
        case AnswerDcePhase::SendingToneAAwaitingInfo1c: {
            if (!info1cReceiver_.has_value()) {
                break; // armed on TX side once Tone A actually starts
            }
            if (const std::optional<V34Info1c> decoded = info1cReceiver_->ProcessSamples(rxSamples)) {
                remoteInfo1c_ = decoded;
                outcome_ = V34Phase2Outcome::Phase2Complete;
                logger_.Info("V34Phase2Handshake(AnswerDce): received INFO1c; sending INFO1a; Phase 2 complete");
                answerPhase_ = AnswerDcePhase::Phase2Complete;
            }
            break;
        }
        case AnswerDcePhase::Phase2Complete:
            break;
    }

    // TX: silence for the first 75ms, then everything else is driven
    // by answerPhase_.
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
    // INFO0a's TX queue is drained unconditionally, first, regardless
    // of answerPhase_ -- RX can advance answerPhase_ (e.g. once INFO0c
    // is decoded) well before this queue has finished draining on the
    // TX side, since the two sides' own transmissions aren't the same
    // length. Phase-gating this queue's draining was the actual bug an
    // earlier version of this function had: a later phase's case would
    // call into toneAGenerator_ before this ever got the chance to
    // construct it. Same pattern applied to INFO1a's queue below.
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
    if (remaining == 0) {
        return;
    }

    // Tone A exists continuously from here on (INFO0a done); ensure
    // it's constructed and its start time recorded the first time we
    // actually emit from it, regardless of which phase that happens to
    // be in.
    if (!toneAGenerator_.has_value()) {
        toneAGenerator_.emplace(nominalAmplitude_, sampleRateHz_);
        toneATxStartedAbs_ = elapsedSamples_ + static_cast<long long>(out.size());
    }

    if (answerPhase_ == AnswerDcePhase::Phase2Complete) {
        if (!info1aTxQueued_) {
            std::vector<Sample> info1aSamples;
            EncodeInfo1aToSamples(MakeDefaultLocalInfo1a(), kAnswerModemInfoCarrierHz, info1aSamples, sampleRateHz_);
            pendingInfo1aTx_.insert(pendingInfo1aTx_.end(), info1aSamples.begin(), info1aSamples.end());
            info1aTxQueued_ = true;
        }
        while (remaining > 0 && !pendingInfo1aTx_.empty()) {
            out.push_back(pendingInfo1aTx_.front());
            pendingInfo1aTx_.pop_front();
            --remaining;
        }
        if (remaining > 0) {
            FillSilence(out, remaining);
        }
        return;
    }

    switch (answerPhase_) {
        case AnswerDcePhase::AwaitingInfo0c:
        case AnswerDcePhase::AwaitingToneBPresence: {
            // 11.2.1.2.3: the condition for the first reversal (Tone B
            // detected AND >=50ms of our own Tone A sent) isn't a fixed
            // schedule -- check it here, right before generating, so it
            // fires at the earliest sample where both are true. Harmless
            // to check during AwaitingInfo0c too: toneBPresenceDetected_
            // can only be true once that phase has already been reached.
            const long long cursor = elapsedSamples_ + static_cast<long long>(out.size());
            const long long minToneADurationSamples =
                static_cast<long long>(kAnswerMinToneADurationSeconds * sampleRateHz_);
            if (toneBPresenceDetected_ && !firstReversalScheduled_ &&
                cursor >= toneATxStartedAbs_ + minToneADurationSamples) {
                firstReversalScheduled_ = true;
                firstReversalScheduledAbs_ = cursor;
            }
            toneAGenerator_->GenerateSamples(out, remaining); // plain, no reversal yet
            break;
        }
        case AnswerDcePhase::AwaitingFirstToneBReversal:
        case AnswerDcePhase::SendingOwnL1L2AwaitingToneBPresence2:
        case AnswerDcePhase::SendingThirdToneAAwaitingSecondToneBReversal:
            GenerateAnswerDceToneA(out, remaining);
            break;
        case AnswerDcePhase::ReceivingL1:
        case AnswerDcePhase::ReceivingL2:
            FillSilence(out, remaining);
            break;
        case AnswerDcePhase::SendingToneAAwaitingInfo1c: {
            if (!info1cReceiver_.has_value()) {
                info1cReceiver_.emplace(kCallModemInfoCarrierHz, sampleRateHz_);
            }
            toneAGenerator_->GenerateSamples(out, remaining); // plain Tone A, no reversal
            break;
        }
        case AnswerDcePhase::Phase2Complete:
            break; // handled above
    }
}

void V34Phase2Handshake::GenerateAnswerDceToneA(std::vector<Sample>& out, int sampleCount) {
    long long cursor = elapsedSamples_ + static_cast<long long>(out.size());
    const long long chunkEnd = cursor + sampleCount;
    const long long holdSamples = static_cast<long long>(kReversalHoldSeconds * sampleRateHz_);

    while (cursor < chunkEnd) {
        // 11.2.1.2.3's scheduling condition (Tone B detected AND
        // >=50ms of our own Tone A sent) is checked here, not just in
        // GenerateAnswerDceTxChunk's AwaitingToneBPresence case -- RX
        // can (and typically does) advance answerPhase_ to
        // AwaitingFirstToneBReversal the instant presence is detected,
        // which may be well before the 50ms condition is also
        // satisfied; this function is what TX dispatches to from that
        // phase onward, so the scheduling check has to live here too
        // or it never gets a second chance to fire.
        if (toneBPresenceDetected_ && !firstReversalScheduled_ &&
            cursor >= toneATxStartedAbs_ + static_cast<long long>(kAnswerMinToneADurationSeconds * sampleRateHz_)) {
            firstReversalScheduled_ = true;
            firstReversalScheduledAbs_ = cursor;
        }
        if (firstReversalScheduled_ && !firstReversalApplied_ && cursor >= firstReversalScheduledAbs_) {
            toneAGenerator_->ReversePhase();
            firstReversalApplied_ = true;
            firstReversalAppliedAbs_ = cursor;
            toneBDetector_.emplace(kCallModemInfoCarrierHz, sampleRateHz_);
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
            l1TxStarted_ = true;
            l1TxEndAbs_ = cursor + static_cast<long long>(kL1DurationSeconds * sampleRateHz_);
            l1Generator_.emplace(nominalAmplitude_, sampleRateHz_);
            logger_.Info("V34Phase2Handshake(AnswerDce): starting L1 transmission");
        }
        if (l1TxStarted_ && !l2TxStarted_ && cursor >= l1TxEndAbs_) {
            l2TxStarted_ = true;
            l2TxStartAbs_ = cursor;
            logger_.Info("V34Phase2Handshake(AnswerDce): starting L2 transmission");
        }
        if (thirdReversalPending_ && !thirdReversalApplied_ && cursor >= thirdReversalScheduledAbs_) {
            toneAGenerator_->ReversePhase();
            thirdReversalApplied_ = true;
            thirdReversalAppliedAbs_ = cursor;
            logger_.Info("V34Phase2Handshake(AnswerDce): applied third Tone A reversal");
        }

        long long nextEventAbs = chunkEnd;
        if (toneBPresenceDetected_ && !firstReversalScheduled_) {
            nextEventAbs = std::min(nextEventAbs, toneATxStartedAbs_ +
                                                        static_cast<long long>(kAnswerMinToneADurationSeconds *
                                                                                sampleRateHz_));
        }
        if (firstReversalScheduled_ && !firstReversalApplied_) {
            nextEventAbs = std::min(nextEventAbs, firstReversalScheduledAbs_);
        }
        if (secondReversalScheduled_ && !secondReversalApplied_) {
            nextEventAbs = std::min(nextEventAbs, secondReversalScheduledAbs_);
        }
        if (secondReversalApplied_ && !secondReversalHoldDone_) {
            nextEventAbs = std::min(nextEventAbs, secondReversalAppliedAbs_ + holdSamples);
        }
        if (l1TxStarted_ && !l2TxStarted_) {
            nextEventAbs = std::min(nextEventAbs, l1TxEndAbs_);
        }
        if (thirdReversalPending_ && !thirdReversalApplied_) {
            nextEventAbs = std::min(nextEventAbs, thirdReversalScheduledAbs_);
        }
        if (thirdReversalApplied_ && !thirdReversalHoldDone_) {
            nextEventAbs = std::min(nextEventAbs, thirdReversalAppliedAbs_ + holdSamples);
        }

        const long long chunkLen = nextEventAbs - cursor;
        if (chunkLen <= 0) {
            if (!secondReversalHoldDone_) {
                secondReversalHoldDone_ = true;
            } else {
                thirdReversalHoldDone_ = true;
            }
            continue;
        }

        if (!secondReversalHoldDone_) {
            toneAGenerator_->GenerateSamples(out, static_cast<int>(chunkLen));
        } else if (l1TxStarted_ && !l2TxStarted_) {
            l1Generator_->GenerateL1Samples(out, static_cast<int>(chunkLen));
        } else if (l2TxStarted_ && !thirdReversalHoldDone_) {
            if (!thirdReversalApplied_) {
                l1Generator_->GenerateL2Samples(out, static_cast<int>(chunkLen));
            } else {
                toneAGenerator_->GenerateSamples(out, static_cast<int>(chunkLen));
            }
        } else {
            out.insert(out.end(), static_cast<size_t>(chunkLen), Sample(0));
        }
        cursor += chunkLen;

        if (secondReversalApplied_ && !secondReversalHoldDone_ && cursor >= secondReversalAppliedAbs_ + holdSamples) {
            secondReversalHoldDone_ = true;
        }
        if (thirdReversalApplied_ && !thirdReversalHoldDone_ && cursor >= thirdReversalAppliedAbs_ + holdSamples) {
            thirdReversalHoldDone_ = true;
        }
    }
}

} // namespace voipmodem::dsp
