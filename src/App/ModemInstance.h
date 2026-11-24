#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "CommandLineSession.h"
#include "../Lib/Audio/WindowsAudioOutput.h"
#include "../Lib/Config/AppConfig.h"
#include "../Lib/Core/ICallController.h"
#include "../Lib/Core/ModemStateMachine.h"
#include "../Lib/Logging/Logger.h"
#include "../Lib/Logging/TaggedLogger.h"
#include "../Lib/Serial/WindowsSerialPort.h"
#include "../Lib/Sip/PjsipEndpoint.h"

namespace voipmodem {

// One independently-running modem line: its own COM port, call
// controller, and state machine, driven by its own thread so that
// multiple instances can run concurrently without one instance's serial
// read blocking another's. See docs/ARCHITECTURE.md "Multiple modem
// instances". Each instance logs through the same shared Logger (which
// is already mutex-protected) tagged with its own name via TaggedLogger,
// so output from several instances stays distinguishable in one log.
//
// Gets a real, PJSIP-backed ICallController (PjsipCallController) when
// `sipEndpoint` is non-null and the instance's config has a SipServer
// set; otherwise falls back to NullCallController, same as Phase 1. See
// docs/ARCHITECTURE.md "SIP integration (Phase 2)".
class ModemInstance {
public:
    ModemInstance(ModemInstanceConfig config, Logger& sharedLogger, PjsipEndpoint* sipEndpoint);

    // Opens the serial port. Returns false (and logs why) on failure;
    // the caller decides whether that's fatal for the whole process or
    // just means this one instance is skipped.
    bool Open();

    // Starts the read loop on a background thread. Call only after a
    // successful Open().
    void Start();

    // Signals the read loop to stop, joins the thread, and closes the
    // port. Safe to call even if Start() was never called.
    void Stop();

    const std::string& Name() const { return config_.name; }

private:
    void RunLoop();
    void PollCallEvents();
    void PumpIncomingCallRinging();
    void PumpDialAudioTrigger();

    // Online-data-mode byte passthrough (see docs/ROADMAP.md Phase 4's
    // V.21 data-mode milestone): drains whatever the active call's
    // modem link has actually decoded since the last iteration and
    // writes it straight to the serial port.
    void PumpOnlineDataRx();

    // Routes one serial byte received while ModemState::OnlineData is
    // active: either forwards it to the call as data, or holds it back
    // because it might be the start of a real "+++" escape sequence (see
    // the class comment below and docs/ROADMAP.md Phase 4). Replaces a
    // plain unconditional SendData() call once the escape sequence was
    // added -- see EscapeDetectState's own comment for why this needs to
    // be a small state machine rather than a single byte check.
    void HandleOnlineDataByte(std::uint8_t byte);

    // Sends `count` held-back '+' bytes to the call as ordinary data --
    // called whenever a would-be escape attempt turns out not to be one
    // (wrong timing), since those bytes were genuinely meant for the far
    // end and were only ever delayed, never consumed.
    void FlushHeldPluses(int count);

    // Checks whether an in-progress escape attempt has now been silent
    // for the full guard time after its third '+', the other half of
    // the Hayes "+++" timing rule HandleOnlineDataByte() can't check by
    // itself (it only runs when a byte actually arrives). Called every
    // RunLoop iteration, same pattern as the other Pump* methods.
    void PumpEscapeSequenceTimeout();

    // Plays a dial tone followed by each digit of `number` as a DTMF
    // tone, purely a local cosmetic cue for a human listening via
    // `dialAudio_` -- there's no physical line to produce a real dial
    // tone/dialing sidetone from, and nothing here is sent anywhere
    // (the actual dial already happened via ICallController::Dial by
    // the time this runs). Runs on `dialAudioThread_`, polling
    // `stopRequested_` between each tone so Stop() isn't blocked for
    // the sequence's full ~1-3 second duration. See docs/ROADMAP.md
    // Phase 4.
    void PlayDialSequence(std::string number);

    ModemInstanceConfig config_;
    TaggedLogger logger_;
    PjsipEndpoint* sipEndpoint_; // nullable; non-owning
    WindowsSerialPort serialPort_;
    std::unique_ptr<ICallController> callController_;
    ModemStateMachine core_;
    CommandLineSession session_;
    std::atomic<bool> stopRequested_{false};
    std::thread thread_;

    // RING cadence + S0 auto-answer bookkeeping for the current incoming
    // call, if any. Reset whenever ModemStateMachine::HasIncomingCall()
    // goes back to false (answered, or the caller gave up).
    std::chrono::steady_clock::time_point lastRingAt_{};
    int ringCount_ = 0;
    static constexpr std::chrono::milliseconds kRingInterval{3000};

    // Local dial-tone/DTMF cue (see PlayDialSequence). Only constructed
    // if config_.enableLocalAudio -- same per-line opt-in flag as
    // CallAudioBridge's own local speaker monitor.
    std::optional<WindowsAudioOutput> dialAudio_;
    std::thread dialAudioThread_;
    ModemState lastObservedState_ = ModemState::CommandMode;

    // Hayes "+++" escape-sequence detection (added 2026-11-23 after live
    // testing found there was genuinely no way to send ATH once
    // ModemState::OnlineData was reached -- see docs/ROADMAP.md Phase
    // 4). Real Hayes modems require: silence for a guard time, exactly
    // three '+' characters typed with no gap longer than the guard time
    // between them, then silence for the guard time again -- only then
    // is it a real escape and not, say, the literal text "+++" arriving
    // as data mid-stream. `escapeState_` tracks how many of those '+'
    // have been seen so far; while in any non-Idle state, those '+'
    // bytes are held back (not yet sent as data) because they might
    // still turn out to be real data, not an escape -- see
    // HandleOnlineDataByte()/FlushHeldPluses(). Reset to Idle on the
    // instant CurrentState() isn't OnlineData, so stale state never
    // survives into the next call.
    enum class EscapeDetectState { Idle, SawPlus1, SawPlus2, SawPlus3PendingSilence };
    EscapeDetectState escapeState_ = EscapeDetectState::Idle;
    std::chrono::steady_clock::time_point lastOnlineDataByteAt_{};
    std::chrono::steady_clock::time_point thirdPlusAt_{};
    static constexpr std::chrono::milliseconds kEscapeGuardTime{1000};
};

} // namespace voipmodem
