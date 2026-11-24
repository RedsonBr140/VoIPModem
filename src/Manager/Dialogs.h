#pragma once

#include <windows.h>

#include <deque>
#include <string>
#include <vector>

#include "ControlClient.h"

namespace manager {

// One line of the service's event log, as the Manager keeps it.
struct LogLine {
    unsigned long long seq = 0;
    std::wstring time;    // "14:46:02"
    std::wstring level;   // "ERROR", ...
    std::wstring message;
};

// Connect: in = last values; true on OK.
bool ShowConnectDialog(HWND owner, std::string& host, int& port, std::string& password);

// New line (isNew) or line properties. `config` is the line's entry from
// "config.get" (empty for a new line), `defaults` that answer's
// "defaults", `ports` the COM ports the service's computer has, `driver`
// the status' "driver" text. On OK, `out` is the "line" object to send
// with line.add / line.update.
struct LineDialogData {
    bool isNew = true;
    Json config;
    Json defaults;
    std::vector<std::string> ports;
    std::string driver;
    Json out;
};
bool ShowLineDialog(HWND owner, LineDialogData& data);

// Settings, from a "config.get" answer, opened on `tab` (0 SIP, 1 audio,
// 2 modem defaults, 3 logging, 4 control API). On OK, `changes` holds the
// "config.set" arguments (only what changed).
bool ShowSettingsDialog(HWND owner, const Json& config, int tab, Json& changes);

void ShowSelfTestDialog(HWND owner, ControlClient& client);
void ShowPortsDialog(HWND owner, ControlClient& client, bool drivers);
void ShowTraceDialog(HWND owner, const std::deque<LogLine>& log, ControlClient& client);
void ShowConsoleDialog(HWND owner, const std::vector<std::string>& ports, const std::string& initialPort);
void ShowAboutDialog(HWND owner, const Json& hello, bool connected);

// The rate choices: Modulations= <-> (maximum, minimum) indexes into
// kRates (V.21 ... V.92). Empty Modulations = V.21 to V.92.
extern const wchar_t* const kRateNames[];
extern const char* const kRateKeys[];
constexpr int kRateCount = 6;
void ModulationsToRange(const std::string& modulations, int& maxIndex, int& minIndex);
std::string RangeToModulations(int maxIndex, int minIndex);

} // namespace manager
