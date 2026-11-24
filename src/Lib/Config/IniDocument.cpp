#include "IniDocument.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

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

bool EqualsNoCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
               return std::toupper(x) == std::toupper(y);
           });
}

// "[Name]" -> "Name"; false if `line` is not a section header.
bool SectionHeader(const std::string& line, std::string& name) {
    const std::string t = Trim(line);
    if (t.size() < 2 || t.front() != '[' || t.back() != ']') {
        return false;
    }
    name = Trim(t.substr(1, t.size() - 2));
    return true;
}

// "Key=Value" (not a comment) -> "Key"; false otherwise.
bool KeyOf(const std::string& line, std::string& key) {
    const std::string t = Trim(line);
    if (t.empty() || t[0] == ';' || t[0] == '#' || t[0] == '[') {
        return false;
    }
    const auto eq = t.find('=');
    if (eq == std::string::npos) {
        return false;
    }
    key = Trim(t.substr(0, eq));
    return true;
}

} // namespace

bool IniDocument::LoadFromFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        lines_.clear();
        return false;
    }
    std::ostringstream content;
    content << in.rdbuf();
    LoadFromString(content.str());
    return true;
}

void IniDocument::LoadFromString(const std::string& content) {
    lines_.clear();
    std::string line;
    std::istringstream in(content);
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines_.push_back(line);
    }
}

std::string IniDocument::ToString() const {
    std::string out;
    for (const std::string& line : lines_) {
        out += line;
        out += "\r\n";
    }
    return out;
}

bool IniDocument::SaveToFile(const std::string& path) const {
    const std::string temp = path + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << ToString();
        if (!out.good()) {
            return false;
        }
    }
#ifdef _WIN32
    if (!MoveFileExA(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::remove(temp.c_str());
        return false;
    }
#else
    if (std::rename(temp.c_str(), path.c_str()) != 0) {
        std::remove(temp.c_str());
        return false;
    }
#endif
    return true;
}

IniDocument::Range IniDocument::FindSection(const std::string& section) const {
    Range range;
    for (size_t i = 0; i < lines_.size(); ++i) {
        std::string name;
        if (!SectionHeader(lines_[i], name)) {
            continue;
        }
        if (range.found) {
            range.end = i;
            return range;
        }
        if (EqualsNoCase(name, section)) {
            range.found = true;
            range.header = i;
        }
    }
    if (range.found) {
        range.end = lines_.size();
    }
    return range;
}

bool IniDocument::HasSection(const std::string& section) const {
    return FindSection(section).found;
}

bool IniDocument::HasKey(const std::string& section, const std::string& key) const {
    const Range range = FindSection(section);
    if (!range.found) {
        return false;
    }
    for (size_t i = range.header + 1; i < range.end; ++i) {
        std::string k;
        if (KeyOf(lines_[i], k) && EqualsNoCase(k, key)) {
            return true;
        }
    }
    return false;
}

void IniDocument::Set(const std::string& section, const std::string& key, const std::string& value) {
    const std::string newLine = key + "=" + value;
    Range range = FindSection(section);
    if (!range.found) {
        if (!lines_.empty() && !Trim(lines_.back()).empty()) {
            lines_.push_back("");
        }
        lines_.push_back("[" + section + "]");
        lines_.push_back(newLine);
        return;
    }

    size_t lastKey = range.header;
    for (size_t i = range.header + 1; i < range.end; ++i) {
        std::string k;
        if (!KeyOf(lines_[i], k)) {
            continue;
        }
        if (EqualsNoCase(k, key)) {
            lines_[i] = newLine;
            return;
        }
        lastKey = i;
    }
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(lastKey + 1), newLine);
}

void IniDocument::Remove(const std::string& section, const std::string& key) {
    Range range = FindSection(section);
    if (!range.found) {
        return;
    }
    for (size_t i = range.end; i-- > range.header + 1;) {
        std::string k;
        if (KeyOf(lines_[i], k) && EqualsNoCase(k, key)) {
            lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
}

void IniDocument::RemoveSection(const std::string& section) {
    const Range range = FindSection(section);
    if (!range.found) {
        return;
    }
    // Up to the section's last key: comments after it usually introduce
    // whatever comes next, so they stay.
    size_t end = range.header + 1;
    for (size_t i = range.header + 1; i < range.end; ++i) {
        std::string k;
        if (KeyOf(lines_[i], k)) {
            end = i + 1;
        }
    }
    lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(range.header),
                 lines_.begin() + static_cast<std::ptrdiff_t>(end));
}

} // namespace voipmodem
