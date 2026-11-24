#include "Dialogs.h"

#include <commctrl.h>

#include <atomic>
#include <cctype>
#include <memory>
#include <thread>

#include "Util.h"
#include "resource.h"
#include "../Lib/Version.h"

namespace manager {

const wchar_t* const kRateNames[] = {L"V.21 300", L"V.22bis 2400", L"V.32bis 14400",
                                     L"V.34 33600", L"V.90 56000", L"V.92 56000"};
const char* const kRateKeys[] = {"V21", "V22", "V32BIS", "V34", "V90", "V92"};

void ModulationsToRange(const std::string& modulations, int& maxIndex, int& minIndex) {
    maxIndex = -1;
    minIndex = kRateCount;
    std::string item;
    for (size_t i = 0; i <= modulations.size(); ++i) {
        if (i == modulations.size() || modulations[i] == ',') {
            int index = -1;
            if (item == "V21") index = 0;
            else if (item == "V22" || item == "V22BIS") index = 1;
            else if (item == "V32" || item == "V32BIS") index = 2;
            else if (item == "V34") index = 3;
            else if (item == "V90") index = 4;
            else if (item == "V92") index = 5;
            if (index >= 0) {
                maxIndex = max(maxIndex, index);
                minIndex = min(minIndex, index);
            }
            item.clear();
        } else if (!std::isspace(static_cast<unsigned char>(modulations[i]))) {
            item += static_cast<char>(std::toupper(static_cast<unsigned char>(modulations[i])));
        }
    }
    if (maxIndex < 0) {
        maxIndex = kRateCount - 1;
        minIndex = 0;
    }
}

std::string RangeToModulations(int maxIndex, int minIndex) {
    if (maxIndex == kRateCount - 1 && minIndex == 0) {
        return ""; // the engine's defaults: everything up to V.92
    }
    std::string list;
    for (int i = minIndex; i <= maxIndex; ++i) {
        list += (list.empty() ? "" : ",") + std::string(kRateKeys[i]);
    }
    return list;
}

namespace {

void AddComboItems(HWND dlg, int id, std::initializer_list<const wchar_t*> items, int select) {
    HWND combo = GetDlgItem(dlg, id);
    for (const wchar_t* item : items) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    }
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(select), 0);
}

int ComboIndex(HWND dlg, int id) {
    return static_cast<int>(SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0));
}

void Error(HWND owner, const std::wstring& text) {
    MessageBoxW(owner, text.c_str(), L"VoIPModem Manager", MB_OK | MB_ICONEXCLAMATION);
}

// ------------------------------------------------------------ connect

struct ConnectState {
    std::string* host;
    int* port;
    std::string* password;
};

INT_PTR CALLBACK ConnectProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<ConnectState*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
        case WM_INITDIALOG:
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            st = reinterpret_cast<ConnectState*>(lp);
            SetItemText(dlg, IDC_CN_HOST, *st->host);
            SetDlgItemInt(dlg, IDC_CN_PORT, static_cast<UINT>(*st->port), FALSE);
            SetItemText(dlg, IDC_CN_PASSWORD, *st->password);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                const std::string host = GetItemText(dlg, IDC_CN_HOST);
                const int port = GetItemInt(dlg, IDC_CN_PORT, 0);
                if (host.empty() || port <= 0 || port > 65535) {
                    Error(dlg, L"Enter the computer (an address or a name) and a port from 1 to 65535.");
                    return TRUE;
                }
                *st->host = host;
                *st->port = port;
                *st->password = GetItemText(dlg, IDC_CN_PASSWORD);
                EndDialog(dlg, IDOK);
            } else if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
            }
            return TRUE;
    }
    return FALSE;
}

// While a dialog's request is with the service: OK greyed and relabelled,
// so it can't be sent twice. Cancel stays available.
void BeginSaving(HWND dlg, const wchar_t* label) {
    SetDlgItemTextW(dlg, IDOK, label);
    EnableWindow(GetDlgItem(dlg, IDOK), FALSE);
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
}

void EndSaving(HWND dlg, const wchar_t* label) {
    SetDlgItemTextW(dlg, IDOK, label);
    EnableWindow(GetDlgItem(dlg, IDOK), TRUE);
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
}

// The line field a refusal from the service is about, to put the cursor there.
int FieldForError(const std::string& error) {
    if (error.find("COM") != std::string::npos || error.find("port") != std::string::npos) return IDC_LN_COM;
    if (error.find("extension") != std::string::npos) return IDC_LN_EXT;
    if (error.find("name") != std::string::npos) return IDC_LN_NAME;
    return IDC_LN_EXT;
}

// ---------------------------------------------------------------- line

