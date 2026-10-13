#include "IniConfig.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

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

std::string ToUpper(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

} // namespace

bool IniConfig::LoadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }
    Parse(in);
    return true;
}

void IniConfig::LoadFromString(const std::string& content) {
    std::istringstream in(content);
    Parse(in);
}

void IniConfig::Parse(std::istream& in) {
    std::string currentSection;
    std::string rawLine;
    while (std::getline(in, rawLine)) {
        const std::string line = Trim(rawLine);
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            const std::string rawName = Trim(line.substr(1, line.size() - 2));
            currentSection = ToUpper(rawName);
            if (sections_.find(currentSection) == sections_.end()) {
                sectionOrder_.push_back(rawName);
            }
            sections_.emplace(currentSection, KeyValueMap{});
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        const std::string key = ToUpper(Trim(line.substr(0, eq)));
        const std::string value = Trim(line.substr(eq + 1));
        sections_[currentSection][key] = value;
    }
}

std::string IniConfig::GetString(const std::string& section, const std::string& key,
                                  const std::string& defaultValue) const {
    const auto sectionIt = sections_.find(ToUpper(section));
    if (sectionIt == sections_.end()) {
        return defaultValue;
    }
    const auto keyIt = sectionIt->second.find(ToUpper(key));
    if (keyIt == sectionIt->second.end()) {
        return defaultValue;
    }
    return keyIt->second;
}

int IniConfig::GetInt(const std::string& section, const std::string& key, int defaultValue) const {
    const std::string value = GetString(section, key, "");
    if (value.empty()) {
        return defaultValue;
    }
    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        return defaultValue;
    }
}

} // namespace voipmodem
