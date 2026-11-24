#include "AppConfig.h"

#include <algorithm>
#include <cctype>

namespace voipmodem {

namespace {

std::string ToUpper(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

// Matches a bare "[Modem]" section (legacy single-instance config; name
// comes back empty) or a "[Modem.<name>]" section (one line of a
// multi-instance config; name is <name> in its original case). Returns
// false for anything else (e.g. "[Logging]").
bool IsModemSection(const std::string& sectionName, std::string& outInstanceName) {
    const std::string upper = ToUpper(sectionName);
    if (upper == "MODEM") {
        outInstanceName.clear();
        return true;
    }

    static const std::string kPrefix = "MODEM.";
    if (upper.size() > kPrefix.size() && upper.compare(0, kPrefix.size(), kPrefix) == 0) {
        outInstanceName = sectionName.substr(kPrefix.size());
        return true;
    }

    return false;
}

bool GetBool(const IniConfig& ini, const std::string& section, const std::string& key, bool defaultValue) {
    return ini.GetInt(section, key, defaultValue ? 1 : 0) != 0;
}

// Reads one line's modem settings from `section`, each missing key taken
// from `defaults`.
ModemInstanceConfig ReadLine(const IniConfig& ini, const std::string& section, const ModemInstanceConfig& defaults) {
    ModemInstanceConfig modem = defaults;
    modem.comPort = ini.GetString(section, "ComPort", defaults.comPort);
    modem.baudRate = ini.GetInt(section, "BaudRate", defaults.baudRate);
    modem.modemIdentification = ini.GetString(section, "Identification", defaults.modemIdentification);
    modem.autoAnswerRingCount = ini.GetInt(section, "AutoAnswerRingCount", defaults.autoAnswerRingCount);
    modem.enableLocalAudio = GetBool(ini, section, "EnableLocalAudio", defaults.enableLocalAudio);
    modem.modulations = ini.GetString(section, "Modulations", defaults.modulations);
    modem.errorControl = GetBool(ini, section, "ErrorControl", defaults.errorControl);
    modem.compression = GetBool(ini, section, "Compression", defaults.compression);
    modem.inService = GetBool(ini, section, "InService", defaults.inService);
    modem.sipServer = ini.GetString(section, "SipServer", defaults.sipServer);
    modem.sipExtension = ini.GetString(section, "SipExtension", defaults.sipExtension);
    modem.sipPassword = ini.GetString(section, "SipPassword", defaults.sipPassword);
    modem.sipRealm = ini.GetString(section, "SipRealm", defaults.sipRealm);
    return modem;
}

} // namespace

LogLevel ParseLogLevel(const std::string& value, LogLevel defaultValue) {
    const std::string upper = ToUpper(value);
    if (upper == "ERROR") return LogLevel::Error;
    if (upper == "WARN" || upper == "WARNING") return LogLevel::Warn;
    if (upper == "INFO") return LogLevel::Info;
    if (upper == "DEBUG") return LogLevel::Debug;
    return defaultValue;
}

std::string ModemSectionName(const std::string& lineName) {
    return lineName.empty() ? "Modem" : "Modem." + lineName;
}

ModemInstanceConfig LoadLineDefaults(const IniConfig& ini) {
    ModemInstanceConfig builtIn;
    builtIn.sipServer = ini.GetString("SIP", "Server", "");
    builtIn.sipRealm = ini.GetString("SIP", "Realm", builtIn.sipRealm);
    ModemInstanceConfig defaults = ReadLine(ini, "Defaults", builtIn);
    defaults.name.clear();
    defaults.comPort.clear();
    defaults.sipExtension.clear();
    defaults.sipPassword.clear();
    defaults.inService = true;
    return defaults;
}

AppConfig LoadAppConfig(const IniConfig& ini) {
    AppConfig config;
    config.logLevel = ParseLogLevel(ini.GetString("Logging", "Level", "Info"), config.logLevel);
    config.logFilePath = ini.GetString("Logging", "File", config.logFilePath);
    config.logRotateKB = std::max(0, ini.GetInt("Logging", "RotateKB", config.logRotateKB));
    config.logKeepFiles = std::max(1, ini.GetInt("Logging", "Keep", config.logKeepFiles));
    config.traceAtCommands = GetBool(ini, "Logging", "TraceAT", config.traceAtCommands);

    config.sipLocalPort = ini.GetInt("SIP", "LocalPort", config.sipLocalPort);
    config.sipTransport = ToUpper(ini.GetString("SIP", "Transport", config.sipTransport));
    if (config.sipTransport != "TCP") {
        config.sipTransport = "UDP";
    }
    config.sipServer = ini.GetString("SIP", "Server", config.sipServer);
    config.sipRealm = ini.GetString("SIP", "Realm", config.sipRealm);
    config.sipRegisterExpiry = std::max(30, ini.GetInt("SIP", "RegisterExpiry", config.sipRegisterExpiry));
    config.sipRegisterOnStart = GetBool(ini, "SIP", "RegisterOnStart", config.sipRegisterOnStart);

    config.preferredCodec = ToUpper(ini.GetString("Media", "Codec", config.preferredCodec));
    if (config.preferredCodec != "PCMA") {
        config.preferredCodec = "PCMU";
    }
    config.jitterBufferMs = std::min(2000, std::max(20, ini.GetInt("Media", "JitterBufferMs", config.jitterBufferMs)));
    config.rtpPort = std::max(0, ini.GetInt("Media", "RtpPort", config.rtpPort));
    config.rtpPortRange = std::max(0, ini.GetInt("Media", "RtpPortRange", config.rtpPortRange));
    config.echoCanceller = GetBool(ini, "Media", "EchoCanceller", config.echoCanceller);

    config.controlPort = ini.GetInt("Control", "Port", config.controlPort);
    config.controlBindAddress = ini.GetString("Control", "BindAddress", config.controlBindAddress);
    config.controlPassword = ini.GetString("Control", "Password", config.controlPassword);

    const ModemInstanceConfig defaults = LoadLineDefaults(ini);
    for (const std::string& sectionName : ini.SectionNames()) {
        std::string instanceName;
        if (!IsModemSection(sectionName, instanceName)) {
            continue;
        }
        ModemInstanceConfig base = defaults;
        base.comPort = ModemInstanceConfig{}.comPort;
        ModemInstanceConfig modem = ReadLine(ini, sectionName, base);
        modem.name = instanceName;
        config.modems.push_back(std::move(modem));
    }

    if (config.modems.empty()) {
        config.modems.push_back(ModemInstanceConfig{}); // no INI / no [Modem...] section: one default line
    }

    return config;
}

} // namespace voipmodem