INT_PTR CALLBACK LineProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<LineDialogData*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            d = reinterpret_cast<LineDialogData*>(lp);
            const Json& c = d->config;
            SetWindowTextW(dlg, d->isNew ? L"Add modem line" : L"Line properties");
            SetDlgItemTextW(dlg, IDOK, d->isNew ? L"Create" : L"OK");

            HWND com = GetDlgItem(dlg, IDC_LN_COM);
            for (const std::string& p : d->ports) {
                SendMessageW(com, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Widen(p).c_str()));
            }
            if (d->isNew) {
                SendMessageW(GetDlgItem(dlg, IDC_LN_NAME), EM_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(L"automatic"));
                if (!d->ports.empty()) SendMessageW(com, CB_SETCURSEL, 0, 0);
            } else {
                SetItemText(dlg, IDC_LN_NAME, JStr(c, "id").empty() ? "(Modem)" : JStr(c, "id"));
                SendDlgItemMessageW(dlg, IDC_LN_NAME, EM_SETREADONLY, TRUE, 0);
                SetItemText(dlg, IDC_LN_COM, JStr(c, "comPort"));
                SetItemText(dlg, IDC_LN_EXT, JStr(c, "sipExtension"));
                SetItemText(dlg, IDC_LN_SERVER, JStr(c, "sipServer"));
                if (JBool(c, "passwordSet")) {
                    SetDlgItemTextW(dlg, IDC_LN_PASS_NOTE, L"blank: unchanged");
                }
            }

            HWND max = GetDlgItem(dlg, IDC_LN_MAX);
            SendMessageW(max, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Default (Modem defaults)"));
            for (int i = kRateCount - 1; i >= 0; --i) {
                SendMessageW(max, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kRateNames[i]));
            }
            int selection = 0;
            const std::string mods = JStr(c, "modulations");
            if (!d->isNew && mods != JStr(d->defaults, "modulations")) {
                int hi, lo;
                ModulationsToRange(mods, hi, lo);
                selection = kRateCount - hi;
            }
            SendMessageW(max, CB_SETCURSEL, static_cast<WPARAM>(selection), 0);

            const long long rings = d->isNew ? JInt(d->defaults, "autoAnswerRings", 1) : JInt(c, "autoAnswerRings");
            Check(dlg, IDC_LN_AUTO, d->isNew ? true : rings > 0);
            Check(dlg, IDC_LN_INSERVICE, d->isNew ? true : JBool(c, "inService", true));
            Check(dlg, IDC_LN_V42, JBool(d->isNew ? d->defaults : c, "errorControl", true));
            Check(dlg, IDC_LN_COMP, JBool(d->isNew ? d->defaults : c, "compression", true));
            if (!d->driver.empty()) {
                SetItemText(dlg, IDC_LN_DRIVER, "Windows modem driver: " + d->driver);
            } else if (d->isNew) {
                SetDlgItemTextW(dlg, IDC_LN_DRIVER,
                                L"Bind Windows' \"Standard Modem\" driver to the other side of the com0com pair "
                                L"to use the line from RAS or a dialer.");
            }
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                Json out = Json::object();
                const std::string com = GetItemText(dlg, IDC_LN_COM);
                const std::string ext = GetItemText(dlg, IDC_LN_EXT);
                if (com.empty() || ext.empty()) {
                    Error(dlg, L"A line needs a COM port and a SIP extension.");
                    return TRUE;
                }
                if (d->isNew) {
                    const std::string name = GetItemText(dlg, IDC_LN_NAME);
                    if (!name.empty()) out["id"] = name;
                }
                out["comPort"] = com;
                out["sipExtension"] = ext;
                out["sipServer"] = GetItemText(dlg, IDC_LN_SERVER);
                const std::string password = GetItemText(dlg, IDC_LN_PASS);
                if (!password.empty()) out["sipPassword"] = password;

                const int sel = ComboIndex(dlg, IDC_LN_MAX);
                if (sel <= 0) {
                    out["modulations"] = JStr(d->defaults, "modulations");
                } else {
                    int hi, lo;
                    ModulationsToRange(JStr(d->defaults, "modulations"), hi, lo);
                    const int chosen = kRateCount - sel;
                    out["modulations"] = RangeToModulations(chosen, min(lo, chosen));
                }

                const long long current = JInt(d->config, "autoAnswerRings");
                const long long def = JInt(d->defaults, "autoAnswerRings");
                out["autoAnswerRings"] = IsChecked(dlg, IDC_LN_AUTO) ? (current > 0 ? current : (def > 0 ? def : 1)) : 0;
                out["inService"] = IsChecked(dlg, IDC_LN_INSERVICE);
                out["errorControl"] = IsChecked(dlg, IDC_LN_V42);
                out["compression"] = IsChecked(dlg, IDC_LN_COMP);
                // Send it from here and wait: the dialog closes only once
                // the service has accepted the line.
                BeginSaving(dlg, d->isNew ? L"Adding the line…" : L"Saving…");
                if (d->isNew) {
                    d->client->Request(dlg, "line.add", {{"line", out}});
                } else {
                    d->client->Request(dlg, "line.update", {{"id", d->id}, {"line", out}});
                }
            } else if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
            }
            return TRUE;
        case WM_APP_RESPONSE: {
            std::unique_ptr<Response> r(reinterpret_cast<Response*>(lp));
            if (r->ok) {
                d->result = r->result;
                EndDialog(dlg, IDOK);
            } else {
                EndSaving(dlg, d->isNew ? L"Create" : L"OK");
                Error(dlg, L"The service did not accept the line:\r\n" + Widen(r->error) +
                               L"\r\n\r\nCorrect it and press " + (d->isNew ? L"Create" : L"OK") + L" again.");
                SetFocus(GetDlgItem(dlg, FieldForError(r->error)));
            }
            return TRUE;
        }
        case WM_APP_CONNECTION:
            delete reinterpret_cast<ConnectionInfo*>(lp);
            return TRUE;
    }
    return FALSE;
}

