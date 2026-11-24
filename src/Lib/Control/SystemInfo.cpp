#include "SystemInfo.h"

#include <algorithm>
#include <cctype>
#include <map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace voipmodem {

namespace {

bool SamePort(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
               return std::toupper(x) == std::toupper(y);
           });
}

#ifdef _WIN32

// As UTF-8 (driver names are localised: "Modem padrão de 9600 bps").
std::string ReadString(HKEY key, const wchar_t* value) {
    wchar_t buffer[512];
    DWORD size = sizeof buffer - sizeof(wchar_t);
    DWORD type = 0;
    if (RegQueryValueExW(key, value, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &size) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return "";
    }
    const size_t chars = size / sizeof(wchar_t);
    buffer[chars < 512 ? chars : 511] = L'\0';
    const int n = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) {
        return "";
    }
    std::string utf8(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buffer, -1, &utf8[0], n, nullptr, nullptr);
    return utf8;
}

std::vector<std::string> SubKeys(HKEY key) {
    std::vector<std::string> names;
    char name[256];
    for (DWORD i = 0;; ++i) {
        DWORD len = sizeof name;
        if (RegEnumKeyExA(key, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            break;
        }
        names.emplace_back(name, len);
    }
    return names;
}

// SERIALCOMM lists every serial port present now: value name = device
// (e.g. "\Device\com0com11"), data = port name ("COM4").
std::vector<std::pair<std::string, std::string>> ReadSerialComm() {
    std::vector<std::pair<std::string, std::string>> entries;
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return entries;
    }
    char name[256];
    char data[64];
    for (DWORD i = 0;; ++i) {
        DWORD nameLen = sizeof name;
        DWORD dataLen = sizeof data - 1;
        DWORD type = 0;
        if (RegEnumValueA(key, i, name, &nameLen, nullptr, &type, reinterpret_cast<BYTE*>(data), &dataLen) !=
            ERROR_SUCCESS) {
            break;
        }
        if (type == REG_SZ) {
            data[dataLen < sizeof data ? dataLen : sizeof data - 1] = '\0';
            entries.emplace_back(std::string(name, nameLen), std::string(data));
        }
    }
    RegCloseKey(key);
    return entries;
}

void ReadSerialPorts(SystemPorts& out, const std::vector<std::pair<std::string, std::string>>& serialComm) {
    for (const auto& entry : serialComm) {
        out.serialPorts.push_back(entry.second);
    }
    std::sort(out.serialPorts.begin(), out.serialPorts.end(), [](const std::string& a, const std::string& b) {
        // COM2 before COM10
        if (a.size() != b.size()) return a.size() < b.size();
        return a < b;
    });
}

// com0com registers its ports in SERIALCOMM as \Device\com0com1<n> (side
// A of pair <n>) and \Device\com0com2<n> (side B), whatever name they
// were given ("COM3", or the default "CNCA0").
void ReadCom0comPairs(SystemPorts& out, const std::vector<std::pair<std::string, std::string>>& serialComm) {
    static const std::string kPrefix = "\\Device\\com0com";
    std::map<std::string, PortPair> pairs; // by <n>
    for (const auto& entry : serialComm) {
        const std::string& device = entry.first;
        if (device.size() < kPrefix.size() + 2 || device.compare(0, kPrefix.size(), kPrefix) != 0) {
            continue;
        }
        const char side = device[kPrefix.size()];
        PortPair& pair = pairs[device.substr(kPrefix.size() + 1)];
        if (side == '1') {
            pair.a = entry.second;
        } else if (side == '2') {
            pair.b = entry.second;
        }
    }
    for (const auto& entry : pairs) {
        if (!entry.second.a.empty() && !entry.second.b.empty()) {
            out.com0comPairs.push_back(entry.second);
        }
    }
}

// Modems are devices of the Modem setup class; each instance's driver key
// says which port it is attached to.
void ReadModems(SystemPorts& out) {
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E96D-E325-11CE-BFC1-08002BE10318}", 0,
                      KEY_READ, &key) != ERROR_SUCCESS) {
        return;
    }
    for (const std::string& sub : SubKeys(key)) {
        HKEY modemKey;
        if (RegOpenKeyExA(key, sub.c_str(), 0, KEY_READ, &modemKey) != ERROR_SUCCESS) {
            continue;
        }
        ModemDriver modem;
        modem.attachedTo = ReadString(modemKey, L"AttachedTo");
        modem.name = ReadString(modemKey, L"FriendlyName");
        if (modem.name.empty()) {
            modem.name = ReadString(modemKey, L"DriverDesc");
        }
        RegCloseKey(modemKey);
        if (!modem.attachedTo.empty()) {
            out.modems.push_back(modem);
        }
    }
    RegCloseKey(key);
}

#endif

} // namespace

SystemPorts QuerySystemPorts() {
    SystemPorts ports;
#ifdef _WIN32
    const auto serialComm = ReadSerialComm();
    ReadSerialPorts(ports, serialComm);
    ReadCom0comPairs(ports, serialComm);
    ReadModems(ports);
#endif
    return ports;
}

std::string PairedPort(const SystemPorts& ports, const std::string& port) {
    for (const PortPair& pair : ports.com0comPairs) {
        if (SamePort(pair.a, port)) return pair.b;
        if (SamePort(pair.b, port)) return pair.a;
    }
    return "";
}

std::string DriverBinding(const SystemPorts& ports, const std::string& port) {
    const std::string other = PairedPort(ports, port);
    if (other.empty()) {
        return "(no com0com pair)";
    }
    for (const ModemDriver& modem : ports.modems) {
        if (SamePort(modem.attachedTo, other)) {
            return modem.name + " on " + other;
        }
    }
    return "(not bound) " + other;
}

} // namespace voipmodem
