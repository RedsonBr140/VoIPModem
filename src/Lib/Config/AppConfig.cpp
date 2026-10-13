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

LogLevel ParseLogLevel(const std::string& value, LogLevel defaultValue) {
    const std::string upper = ToUpper(value);
    if (upper == "ERROR") return LogLevel::Error;
    if (upper == "WARN") return LogLevel::Warn;
    if (upper == "INFO") return LogLevel::Info;
    if (upper == "DEBUG") return LogLevel::Debug;
    return defaultValue;
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

} // namespace

AppConfig LoadAppConfig(const IniConfig& ini) {
    AppConfig config;
    config.logLevel = ParseLogLevel(ini.GetString("Logging", "Level", "Info"), config.logLevel);
    config.logFilePath = ini.GetString("Logging", "File", config.logFilePath);
    config.sipLocalPort = ini.GetInt("SIP", "LocalPort", config.sipLocalPort);

    for (const std::string& sectionName : ini.SectionNames()) {
        std::string instanceName;
        if (!IsModemSection(sectionName, instanceName)) {
            continue;
        }

        ModemInstanceConfig modem;
        modem.name = instanceName;
        modem.comPort = ini.GetString(sectionName, "ComPort", modem.comPort);
        modem.baudRate = ini.GetInt(sectionName, "BaudRate", modem.baudRate);
        modem.modemIdentification = ini.GetString(sectionName, "Identification", modem.modemIdentification);
        modem.autoAnswerRingCount = ini.GetInt(sectionName, "AutoAnswerRingCount", modem.autoAnswerRingCount);
        modem.sipServer = ini.GetString(sectionName, "SipServer", modem.sipServer);
        modem.sipExtension = ini.GetString(sectionName, "SipExtension", modem.sipExtension);
        modem.sipPassword = ini.GetString(sectionName, "SipPassword", modem.sipPassword);
        modem.sipRealm = ini.GetString(sectionName, "SipRealm", modem.sipRealm);
        config.modems.push_back(std::move(modem));
    }

    if (config.modems.empty()) {
        config.modems.push_back(ModemInstanceConfig{}); // no INI / no [Modem...] section: one default line
    }

    return config;
}

} // namespace voipmodem