// ------------------------------------------------------------ settings

struct SettingsState {
    const Json* config;
    int tab;
    ControlClient* client;
    Json* result;
};

void ShowPage(HWND dlg, int page) {
    for (HWND child = GetWindow(dlg, GW_CHILD); child != nullptr; child = GetWindow(child, GW_HWNDNEXT)) {
        const int id = GetDlgCtrlID(child);
        if (id >= IDC_PAGE_FIRST && id < IDC_PAGE_FIRST + 5 * IDC_PAGE_SPAN) {
            const int owner = (id - IDC_PAGE_FIRST) / IDC_PAGE_SPAN;
            ShowWindow(child, owner == page ? SW_SHOW : SW_HIDE);
        }
    }
}

void SplitHostPort(const std::string& server, std::string& host, int& port) {
    host = server;
    port = 5060;
    const auto colon = server.rfind(':');
    if (colon != std::string::npos && colon + 1 < server.size() &&
        server.find_first_not_of("0123456789", colon + 1) == std::string::npos) {
        host = server.substr(0, colon);
        port = std::atoi(server.c_str() + colon + 1);
    }
}

INT_PTR CALLBACK SettingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            st = reinterpret_cast<SettingsState*>(lp);
            const Json& c = *st->config;
            SetWindowTextW(dlg, (L"VoIPModem settings — " + Widen(JStr(c, "path"))).c_str());

            HWND tab = GetDlgItem(dlg, IDC_SET_TAB);
            const wchar_t* tabs[] = {L"SIP account", L"Audio / RTP", L"Modem defaults", L"Logging", L"Control API"};
            for (int i = 0; i < 5; ++i) {
                TCITEMW item{};
                item.mask = TCIF_TEXT;
                item.pszText = const_cast<wchar_t*>(tabs[i]);
                TabCtrl_InsertItem(tab, i, &item);
            }
            // Under the page controls, or it paints over them.
            SetWindowPos(tab, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

            const Json& sip = c["sip"];
            std::string host;
            int port;
            SplitHostPort(JStr(sip, "server"), host, port);
            SetItemText(dlg, IDC_SIP_SERVER, host);
            SetDlgItemInt(dlg, IDC_SIP_PORT, static_cast<UINT>(port), FALSE);
            SetDlgItemInt(dlg, IDC_SIP_LOCALPORT, static_cast<UINT>(JInt(sip, "localPort")), FALSE);
            AddComboItems(dlg, IDC_SIP_TRANSPORT, {L"UDP", L"TCP"}, JStr(sip, "transport") == "TCP" ? 1 : 0);
            SetItemText(dlg, IDC_SIP_REALM, JStr(sip, "realm", "*"));
            SetDlgItemInt(dlg, IDC_SIP_EXPIRY, static_cast<UINT>(JInt(sip, "registerExpiry", 300)), FALSE);
            Check(dlg, IDC_SIP_REGONSTART, JBool(sip, "registerOnStart", true));

            const Json& media = c["media"];
            AddComboItems(dlg, IDC_MED_CODEC, {L"G.711 µ-law (PCMU)", L"G.711 A-law (PCMA)"},
                          JStr(media, "codec") == "PCMA" ? 1 : 0);
            const long long rtp = JInt(media, "rtpPort", 4000);
            const long long range = JInt(media, "rtpPortRange");
            SetItemText(dlg, IDC_MED_RTP, std::to_string(rtp) + (range > 0 ? "-" + std::to_string(rtp + range) : ""));
            SetDlgItemInt(dlg, IDC_MED_JITTER, static_cast<UINT>(JInt(media, "jitterBufferMs", 160)), FALSE);
            Check(dlg, IDC_MED_EC, JBool(media, "echoCanceller"));

            const Json& def = c["defaults"];
            int hi, lo;
            ModulationsToRange(JStr(def, "modulations"), hi, lo);
            for (int id : {IDC_MOD_MAX, IDC_MOD_MIN}) {
                for (int i = 0; i < kRateCount; ++i) {
                    SendDlgItemMessageW(dlg, id, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kRateNames[i]));
                }
            }
            SendDlgItemMessageW(dlg, IDC_MOD_MAX, CB_SETCURSEL, static_cast<WPARAM>(hi), 0);
            SendDlgItemMessageW(dlg, IDC_MOD_MIN, CB_SETCURSEL, static_cast<WPARAM>(lo), 0);
            AddComboItems(dlg, IDC_MOD_RINGS,
                          {L"Never (ATA only)", L"1 ring", L"2 rings", L"3 rings", L"4 rings", L"5 rings", L"6 rings"},
                          static_cast<int>(min(6LL, max(0LL, JInt(def, "autoAnswerRings")))));
            SetItemText(dlg, IDC_MOD_IDENT, JStr(def, "identification"));
            Check(dlg, IDC_MOD_V42, JBool(def, "errorControl", true));
            Check(dlg, IDC_MOD_COMP, JBool(def, "compression", true));

            const Json& log = c["logging"];
            SetItemText(dlg, IDC_LOG_FILE, JStr(log, "file"));
            const std::string level = JStr(log, "level", "INFO");
            AddComboItems(dlg, IDC_LOG_LEVEL, {L"Error", L"Warn", L"Info", L"Debug"},
                          level == "ERROR" ? 0 : level == "WARN" ? 1 : level == "DEBUG" ? 3 : 2);
            SetDlgItemInt(dlg, IDC_LOG_ROTATE, static_cast<UINT>(JInt(log, "rotateKB")), FALSE);
            SetDlgItemInt(dlg, IDC_LOG_KEEP, static_cast<UINT>(JInt(log, "keep", 5)), FALSE);
            Check(dlg, IDC_LOG_TRACEAT, JBool(log, "traceAt"));

            const Json& ctl = c["control"];
            SetDlgItemInt(dlg, IDC_CTL_PORT, static_cast<UINT>(JInt(ctl, "port", 7880)), FALSE);
            SetItemText(dlg, IDC_CTL_BIND, JStr(ctl, "bindAddress", "127.0.0.1"));
            EnableWindow(GetDlgItem(dlg, IDC_CTL_CLEARPW), JBool(ctl, "passwordSet"));
            if (JBool(ctl, "passwordSet")) {
                SendDlgItemMessageW(dlg, IDC_CTL_PASSWORD, EM_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(L"(set; blank keeps it)"));
            }

            TabCtrl_SetCurSel(tab, st->tab);
            ShowPage(dlg, st->tab);
            return TRUE;
        }
        case WM_NOTIFY: {
            const auto* hdr = reinterpret_cast<NMHDR*>(lp);
            if (hdr->idFrom == IDC_SET_TAB && hdr->code == TCN_SELCHANGE) {
                ShowPage(dlg, TabCtrl_GetCurSel(hdr->hwndFrom));
            }
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            if (LOWORD(wp) != IDOK) {
                return FALSE;
            }
            {
                const Json& c = *st->config;
                Json changes = Json::object();
                auto put = [&](const char* group, const char* key, const Json& value) {
                    const Json& old = c.contains(group) ? c[group] : Json::object();
                    if (!old.contains(key) || old[key] != value) {
                        changes[group][key] = value;
                    }
                };

                const std::string host = GetItemText(dlg, IDC_SIP_SERVER);
                const int port = GetItemInt(dlg, IDC_SIP_PORT, 5060);
                put("sip", "server", host.empty() ? "" : (port == 5060 ? host : host + ":" + std::to_string(port)));
                put("sip", "localPort", GetItemInt(dlg, IDC_SIP_LOCALPORT, 0));
                put("sip", "transport", ComboIndex(dlg, IDC_SIP_TRANSPORT) == 1 ? "TCP" : "UDP");
                put("sip", "realm", GetItemText(dlg, IDC_SIP_REALM));
                put("sip", "registerExpiry", GetItemInt(dlg, IDC_SIP_EXPIRY, 300));
                put("sip", "registerOnStart", IsChecked(dlg, IDC_SIP_REGONSTART));

                put("media", "codec", ComboIndex(dlg, IDC_MED_CODEC) == 1 ? "PCMA" : "PCMU");
                const std::string rtp = GetItemText(dlg, IDC_MED_RTP);
                const int first = std::atoi(rtp.c_str());
                const auto dash = rtp.find('-');
                const int last = dash == std::string::npos ? 0 : std::atoi(rtp.c_str() + dash + 1);
                if (first <= 0 || first > 65535 || (dash != std::string::npos && last < first)) {
                    Error(dlg, L"RTP ports: a first port (1-65535), or first-last.");
                    return TRUE;
                }
                put("media", "rtpPort", first);
                put("media", "rtpPortRange", dash == std::string::npos ? 0 : last - first);
                put("media", "jitterBufferMs", GetItemInt(dlg, IDC_MED_JITTER, 160));
                put("media", "echoCanceller", IsChecked(dlg, IDC_MED_EC));

                const int hi = ComboIndex(dlg, IDC_MOD_MAX);
                const int lo = ComboIndex(dlg, IDC_MOD_MIN);
                if (lo > hi) {
                    Error(dlg, L"The minimum rate is above the maximum rate.");
                    return TRUE;
                }
                put("defaults", "modulations", RangeToModulations(hi, lo));
                put("defaults", "autoAnswerRings", ComboIndex(dlg, IDC_MOD_RINGS));
                put("defaults", "identification", GetItemText(dlg, IDC_MOD_IDENT));
                put("defaults", "errorControl", IsChecked(dlg, IDC_MOD_V42));
                put("defaults", "compression", IsChecked(dlg, IDC_MOD_COMP));

                static const char* const kLevels[] = {"Error", "Warn", "Info", "Debug"};
                put("logging", "file", GetItemText(dlg, IDC_LOG_FILE));
                const std::string level = kLevels[max(0, ComboIndex(dlg, IDC_LOG_LEVEL))];
                std::string oldLevel = JStr(c["logging"], "level");
                for (auto& ch : oldLevel) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                std::string upper = level;
                for (auto& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                if (upper != oldLevel) changes["logging"]["level"] = level;
                put("logging", "rotateKB", GetItemInt(dlg, IDC_LOG_ROTATE, 0));
                put("logging", "keep", GetItemInt(dlg, IDC_LOG_KEEP, 5));
                put("logging", "traceAt", IsChecked(dlg, IDC_LOG_TRACEAT));

                put("control", "port", GetItemInt(dlg, IDC_CTL_PORT, 7880));
                put("control", "bindAddress", GetItemText(dlg, IDC_CTL_BIND));
                const std::string password = GetItemText(dlg, IDC_CTL_PASSWORD);
                if (!password.empty()) changes["control"]["password"] = password;
                if (IsChecked(dlg, IDC_CTL_CLEARPW)) changes["control"]["clearPassword"] = true;
                if (JStr(changes.contains("control") ? changes["control"] : Json::object(), "bindAddress") != "" &&
                    JStr(changes["control"], "bindAddress") != "127.0.0.1" && !JBool(c["control"], "passwordSet") &&
                    password.empty()) {
                    if (MessageBoxW(dlg,
                                    L"The control API will listen beyond this computer without a password: anyone "
                                    L"who can reach the port can change every setting. Continue?",
                                    L"VoIPModem settings", MB_YESNO | MB_ICONWARNING) != IDYES) {
                        return TRUE;
                    }
                }
                if (changes.empty()) {
                    EndDialog(dlg, IDCANCEL); // nothing to save
                    return TRUE;
                }
                BeginSaving(dlg, L"Saving…");
                st->client->Request(dlg, "config.set", changes);
            }
            return TRUE;
        case WM_APP_RESPONSE: {
            std::unique_ptr<Response> r(reinterpret_cast<Response*>(lp));
            if (r->ok) {
                *st->result = r->result;
                EndDialog(dlg, IDOK);
            } else {
                EndSaving(dlg, L"OK");
                Error(dlg, L"The service did not accept the settings:\r\n" + Widen(r->error) +
                               L"\r\n\r\nNothing was saved. Correct it and press OK again.");
            }
            return TRUE;
        }
        case WM_APP_CONNECTION:
            delete reinterpret_cast<ConnectionInfo*>(lp);
            return TRUE;
    }
    return FALSE;
}

