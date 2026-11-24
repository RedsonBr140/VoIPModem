#pragma once

#include <string>
#include <vector>

#include "IniConfig.h"
#include "../Logging/Logger.h"

namespace voipmodem {

// Per-instance config. SIP fields are read here now that Phase 2 actually
// consumes them (PjsipCallController); RTP/DSP settings from
// docs/PROJECT_SPEC.md section 5 are still intentionally absent until
// Phase 3+ adds the modules that would consume them.
struct ModemInstanceConfig {
    // From "[Modem.<name>]"; empty for a legacy unnamed "[Modem]" section.
    // Used only to label log lines when multiple instances run at once —
    // see TaggedLogger.
    std::string name;
    std::string comPort = "COM3";
    int baudRate = 115200;
    std::string modemIdentification = "VoIPModem Virtual Modem";
    int autoAnswerRingCount = 0; // S0; 0 = auto-answer disabled

    // Default false: advertising V34Duplex in this instance's own V8
    // CM/JM menu makes a real, genuinely-V.34-capable remote modem
    // commit to V.34 training -- which this codebase can only follow
    // through Phase 2's first RTD/L1 exchange (see
    // docs/ROADMAP.md Phase 4), not an actual data connection. A real
    // modem left waiting for the rest would likely hang or fail the
    // call instead of gracefully negotiating down to V.21, which is
    // the one mode this codebase's V8 side currently completes
    // cleanly. Only turn this on for a controlled test between two of
    // this project's own instances (e.g. the com0com-paired local
    // test rig), never on an instance that takes real inbound calls.
    bool enableV34Probing = false;

    // Default false: whether this line's CallAudioBridge mirrors its
    // call audio to the local PC's default sound output device (see
    // WindowsAudioOutput/CallAudioBridge). Per-line and opt-in so two
    // VoIPModem instances calling each other on the SAME machine (the
    // com0com test rig) don't both open the shared default device and
    // mix into one indistinguishable stream -- enable it on whichever
    // single line you want to listen to.
    bool enableLocalAudio = false;

    // Which modulations this line's modem (libv90modem) offers, as a
    // comma list of V21, V22, V32BIS, V34, V90, V92, BELL103, BELL212A
    // (ini: `Modulations=`). Empty = libv90modem's defaults plus V.92.
    // e.g. "V21,V22,V32BIS,V34" keeps a call out of V.90/V.92 PCM.
    std::string modulations;

    // SIP registration for this instance's extension. If sipServer is
    // empty, this instance gets NullCallController instead of real SIP —
    // see ModemInstance and docs/ARCHITECTURE.md "SIP integration (Phase 2)".
    std::string sipServer;      // Asterisk host[:port], e.g. "10.174.7.172:5060"
    std::string sipExtension;   // AOR user part, e.g. "1003"
    std::string sipPassword;
    std::string sipRealm = "*"; // "*" matches any realm Asterisk challenges with
};

struct AppConfig {
    LogLevel logLevel = LogLevel::Info;
    std::string logFilePath = "voipmodem.log";

    // Local UDP port for the one shared SIP transport all instances
    // register through (see docs/ARCHITECTURE.md "SIP integration
    // (Phase 2)" for why this is process-wide, not per-instance). 0 means
    // no SIP transport is created at all — every instance then falls back
    // to NullCallController regardless of its own sipServer setting.
    int sipLocalPort = 0;

    // One entry per modem line to run. Always has at least one entry —
    // LoadAppConfig() falls back to a single default instance if the INI
    // defines none, matching pre-multi-instance Phase 1 behavior.
    std::vector<ModemInstanceConfig> modems;
};

AppConfig LoadAppConfig(const IniConfig& ini);

} // namespace voipmodem
