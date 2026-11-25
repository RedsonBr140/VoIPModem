#include "AtCommandParser.h"

#include <cctype>

namespace voipmodem {

namespace {

std::string Trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::string ToUpperCopy(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

bool TryParseDigits(const std::string& body, size_t& i, int& outValue) {
    const size_t start = i;
    while (i < body.size() && std::isdigit(static_cast<unsigned char>(body[i]))) {
        ++i;
    }
    if (i == start) {
        return false;
    }
    try {
        outValue = std::stoi(body.substr(start, i - start));
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

// Value ranges for the commands Windows' generic modem driver sends in its
// init strings. A missing digit means 0, as for every other Hayes command.
bool IsValidSettingValue(AtCommandType type, int value) {
    switch (type) {
        case AtCommandType::SetDcdMode:          return value >= 0 && value <= 1;
        case AtCommandType::SetDtrMode:          return value >= 0 && value <= 3;
        case AtCommandType::SetFlowControl:      return value == 0 || (value >= 3 && value <= 6);
        case AtCommandType::SetResultCodeSet:    return value >= 0 && value <= 4;
        case AtCommandType::SetSpeedNegotiation: return value >= 0 && value <= 1;
        default:                                 return true; // other commands are not range-checked here
    }
}

// Unimodem sends "+++", waits, then "ATH<cr>" to hang up even when the call
// has already ended and the modem is back in command mode. There the '+'
// bytes are ordinary command-line characters, so the line arrives as
// "+++ATH". Skip a leading run of '+' when "AT" follows it, so that line
// runs as "ATH". Anything else that starts with '+' is left alone (and is
// still an ERROR for lacking the AT prefix).
std::string StripLeadingEscapeChars(const std::string& s) {
    const auto pos = s.find_first_not_of('+');
    if (pos == 0 || pos == std::string::npos || s.size() - pos < 2) {
        return s;
    }
    if (std::toupper(static_cast<unsigned char>(s[pos])) != 'A' ||
        std::toupper(static_cast<unsigned char>(s[pos + 1])) != 'T') {
        return s;
    }
    return s.substr(pos);
}

} // namespace

AtParseResult AtCommandParser::Parse(const std::string& line) {
    AtParseResult result;
    const std::string trimmed = StripLeadingEscapeChars(Trim(line));

    if (trimmed.empty()) {
        // A bare blank line is a no-op in command mode, not an error.
        result.isValid = true;
        return result;
    }

    if (trimmed.size() < 2 ||
        std::toupper(static_cast<unsigned char>(trimmed[0])) != 'A' ||
        std::toupper(static_cast<unsigned char>(trimmed[1])) != 'T') {
        result.isValid = false;
        return result;
    }

    const std::string body = trimmed.substr(2);
    if (body.empty()) {
        result.isValid = true;
        result.commands.push_back(
            AtCommand{AtCommandType::Attention, std::nullopt, std::nullopt, AtDialMode::Default, ""});
        return result;
    }

    size_t i = 0;
    while (i < body.size()) {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(body[i])));

        if (c == 'D') {
            size_t rest = i + 1;
            AtDialMode mode = AtDialMode::Default;
            if (rest < body.size()) {
                const char modeChar = static_cast<char>(std::toupper(static_cast<unsigned char>(body[rest])));
                if (modeChar == 'T') {
                    mode = AtDialMode::Tone;
                    ++rest;
                } else if (modeChar == 'P') {
                    mode = AtDialMode::Pulse;
                    ++rest;
                }
            }
            AtCommand cmd;
            cmd.type = AtCommandType::Dial;
            cmd.dialMode = mode;
            cmd.stringParam = body.substr(rest);
            result.commands.push_back(cmd);
            i = body.size();
            break;
        }

        if (c == 'A') {
            result.commands.push_back(
                AtCommand{AtCommandType::Answer, std::nullopt, std::nullopt, AtDialMode::Default, ""});
            ++i;
            continue;
        }

        if (c == 'S') {
            ++i;
            int regIndex = 0;
            if (!TryParseDigits(body, i, regIndex)) {
                result.isValid = false;
                return result;
            }

            if (i < body.size() && body[i] == '=') {
                ++i;
                int value = 0;
                if (!TryParseDigits(body, i, value)) {
                    result.isValid = false;
                    return result;
                }
                AtCommand cmd;
                cmd.type = AtCommandType::SetRegister;
                cmd.registerIndex = regIndex;
                cmd.numericParam = value;
                result.commands.push_back(cmd);
                continue;
            }

            if (i < body.size() && body[i] == '?') {
                ++i;
                AtCommand cmd;
                cmd.type = AtCommandType::QueryRegister;
                cmd.registerIndex = regIndex;
                result.commands.push_back(cmd);
                continue;
            }

            result.isValid = false; // "Sn" with no "=" or "?" is malformed
            return result;
        }

        if (c == '+' || c == '#') {
            // V.25ter extended commands ("+FCLASS", "+GMM", ...) and vendor
            // extensions ("#CLS", ...). Windows' generic modem driver probes
            // these during "Query Modem" diagnostics and RAS setup. Consumes
            // the rest of the line, like ATD — real devices don't chain
            // another command after one of these either.
            const char prefixChar = c;
            size_t nameStart = i + 1;
            size_t j = nameStart;
            while (j < body.size() && std::isalpha(static_cast<unsigned char>(body[j]))) {
                ++j;
            }
            if (j == nameStart) {
                result.isValid = false; // "+" or "#" with no command name
                return result;
            }
            const std::string name = ToUpperCopy(body.substr(nameStart, j - nameStart));

            AtExtendedSuffix suffix = AtExtendedSuffix::None;
            std::string value;
            if (j + 1 < body.size() && body[j] == '=' && body[j + 1] == '?') {
                suffix = AtExtendedSuffix::Test;
            } else if (j < body.size() && body[j] == '?') {
                suffix = AtExtendedSuffix::Query;
            } else if (j < body.size() && body[j] == '=') {
                suffix = AtExtendedSuffix::SetValue;
                value = body.substr(j + 1);
            }

            AtCommand cmd;
            cmd.type = AtCommandType::ExtendedCommand;
            cmd.extendedPrefix = prefixChar;
            cmd.extendedName = name;
            cmd.extendedSuffix = suffix;
            cmd.extendedValue = value;
            result.commands.push_back(cmd);
            i = body.size();
            break;
        }

        if (c == '&') {
            ++i;
            if (i >= body.size()) {
                result.isValid = false; // bare "&"
                return result;
            }
            AtCommandType ampType;
            switch (std::toupper(static_cast<unsigned char>(body[i]))) {
                case 'F': ampType = AtCommandType::FactoryDefaults; break;
                case 'C': ampType = AtCommandType::SetDcdMode; break;
                case 'D': ampType = AtCommandType::SetDtrMode; break;
                case 'K': ampType = AtCommandType::SetFlowControl; break;
                default:
                    result.isValid = false; // any other &<letter> is unsupported
                    return result;
            }
            ++i;
            AtCommand cmd;
            cmd.type = ampType;
            int value = 0;
            if (TryParseDigits(body, i, value)) {
                cmd.numericParam = value;
            }
            if (!IsValidSettingValue(ampType, cmd.numericParam.value_or(0))) {
                result.isValid = false;
                return result;
            }
            result.commands.push_back(cmd);
            continue;
        }

        AtCommandType type;
        switch (c) {
            case 'Z': type = AtCommandType::Reset; break;
            case 'E': type = AtCommandType::SetEcho; break;
            case 'V': type = AtCommandType::SetVerbose; break;
            case 'Q': type = AtCommandType::SetQuiet; break;
            case 'L': type = AtCommandType::SetSpeakerVolume; break;
            case 'M': type = AtCommandType::SetSpeakerMode; break;
            case 'I': type = AtCommandType::Identify; break;
            case 'H': type = AtCommandType::HangUp; break;
            case 'X': type = AtCommandType::SetResultCodeSet; break;
            case 'N': type = AtCommandType::SetSpeedNegotiation; break;
            default:
                result.isValid = false;
                return result;
        }

        ++i;
        AtCommand cmd;
        cmd.type = type;
        int value = 0;
        if (TryParseDigits(body, i, value)) {
            cmd.numericParam = value;
        }
        if (!IsValidSettingValue(type, cmd.numericParam.value_or(0))) {
            result.isValid = false;
            return result;
        }
        result.commands.push_back(cmd);
    }

    result.isValid = true;
    return result;
}

} // namespace voipmodem