// ----------------------------------------------------------- self-test

INT_PTR CALLBACK SelfTestProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* client = reinterpret_cast<ControlClient*>(GetWindowLongPtrW(dlg, DWLP_USER));
    static const char* const kKeys[] = {"V21", "V22", "V32BIS", "V34"};
    switch (msg) {
        case WM_INITDIALOG:
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            AddComboItems(dlg, IDC_ST_MOD, {L"V.21", L"V.22bis", L"V.32bis", L"V.34"}, 3);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_ST_RUN) {
                const int sel = max(0, ComboIndex(dlg, IDC_ST_MOD));
                EnableWindow(GetDlgItem(dlg, IDC_ST_RUN), FALSE);
                SetDlgItemTextW(dlg, IDC_ST_RESULT, L"Running on the service...");
                client->Request(dlg, "selftest", {{"modulations", kKeys[sel]}});
            } else if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
            }
            return TRUE;
        case WM_APP_RESPONSE: {
            std::unique_ptr<Response> r(reinterpret_cast<Response*>(lp));
            EnableWindow(GetDlgItem(dlg, IDC_ST_RUN), TRUE);
            if (!r->ok) {
                SetDlgItemTextW(dlg, IDC_ST_RESULT, (L"Could not run: " + Widen(r->error)).c_str());
            } else if (JBool(r->result, "connected")) {
                wchar_t text[300];
                swprintf(text, 300,
                         L"PASSED: the two modems connected at %s %lld/%lld bit/s after %.1f s of line time "
                         L"(%.2f s of processing on the server).",
                         Widen(JStr(r->result, "modulation")).c_str(), JInt(r->result, "txBps"),
                         JInt(r->result, "rxBps"), JNum(r->result, "lineSeconds"), JNum(r->result, "wallSeconds"));
                SetDlgItemTextW(dlg, IDC_ST_RESULT, text);
            } else {
                SetDlgItemTextW(dlg, IDC_ST_RESULT, (L"FAILED: " + Widen(JStr(r->result, "failure"))).c_str());
            }
            return TRUE;
        }
        case WM_APP_CONNECTION:
            delete reinterpret_cast<ConnectionInfo*>(lp);
            return TRUE;
    }
    return FALSE;
}

