#pragma once

#include <map>
#include <string>

namespace voipmodem {

// Minimal INI reader: [Section] headers, key=value pairs, ';' or '#'
// line comments. No nested sections, no interpolation — Phase 1 only
// needs flat key/value lookups.
class IniConfig {
public:
    bool LoadFromFile(const std::string& path);

    std::string GetString(const std::string& section, const std::string& key,
                           const std::string& defaultValue) const;
    int GetInt(const std::string& section, const std::string& key, int defaultValue) const;

private:
    using KeyValueMap = std::map<std::string, std::string>;
    std::map<std::string, KeyValueMap> sections_;
};

} // namespace voipmodem
