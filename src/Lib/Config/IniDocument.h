#pragma once

#include <string>
#include <vector>

namespace voipmodem {

// An ini file edited in place: keeps every line it does not change
// (comments, blank lines, key order, unknown keys), so a hand-written
// voipmodem.ini survives the Manager saving settings into it. Section and
// key names match case-insensitively, as IniConfig reads them. Reading
// values is IniConfig's job; this only writes.
class IniDocument {
public:
    bool LoadFromFile(const std::string& path);
    void LoadFromString(const std::string& content);

    // Writes to `path` through a temporary file, so a failed write never
    // leaves a half-written ini. Lines end with CRLF.
    bool SaveToFile(const std::string& path) const;
    std::string ToString() const;

    // Replaces the value of the first `key` in `section`, or adds the key
    // after the section's last key (creating the section at the end of the
    // file if needed).
    void Set(const std::string& section, const std::string& key, const std::string& value);

    // Removes every `key` line in `section`. No-op if absent.
    void Remove(const std::string& section, const std::string& key);

    // Removes the section header and every line up to its last key;
    // comments after that key are kept, since they usually introduce
    // whatever follows.
    void RemoveSection(const std::string& section);

    bool HasSection(const std::string& section) const;
    bool HasKey(const std::string& section, const std::string& key) const;

private:
    struct Range {
        size_t header = 0; // index of the "[...]" line
        size_t end = 0;    // one past the section's last line
        bool found = false;
    };
    Range FindSection(const std::string& section) const;

    std::vector<std::string> lines_;
};

} // namespace voipmodem