// ---------------------------------------------------- ports and drivers

void AddColumns(HWND list, std::initializer_list<std::pair<const wchar_t*, int>> columns) {
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    int i = 0;
    for (const auto& col : columns) {
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = const_cast<wchar_t*>(col.first);
        c.cx = col.second;
        ListView_InsertColumn(list, i++, &c);
    }
}

void AddRow(HWND list, std::initializer_list<std::wstring> cells) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = ListView_GetItemCount(list);
    int sub = 0;
    for (const std::wstring& cell : cells) {
        item.iSubItem = sub;
        item.pszText = const_cast<wchar_t*>(cell.c_str());
        if (sub == 0) {
            item.iItem = ListView_InsertItem(list, &item);
        } else {
            ListView_SetItem(list, &item);
        }
        ++sub;
    }
}

INT_PTR CALLBACK PortsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* client = reinterpret_cast<ControlClient*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            client = reinterpret_cast<ControlClient*>(lp);
            AddColumns(GetDlgItem(dlg, IDC_PT_PAIRS), {{L"Port A", 90}, {L"Port B", 90}, {L"VoIPModem line", 230}});
            AddColumns(GetDlgItem(dlg, IDC_PT_MODEMS),
                       {{L"Modem driver", 200}, {L"Attached to", 80}, {L"Paired with", 80}, {L"Line", 120}});
            SetDlgItemTextW(dlg, IDC_PT_NOTE,
                            L"Each VoIPModem line opens one side of a com0com pair; Windows' modem driver (and RAS) "
                            L"uses the other side. Create pairs with com0com's setup tool; bind a driver in Phone and "
                            L"Modem Options > Modems > Add, \"Don't detect my modem\", Standard Modem Types, then "
                            L"the pair's other port.");
            client->Request(dlg, "system.ports");
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_PT_REFRESH) {
                client->Request(dlg, "system.ports");
            } else if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
            }
            return TRUE;
        case WM_APP_RESPONSE: {
            std::unique_ptr<Response> r(reinterpret_cast<Response*>(lp));
            HWND pairs = GetDlgItem(dlg, IDC_PT_PAIRS);
            HWND modems = GetDlgItem(dlg, IDC_PT_MODEMS);
            ListView_DeleteAllItems(pairs);
            ListView_DeleteAllItems(modems);
            if (!r->ok) {
                AddRow(pairs, {L"(" + Widen(r->error) + L")", L"", L""});
                return TRUE;
            }
            for (const Json& p : r->result["com0comPairs"]) {
                std::wstring line = Widen(JStr(p, "lineA"));
                if (!JStr(p, "lineB").empty()) line += (line.empty() ? L"" : L", ") + Widen(JStr(p, "lineB"));
                AddRow(pairs, {Widen(JStr(p, "a")), Widen(JStr(p, "b")), line.empty() ? L"—" : line});
            }
            if (r->result["com0comPairs"].empty()) {
                AddRow(pairs, {L"(none: is com0com installed?)", L"", L""});
            }
            for (const Json& m : r->result["modems"]) {
                AddRow(modems, {Widen(JStr(m, "name")), Widen(JStr(m, "attachedTo")),
                                Widen(JStr(m, "pairedWith", "")), Widen(JStr(m, "line"))});
            }
            if (r->result["modems"].empty()) {
                AddRow(modems, {L"(no modem drivers installed)", L"", L"", L""});
            }
            return TRUE;
        }
        case WM_APP_CONNECTION:
            delete reinterpret_cast<ConnectionInfo*>(lp);
            return TRUE;
    }
    return FALSE;
}

