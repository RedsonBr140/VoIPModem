#pragma once

#include <istream>
#include <map>
#include <string>
#include <vector>

namespace voipmodem {

// Minimal INI reader: [Section] headers, key=value pairs, ';' or '#'
// line comments. No nested sections, no interpolation — Phase 1 only
// needs flat key/value lookups.
class IniConfig {
public:
    bool LoadFromFile(const std::string& path);

    // Same parsing as LoadFromFile, from an in-memory string. Exists so
    // tests can exercise section/key parsing (e.g. multi-instance
    // "[Modem.<name>]" sections) without touching the filesystem.
    void LoadFromString(const std::string& content);

    std::string GetString(const std::string& section, const std::string& key,
                           const std::string& defaultValue) const;
    int GetInt(const std::string& section, const std::string& key, int defaultValue) const;

    // Section names in the order they first appear in the file, in their
    // original case. Lets a caller discover a variable number of sections
    // (e.g. one "[Modem.<name>]" per modem instance) without knowing their
    // names ahead of time.
    const std::vector<std::string>& SectionNames() const { return sectionOrder_; }

private:
    using KeyValueMap = std::map<std::string, std::string>;

    void Parse(std::istream& in);

    std::map<std::string, KeyValueMap> sections_; // keyed by UPPERCASE section name
    std::vector<std::string> sectionOrder_;       // original case, first-seen order
};

} // namespace voipmodem
