#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "IniConfig.h"
#include "../Logging/Logger.h"

namespace voipmodem {

// One modem line ("[Modem]" or "[Modem.<name>]"). A key a line section
// leaves out comes from "[Defaults]" (the Manager's "Modem defaults"), and
// failing that from the value below; SipServer/SipRealm fall back to
// "[SIP] Server"/"[SIP] Realm".
struct ModemInstanceConfig {
    // From "[Modem.<name>]"; empty for a legacy unnamed "[Modem]" section.
    // Labels the line's log lines (TaggedLogger) and identifies it in the
    // control API.
    std::string name;
    std::string comPort = "COM3";
    int baudRate = 115200;
    std::string modemIdentification = "VoIPModem Virtual Modem";
    int autoAnswerRingCount = 0; // S0; 0 = auto-answer disabled

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

    // V.42 error control (LAPM) on the modem link (`ErrorControl=`); off =
    // plain start-stop characters. `Compression=`: V.42bis/V.44 offered
    // inside LAPM.
    bool errorControl = true;
    bool compression = true;

    // `InService=0` takes the line out of service: its SIP extension is
    // not registered, so it receives no calls (the COM port stays open).
    bool inService = true;

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
    int logRotateKB = 0;          // [Logging] RotateKB; 0 = never rotate
    int logKeepFiles = 5;         // [Logging] Keep
    bool traceAtCommands = false; // [Logging] TraceAT: every AT command line and response at Info

    // Local port for the one shared SIP transport all instances register
    // through (see docs/ARCHITECTURE.md "SIP integration (Phase 2)" for why
    // this is process-wide, not per-instance). 0 means no SIP transport is
    // created at all — every instance then falls back to
    // NullCallController regardless of its own sipServer setting.
    int sipLocalPort = 0;
    std::string sipTransport = "UDP"; // [SIP] Transport: UDP or TCP
    std::string sipServer;            // [SIP] Server: default registrar for lines without SipServer
    std::string sipRealm = "*";       // [SIP] Realm
    int sipRegisterExpiry = 300;      // [SIP] RegisterExpiry, seconds
    bool sipRegisterOnStart = true;   // [SIP] RegisterOnStart: register lines as soon as they start

    // [Media]: the call audio path (see docs/ARCHITECTURE.md "Media path").
    std::string preferredCodec = "PCMU"; // PCMU (mu-law) or PCMA (A-law), offered first
    int jitterBufferMs = 160;            // initial prefetch; the other depths derive from it
    int rtpPort = 4000;                  // first local RTP port
    int rtpPortRange = 0;                // ports above rtpPort to use; 0 = no limit
    bool echoCanceller = false;          // pjmedia's echo canceller; off, it degrades modem training

    // [Control]: the TCP control API the Manager uses (docs/CONTROL_API.md).
    int controlPort = 7880;                    // 0 = no control API
    std::string controlBindAddress = "127.0.0.1";
    std::string controlPassword;               // empty = no authentication

    // One entry per modem line to run. Always has at least one entry —
    // LoadAppConfig() falls back to a single default instance if the INI
    // defines none, matching pre-multi-instance Phase 1 behavior.
    std::vector<ModemInstanceConfig> modems;
};

AppConfig LoadAppConfig(const IniConfig& ini);

// The modem settings a line gets when its section leaves them out
// ("[Defaults]" over the built-in defaults). `name` and `comPort` empty.
ModemInstanceConfig LoadLineDefaults(const IniConfig& ini);

// The ini section name of a line: "Modem" for the unnamed legacy line,
// "Modem.<name>" otherwise.
std::string ModemSectionName(const std::string& lineName);

LogLevel ParseLogLevel(const std::string& value, LogLevel defaultValue);

} // namespace voipmodem