// ------------------------------------------------------------ SIP trace

struct TraceState {
    const std::deque<LogLine>* log;
    ControlClient* client;
    size_t shown = static_cast<size_t>(-1);
    unsigned long long lastSeq = 0;
};

void RefreshTrace(HWND dlg, TraceState* st, bool force) {
    const bool all = IsChecked(dlg, IDC_TR_ALL);
    const unsigned long long newest = st->log->empty() ? 0 : st->log->front().seq;
    if (!force && newest == st->lastSeq) return;
    st->lastSeq = newest;
    std::wstring text;
    // The deque is newest first; the trace reads oldest first.
    for (auto it = st->log->rbegin(); it != st->log->rend(); ++it) {
        if (!all && it->message.find(L"[SIP]") == std::wstring::npos) continue;
        text += it->time + L"  " + it->message + L"\r\n";
    }
    HWND edit = GetDlgItem(dlg, IDC_TR_TEXT);
    SetWindowTextW(edit, text.c_str());
    SendMessageW(edit, EM_LINESCROLL, 0, 0x7fffffff);
}

INT_PTR CALLBACK TraceProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<TraceState*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            st = reinterpret_cast<TraceState*>(lp);
            static HFONT mono = CreateFontW(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0,
                                            FIXED_PITCH | FF_MODERN, L"Courier New");
            SendDlgItemMessageW(dlg, IDC_TR_TEXT, WM_SETFONT, reinterpret_cast<WPARAM>(mono), TRUE);
            RefreshTrace(dlg, st, true);
            SetTimer(dlg, 1, 1000, nullptr);
            return TRUE;
        }
        case WM_TIMER:
            RefreshTrace(dlg, st, false);
            return TRUE;
        case WM_SIZE: {
            const int w = LOWORD(lp), h = HIWORD(lp);
            RECT r{0, 0, 7, 7};
            MapDialogRect(dlg, &r);
            const int m = r.right;
            RECT b{0, 0, 50, 14};
            MapDialogRect(dlg, &b);
            const int bh = b.bottom, bw = b.right;
            MoveWindow(GetDlgItem(dlg, IDC_TR_TEXT), m, m, w - 2 * m, h - 3 * m - bh, TRUE);
            MoveWindow(GetDlgItem(dlg, IDC_TR_ALL), m, h - m - bh + 2, 2 * bw + 20, bh - 4, TRUE);
            MoveWindow(GetDlgItem(dlg, IDCANCEL), w - m - bw, h - m - bh, bw, bh, TRUE);
            MoveWindow(GetDlgItem(dlg, IDC_TR_DEBUG), w - 2 * m - bw - 2 * bw, h - m - bh, 2 * bw, bh, TRUE);
            InvalidateRect(dlg, nullptr, TRUE);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_TR_ALL) {
                RefreshTrace(dlg, st, true);
            } else if (LOWORD(wp) == IDC_TR_DEBUG) {
                st->client->Request(dlg, "config.set", {{"logging", {{"level", "Debug"}}}});
                EnableWindow(GetDlgItem(dlg, IDC_TR_DEBUG), FALSE);
            } else if (LOWORD(wp) == IDCANCEL) {
                KillTimer(dlg, 1);
                EndDialog(dlg, IDCANCEL);
            }
            return TRUE;
        case WM_APP_RESPONSE: {
            std::unique_ptr<Response> r(reinterpret_cast<Response*>(lp));
            if (!r->ok) {
                Error(dlg, L"Could not change the log level: " + Widen(r->error));
                EnableWindow(GetDlgItem(dlg, IDC_TR_DEBUG), TRUE);
            }
            return TRUE;
        }
        case WM_APP_CONNECTION:
            delete reinterpret_cast<ConnectionInfo*>(lp);
            return TRUE;
    }
    return FALSE;
}

