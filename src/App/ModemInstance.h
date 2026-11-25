#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "CommandLineSession.h"
#include "../Lib/Audio/WindowsAudioOutput.h"
#include "../Lib/Config/AppConfig.h"
#include "../Lib/Control/LineStatus.h"
#include "../Lib/Control/ServiceStats.h"
#include "../Lib/Core/DtrDebouncer.h"
#include "../Lib/Core/ICallController.h"
#include "../Lib/Core/ModemStateMachine.h"
#include "../Lib/Logging/Logger.h"
#include "../Lib/Logging/TaggedLogger.h"
#include "../Lib/Serial/WindowsSerialPort.h"
#include "../Lib/Sip/PjsipEndpoint.h"

namespace voipmodem {

class PjsipCallController;

// Process-wide settings each line's SIP account needs (AppConfig [SIP] /
// [Media] / [Logging]).
struct LineRuntimeSettings {
    std::string sipTransport = "UDP";
    int sipRegisterExpiry = 300;
    bool sipRegisterOnStart = true;
    int rtpPort = 4000;
    int rtpPortRange = 0;
    bool traceAtCommands = false;
};

// An operator action on a line, from the control API. Queued and carried
// out on the line's own thread, which owns the state machine and the call.
enum class LineCommand {
    Answer,        // answer the ringing call (as ATA)
    HangUp,        // end the call, or refuse the ringing one
    InService,     // register again and accept calls
    OutOfService,  // hang up, unregister
    Register,      // re-register (if in service)
    Unregister     // unregister until the next Register / restart
};

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
// docs/ARCHITECTURE.md "SIP integration (Phase 2)". The SIP account is
// created (and registered) only once the COM port is open, so a line
// whose port is missing takes no calls.
class ModemInstance {
public:
    ModemInstance(ModemInstanceConfig config, Logger& sharedLogger, PjsipEndpoint* sipEndpoint,
                  const LineRuntimeSettings& runtime, ServiceStats& stats);
    ~ModemInstance();

    // Opens the serial port, then starts the SIP account. Returns false
    // (and logs why) if the port cannot be opened; the line then reports
    // "Port closed" and Open() may be retried later.
    bool Open();

    // Starts the read loop on a background thread. Call only after a
    // successful Open().
    void Start();

    // Signals the read loop to stop, joins the thread, and closes the
    // port. Safe to call even if Start() was never called.
    void Stop();

    bool IsRunning() const { return thread_.joinable(); }
    const std::string& Name() const { return config_.name; }
    const ModemInstanceConfig& Config() const { return config_; }

    // Thread-safe: queue an operator action (see LineCommand).
    void Post(LineCommand command);

    // Thread-safe: the line as of its last status refresh (twice a second).
    LineStatus Status() const;

    // Thread-safe: true while a call exists (ringing, dialing or up).
    bool InCall() const;

    // Thread-safe: [Logging] TraceAT changed.
    void SetTrace(bool enabled) { session_.SetTrace(enabled); }

private:
    void RunLoop();
    void PollCallEvents();
    void PumpIncomingCallRinging();
    void PumpDialAudioTrigger();
    void ProcessCommands();

    // Serial control signals (docs/ARCHITECTURE.md "Serial control
    // signals"). PumpControlSignals() polls the DTE's DTR (our DSR) once
    // per RunLoop iteration and hands debounced changes to the core;
    // UpdateCarrierOutput() sets our DTR (the DTE's DCD) to
    // ModemStateMachine::CarrierAsserted() if it differs.
    void PumpControlSignals();
    void UpdateCarrierOutput();
    void RefreshStatus();
    void RequestRegistration(bool registered);
    void TryRegistration();

    // Call bookkeeping for ServiceStats: state transitions (dial, answer)
    // and call events (incoming, connected, ended).
    void ObserveState();
    void ObserveEvent(const CallEvent& event);
    void FinishCall();

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
    void SendToCall(const std::vector<std::uint8_t>& bytes);

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
    LineRuntimeSettings runtime_;
    ServiceStats& stats_;
    TaggedLogger logger_;
    PjsipEndpoint* sipEndpoint_; // nullable; non-owning
    WindowsSerialPort serialPort_;
    // Declared before callController_: its factory sets it, so it must
    // already be initialised (to nullptr) by then.
    PjsipCallController* sip_ = nullptr; // callController_ when it is the SIP one
    std::unique_ptr<ICallController> callController_;
    bool sipStarted_ = false;
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

    // DTE's DTR, debounced: a drop must last this long to count, so a
    // glitch as com0com's far side opens or closes does not hang up.
    static constexpr std::chrono::milliseconds kDtrDropDebounce{50};
    DtrDebouncer dtrIn_{kDtrDropDebounce};
    std::optional<bool> dtrOut_; // our DTR as last set; empty = not set since Open()

    // Operator commands (Post), drained by the line's thread.
    std::mutex commandMutex_;
    std::deque<LineCommand> commands_;
    std::optional<bool> wantRegistered_; // a (un)registration PJSIP has not accepted yet
    std::chrono::steady_clock::time_point lastRegistrationAttempt_{};

    // The current (or last) call, for status and statistics. Line thread only.
    struct CallTrack {
        bool active = false;
        bool incoming = false;
        bool answered = false;
        bool connected = false;
        std::chrono::steady_clock::time_point start{};
        std::chrono::steady_clock::time_point connectedAt{};
        std::uint64_t bytesTx = 0;
        std::uint64_t bytesRx = 0;
        ModemLinkStatus lastModem;
    };
    CallTrack call_;
    ModemState statsState_ = ModemState::CommandMode;
    std::chrono::steady_clock::time_point lastFailedEndAt_{};
    bool lastCallFailed_ = false;
    unsigned long reportedRejectedBusy_ = 0;
    unsigned long reportedAtCommands_ = 0;
    std::chrono::steady_clock::time_point lastStatusRefresh_{};

    mutable std::mutex statusMutex_;
    LineStatus status_;
    std::atomic<bool> inCall_{false};
};

} // namespace voipmodem
