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

} // namespace

AtParseResult AtCommandParser::Parse(const std::string& line) {
    AtParseResult result;
    const std::string trimmed = Trim(line);

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

        if (c == '&') {
            ++i;
            if (i >= body.size() || std::toupper(static_cast<unsigned char>(body[i])) != 'F') {
                result.isValid = false; // only &F is supported in Phase 1
                return result;
            }
            ++i;
            AtCommand cmd;
            cmd.type = AtCommandType::FactoryDefaults;
            int value = 0;
            if (TryParseDigits(body, i, value)) {
                cmd.numericParam = value;
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
            case 'I': type = AtCommandType::Identify; break;
            case 'H': type = AtCommandType::HangUp; break;
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
        result.commands.push_back(cmd);
    }

    result.isValid = true;
    return result;
}

} // namespace voipmodem