// ----------------------------------------------------- AT command console

constexpr UINT WM_APP_SERIAL = WM_APP + 20;

struct ConsoleState {
    const std::vector<std::string>* ports;
    std::string initial;
    HANDLE port = INVALID_HANDLE_VALUE;
    std::thread reader;
    std::atomic<bool> stop{false};
};

void AppendOutput(HWND dlg, const std::string& raw) {
    std::wstring text;
    for (size_t i = 0; i < raw.size(); ++i) {
        const char ch = raw[i];
        if (ch == '\r') {
            text += L"\r\n";
            if (i + 1 < raw.size() && raw[i + 1] == '\n') ++i;
        } else if (ch == '\n') {
            text += L"\r\n";
        } else if (static_cast<unsigned char>(ch) >= 32 || ch == '\t') {
            text += static_cast<wchar_t>(static_cast<unsigned char>(ch));
        }
    }
    HWND out = GetDlgItem(dlg, IDC_CON_OUT);
    if (GetWindowTextLengthW(out) > 60000) {
        SetWindowTextW(out, L"");
    }
    const int end = GetWindowTextLengthW(out);
    SendMessageW(out, EM_SETSEL, static_cast<WPARAM>(end), end);
    SendMessageW(out, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
}

void ClosePort(HWND dlg, ConsoleState* st) {
    if (st->port == INVALID_HANDLE_VALUE) return;
    st->stop = true;
    if (st->reader.joinable()) st->reader.join();
    CloseHandle(st->port);
    st->port = INVALID_HANDLE_VALUE;
    SetDlgItemTextW(dlg, IDC_CON_OPEN, L"Open");
    EnableWindow(GetDlgItem(dlg, IDC_CON_PORT), TRUE);
}

void OpenPort(HWND dlg, ConsoleState* st) {
    const std::wstring name = GetText(GetDlgItem(dlg, IDC_CON_PORT));
    HANDLE h = CreateFileW((L"\\\\.\\" + name).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        Error(dlg, L"Cannot open " + name +
                       (err == ERROR_ACCESS_DENIED ? L": it is in use (by RAS, a dialer, or VoIPModem itself)."
                                                   : L" (error " + std::to_wstring(err) + L")."));
        return;
    }
    DCB dcb{};
    dcb.DCBlength = sizeof dcb;
    GetCommState(h, &dcb);
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    SetCommState(h, &dcb);
    COMMTIMEOUTS t{};
    t.ReadIntervalTimeout = MAXDWORD;
    t.ReadTotalTimeoutConstant = 100;
    t.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(h, &t);
    st->port = h;
    st->stop = false;
    st->reader = std::thread([dlg, st, h] {
        char buf[512];
        while (!st->stop) {
            DWORD n = 0;
            if (!ReadFile(h, buf, sizeof buf, &n, nullptr)) break;
            if (n > 0) PostMessageW(dlg, WM_APP_SERIAL, 0, reinterpret_cast<LPARAM>(new std::string(buf, n)));
        }
    });
    SetDlgItemTextW(dlg, IDC_CON_OPEN, L"Close");
    EnableWindow(GetDlgItem(dlg, IDC_CON_PORT), FALSE);
    AppendOutput(dlg, "[" + Narrow(name) + " open: type AT commands below]\r\n");
    SetFocus(GetDlgItem(dlg, IDC_CON_IN));
}

INT_PTR CALLBACK ConsoleProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<ConsoleState*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            st = reinterpret_cast<ConsoleState*>(lp);
            for (const std::string& p : *st->ports) {
                SendDlgItemMessageW(dlg, IDC_CON_PORT, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Widen(p).c_str()));
            }
            SetItemText(dlg, IDC_CON_PORT, st->initial);
            SetDlgItemTextW(dlg, IDC_CON_NOTE,
                            L"Opens a port on this computer: the modem side of a line's com0com pair "
                            L"(not the port VoIPModem itself holds).");
            static HFONT mono = CreateFontW(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0,
                                            FIXED_PITCH | FF_MODERN, L"Courier New");
            SendDlgItemMessageW(dlg, IDC_CON_OUT, WM_SETFONT, reinterpret_cast<WPARAM>(mono), TRUE);
            SendDlgItemMessageW(dlg, IDC_CON_IN, WM_SETFONT, reinterpret_cast<WPARAM>(mono), TRUE);
            SetItemText(dlg, IDC_CON_IN, "ATI");
            return TRUE;
        }
        case WM_APP_SERIAL: {
            std::unique_ptr<std::string> data(reinterpret_cast<std::string*>(lp));
            AppendOutput(dlg, *data);
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_CON_OPEN:
                    if (st->port == INVALID_HANDLE_VALUE) OpenPort(dlg, st);
                    else ClosePort(dlg, st);
                    return TRUE;
                case IDC_CON_SEND:
                case IDOK: {
                    if (st->port == INVALID_HANDLE_VALUE) {
                        OpenPort(dlg, st);
                        if (st->port == INVALID_HANDLE_VALUE) return TRUE;
                    }
                    const std::string line = GetItemText(dlg, IDC_CON_IN) + "\r";
                    DWORD written = 0;
                    WriteFile(st->port, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
                    SetItemText(dlg, IDC_CON_IN, "");
                    SetFocus(GetDlgItem(dlg, IDC_CON_IN));
                    return TRUE;
                }
                case IDCANCEL:
                    ClosePort(dlg, st);
                    EndDialog(dlg, IDCANCEL);
                    return TRUE;
            }
            return FALSE;
    }
    return FALSE;
}

