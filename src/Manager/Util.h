#pragma once

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

namespace manager {

inline std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

inline std::string Narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

inline std::wstring GetText(HWND hwnd) {
    const int n = GetWindowTextLengthW(hwnd);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(hwnd, &s[0], n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}

inline std::string GetItemText(HWND dlg, int id) {
    return Narrow(GetText(GetDlgItem(dlg, id)));
}

inline void SetItemText(HWND dlg, int id, const std::string& text) {
    SetDlgItemTextW(dlg, id, Widen(text).c_str());
}

inline int GetItemInt(HWND dlg, int id, int fallback) {
    BOOL ok = FALSE;
    const UINT v = GetDlgItemInt(dlg, id, &ok, FALSE);
    return ok ? static_cast<int>(v) : fallback;
}

inline bool IsChecked(HWND dlg, int id) {
    return IsDlgButtonChecked(dlg, id) == BST_CHECKED;
}

inline void Check(HWND dlg, int id, bool on) {
    CheckDlgButton(dlg, id, on ? BST_CHECKED : BST_UNCHECKED);
}

// Typed reads of a JSON object field, with a fallback.
inline std::string JStr(const nlohmann::json& j, const char* key, const std::string& fallback = "") {
    return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : fallback;
}
inline double JNum(const nlohmann::json& j, const char* key, double fallback = 0) {
    return j.is_object() && j.contains(key) && j[key].is_number() ? j[key].get<double>() : fallback;
}
inline long long JInt(const nlohmann::json& j, const char* key, long long fallback = 0) {
    return j.is_object() && j.contains(key) && j[key].is_number() ? static_cast<long long>(j[key].get<double>())
                                                                  : fallback;
}
inline bool JBool(const nlohmann::json& j, const char* key, bool fallback = false) {
    return j.is_object() && j.contains(key) && j[key].is_boolean() ? j[key].get<bool>() : fallback;
}

// "00:12:48", or "3d 04:22:07".
inline std::wstring FormatDuration(long long seconds) {
    if (seconds < 0) return L"—";
    wchar_t buf[48];
    const long long d = seconds / 86400;
    const long long h = (seconds / 3600) % 24;
    const long long m = (seconds / 60) % 60;
    const long long s = seconds % 60;
    if (d > 0) {
        swprintf(buf, 48, L"%lldd %02lld:%02lld:%02lld", d, h, m, s);
    } else {
        swprintf(buf, 48, L"%02lld:%02lld:%02lld", seconds / 3600, m, s);
    }
    return buf;
}

// 1.82 M, 410 K, 14 K, 512.
inline std::wstring FormatBytes(std::uint64_t bytes) {
    wchar_t buf[32];
    if (bytes >= 1000ull * 1000 * 1000) {
        swprintf(buf, 32, L"%.2f G", bytes / 1e9);
    } else if (bytes >= 1000ull * 1000) {
        swprintf(buf, 32, L"%.2f M", bytes / 1e6);
    } else if (bytes >= 1000) {
        swprintf(buf, 32, L"%llu K", static_cast<unsigned long long>(bytes / 1000));
    } else {
        swprintf(buf, 32, L"%llu", static_cast<unsigned long long>(bytes));
    }
    return buf;
}

// 1,428
inline std::wstring FormatCount(long long n) {
    std::wstring digits = std::to_wstring(n < 0 ? -n : n);
    std::wstring out;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count > 0 && count % 3 == 0) out.insert(out.begin(), L',');
        out.insert(out.begin(), *it);
        ++count;
    }
    return n < 0 ? L"-" + out : out;
}

} // namespace manager