// ---------------------------------------------------------------- about

INT_PTR CALLBACK AboutProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG:
            SetDlgItemTextW(dlg, IDC_AB_TEXT, reinterpret_cast<const wchar_t*>(lp));
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) EndDialog(dlg, IDOK);
            return TRUE;
    }
    return FALSE;
}

HINSTANCE Instance() {
    return GetModuleHandleW(nullptr);
}

} // namespace

bool ShowConnectDialog(HWND owner, std::string& host, int& port, std::string& password) {
    ConnectState st{&host, &port, &password};
    return DialogBoxParamW(Instance(), MAKEINTRESOURCEW(IDD_CONNECT), owner, ConnectProc,
                           reinterpret_cast<LPARAM>(&st)) == IDOK;
}

bool ShowLineDialog(HWND owner, LineDialogData& data) {
    return DialogBoxParamW(Instance(), MAKEINTRESOURCEW(IDD_LINE), owner, LineProc, reinterpret_cast<LPARAM>(&data)) ==
           IDOK;
}

bool ShowSettingsDialog(HWND owner, const Json& config, int tab, ControlClient& client, Json& result) {
    SettingsState st{&config, tab, &client, &result};
    return DialogBoxParamW(Instance(), MAKEINTRESOURCEW(IDD_SETTINGS), owner, SettingsProc,
                           reinterpret_cast<LPARAM>(&st)) == IDOK;
}

void ShowSelfTestDialog(HWND owner, ControlClient& client) {
    DialogBoxParamW(Instance(), MAKEINTRESOURCEW(IDD_SELFTEST), owner, SelfTestProc, reinterpret_cast<LPARAM>(&client));
}

void ShowPortsDialog(HWND owner, ControlClient& client, bool drivers) {
    (void)drivers; // one window shows both: pairs above, drivers below
    DialogBoxParamW(Instance(), MAKEINTRESOURCEW(IDD_PORTS), owner, PortsProc, reinterpret_cast<LPARAM>(&client));
}

void ShowTraceDialog(HWND owner, const std::deque<LogLine>& log, ControlClient& client) {
    TraceState st{&log, &client};
    DialogBoxParamW(Instance(), MAKEINTRESOURCEW(IDD_TRACE), owner, TraceProc, reinterpret_cast<LPARAM>(&st));
}

void ShowConsoleDialog(HWND owner, const std::vector<std::string>& ports, const std::string& initialPort) {
    ConsoleState st;
    st.ports = &ports;
    st.initial = initialPort;
    DialogBoxParamW(Instance(), MAKEINTRESOURCEW(IDD_CONSOLE), owner, ConsoleProc, reinterpret_cast<LPARAM>(&st));
}

void ShowAboutDialog(HWND owner, const Json& hello, bool connected) {
    std::wstring text = L"VoIPModem Manager " + Widen(VOIPMODEM_VERSION) + L"\r\n\r\n";
    if (connected) {
        text += L"Service: VoIPModem " + Widen(JStr(hello, "version")) + L" on " + Widen(JStr(hello, "host")) + L" (" +
                Widen(JStr(hello, "platform")) + L")" + (JBool(hello, "runningAsService") ? L", as a Windows service" : L"") +
                L"\r\nModem engine: " + Widen(JStr(hello, "engine")) + L"\r\nSIP stack: " +
                (JStr(hello, "pjsip").empty() ? std::wstring(L"off ([SIP] LocalPort is 0)")
                                              : L"PJSIP " + Widen(JStr(hello, "pjsip"))) +
                L"\r\nConfiguration: " + Widen(JStr(hello, "configPath")) +
                L"\r\n\r\n";
    } else {
        text += L"Not connected to a VoIPModem service.\r\n\r\n";
    }
    text += L"VoIPModem links libv90modem (GPL-2.0-only) and PJSIP (GPL-2.0 or later); its binaries are "
            L"distributed under GPL-2.0. The Manager also uses JSON for Modern C++ (MIT).";
    DialogBoxParamW(Instance(), MAKEINTRESOURCEW(IDD_ABOUT), owner, AboutProc, reinterpret_cast<LPARAM>(text.c_str()));
}

} // namespace manager
