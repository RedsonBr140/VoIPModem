// VoIPModem Manager: the operator's window onto a running VoIPModem
// service, through its control API (docs/CONTROL_API.md). It holds no
// modem state of its own: everything shown comes from the service, every
// action goes to it. Plain Win32 and common controls, so the same binary
// style runs from Windows XP / Server 2003 to current Windows.
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "ControlClient.h"
#include "Dialogs.h"
#include "Util.h"
#include "resource.h"
#include "../Lib/Version.h"

namespace manager {

namespace {

const wchar_t kClassName[] = L"VoIPModemManager";
const wchar_t kRegistryKey[] = L"Software\\VoIPModem\\Manager";
const wchar_t kDocsUrl[] = L"https://github.com/RedsonBr140/VoIPModem/tree/main/docs";
const wchar_t kReadmeUrl[] = L"https://github.com/RedsonBr140/VoIPModem#readme";

constexpr UINT kPollTimer = 1;
constexpr UINT kPollMs = 1000;
constexpr size_t kMaxLogLines = 2000;

// The palette of the design (docs/design): state colours by line kind.
constexpr COLORREF kUp = RGB(0x00, 0x66, 0x00);
constexpr COLORREF kNeg = RGB(0x8a, 0x5a, 0x00);
constexpr COLORREF kIdle = RGB(0x55, 0x55, 0x55);
constexpr COLORREF kErr = RGB(0x9b, 0x00, 0x00);
constexpr COLORREF kOff = RGB(0x80, 0x80, 0x80);
constexpr COLORREF kBar = RGB(0x0a, 0x24, 0x6a);
constexpr COLORREF kDim = RGB(0x44, 0x44, 0x44);

enum Tag : WPARAM {
    TagStatus = 1,
    TagLog,
    TagAction,          // show the error, if any; poll again
    TagSettingsConfig,  // high word: tab
    TagSettingsSaved,
    TagNewLineConfig,
    TagNewLinePorts,
    TagEditLineConfig,
    TagEditLinePorts,
    TagLineSaved,
    TagRemove,
    TagRestart,
    TagStop,
    TagConsolePorts,
};

WPARAM MakeTag(Tag tag, int extra = 0) {
    return static_cast<WPARAM>(tag) | (static_cast<WPARAM>(extra) << 16);
}

COLORREF KindColor(const std::string& kind) {
    if (kind == "up") return kUp;
    if (kind == "neg") return kNeg;
    if (kind == "err") return kErr;
    if (kind == "off") return kOff;
    return kIdle;
}

const wchar_t* const kColumns[] = {L"#",        L"COM port",  L"SIP ext.",    L"State",
                                   L"Calling party", L"Modulation", L"TX/RX bps", L"Duration",
                                   L"Bytes TX/RX", L"Retr.",   L"Err",         L"Driver binding"};
const int kColumnWidths[] = {28, 64, 58, 92, 96, 74, 92, 66, 112, 40, 36, 230};
constexpr int kColumnCount = 12;

struct Row {
    std::string id;
    std::string kind;
    std::wstring cells[kColumnCount];
};

struct Frame {
    RECT rc{};
    std::wstring title;
    bool visible = true;
};

class MainWindow {
public:
    bool Create(HINSTANCE instance, int show);
    HWND Handle() const { return hwnd_; }

private:
    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT OnMessage(UINT msg, WPARAM wp, LPARAM lp);

    void CreateChildren();
    void CreateToolbar();
    void Layout();
    void Paint(HDC dc);
    void PaintFrame(HDC dc, const Frame& frame);
    void PaintStats(HDC dc, const RECT& rc);
    void PaintCarriers(HDC dc, const RECT& rc);
    LRESULT OnNotify(NMHDR* hdr);
    LRESULT OnListCustomDraw(NMLVCUSTOMDRAW* cd);
    void OnDrawItem(DRAWITEMSTRUCT* di);
    void OnCommand(int id);
    void OnInitMenu(HMENU menu);
    void OnResponse(WPARAM tag, Response* r);
    void OnConnection(ConnectionInfo* info);

    void Poll();
    void ApplyStatus(const Json& status);
    void ApplyLog(const Json& result);
    void RebuildRows();
    void UpdateCommands();
    void UpdateStatusBar();
    void UpdateTitle();
    void SetHint(const std::wstring& hint);

    const Json* SelectedLine() const;
    std::string SelectedId() const { return selectedId_; }
    void Action(const std::string& cmd, const Json& args, const std::wstring& doing);
    void ExportStatistics();
    void ExportLog();
    void StartService();
    void InstallService();
    void OpenLineDialog(bool isNew, const Json& config, const Json& ports);

    void LoadSettings();
    void SaveSettings();
    int Scale(int px) const { return MulDiv(px, dpi_, 96); }

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND toolbar_ = nullptr;
    HWND filterLabel_ = nullptr;
    HWND filter_ = nullptr;
    HWND toolbarInfo_ = nullptr;
    HWND list_ = nullptr;
    HWND logList_ = nullptr;
    HWND statusBar_ = nullptr;
    HFONT font_ = nullptr;
    HFONT bold_ = nullptr;
    HFONT mono_ = nullptr;
    int dpi_ = 96;

    Frame linesFrame_, statsFrame_, carrierFrame_, logFrame_;
    RECT statsArea_{}, carrierArea_{};

    ControlClient client_;
    std::string host_ = "127.0.0.1";
    int port_ = 7880;
    std::string password_;
    bool connected_ = false;
    bool paused_ = false;       // File > Disconnect: no polling until Connect
    bool promptOpen_ = false;
    std::wstring connectionError_;
    Json hello_;
    Json status_;
    std::deque<LogLine> log_;   // newest first
    unsigned long long logSeq_ = 0;

    std::vector<Row> rows_;
    std::string selectedId_;
    std::string filterKind_ = "all";
    std::wstring hint_ = L"Ready";
    Json pendingConfig_;
    int modal_ = 0;

    bool showToolbar_ = true;
    bool showStatusBar_ = true;
    bool showLog_ = true;
};

// ------------------------------------------------------------ creation

bool MainWindow::Create(HINSTANCE instance, int show) {
    instance_ = instance;
    LoadSettings();

    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // painted in WM_PAINT, double-buffered
    wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MAINMENU);
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    HDC screen = GetDC(nullptr);
    dpi_ = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);

    hwnd_ = CreateWindowExW(0, kClassName, L"VoIPModem Manager", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                            CW_USEDEFAULT, Scale(1024), Scale(768), nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) return false;

    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        WINDOWPLACEMENT wp{};
        DWORD size = sizeof wp;
        if (RegQueryValueExW(key, L"Placement", nullptr, nullptr, reinterpret_cast<BYTE*>(&wp), &size) == ERROR_SUCCESS &&
            size == sizeof wp) {
            wp.length = sizeof wp;
            if (wp.showCmd == SW_SHOWMINIMIZED) wp.showCmd = SW_SHOWNORMAL;
            SetWindowPlacement(hwnd_, &wp);
            show = wp.showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : show;
        }
        RegCloseKey(key);
    }
    ShowWindow(hwnd_, show);
    UpdateWindow(hwnd_);

    client_.SetTarget(host_, port_, password_);
    Poll();
    SetTimer(hwnd_, kPollTimer, kPollMs, nullptr);
    return true;
}

void MainWindow::LoadSettings() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    wchar_t host[256];
    DWORD size = sizeof host - sizeof(wchar_t);
    DWORD type = 0;
    if (RegQueryValueExW(key, L"Host", nullptr, &type, reinterpret_cast<BYTE*>(host), &size) == ERROR_SUCCESS &&
        type == REG_SZ) {
        host[size / sizeof(wchar_t)] = 0;
        host_ = Narrow(host);
    }
    auto readDword = [&](const wchar_t* name, DWORD fallback) {
        DWORD value = 0, len = sizeof value;
        return RegQueryValueExW(key, name, nullptr, nullptr, reinterpret_cast<BYTE*>(&value), &len) == ERROR_SUCCESS
                   ? value
                   : fallback;
    };
    port_ = static_cast<int>(readDword(L"Port", 7880));
    showToolbar_ = readDword(L"Toolbar", 1) != 0;
    showStatusBar_ = readDword(L"StatusBar", 1) != 0;
    showLog_ = readDword(L"LogPane", 1) != 0;
    RegCloseKey(key);
}

void MainWindow::SaveSettings() {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return;
    }
    const std::wstring host = Widen(host_);
    RegSetValueExW(key, L"Host", 0, REG_SZ, reinterpret_cast<const BYTE*>(host.c_str()),
                   static_cast<DWORD>((host.size() + 1) * sizeof(wchar_t)));
    auto writeDword = [&](const wchar_t* name, DWORD value) {
        RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof value);
    };
    writeDword(L"Port", static_cast<DWORD>(port_));
    writeDword(L"Toolbar", showToolbar_);
    writeDword(L"StatusBar", showStatusBar_);
    writeDword(L"LogPane", showLog_);
    WINDOWPLACEMENT wp{};
    wp.length = sizeof wp;
    if (GetWindowPlacement(hwnd_, &wp)) {
        RegSetValueExW(key, L"Placement", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&wp), sizeof wp);
    }
    RegCloseKey(key);
}

void MainWindow::CreateChildren() {
    const int pt8 = -MulDiv(8, dpi_, 72);
    font_ = CreateFontW(pt8, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Tahoma");
    bold_ = CreateFontW(pt8, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Tahoma");
    mono_ = CreateFontW(pt8, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, FIXED_PITCH | FF_MODERN,
                        L"Courier New");

    CreateToolbar();

    list_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS |
                                LVS_OWNERDATA,
                            0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);
    SendMessageW(list_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP);
    for (int i = 0; i < kColumnCount; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.fmt = (i == 0 || i == 9 || i == 10) ? LVCFMT_RIGHT : LVCFMT_LEFT;
        col.pszText = const_cast<wchar_t*>(kColumns[i]);
        col.cx = Scale(kColumnWidths[i]);
        ListView_InsertColumn(list_, i, &col);
    }

    logList_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_NODATA |
                                   LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
                               0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);
    SendMessageW(logList_, WM_SETFONT, reinterpret_cast<WPARAM>(mono_), FALSE);

    statusBar_ = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd_,
                                 nullptr, instance_, nullptr);
    SendMessageW(statusBar_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);

    linesFrame_.title = L"Registered modem lines";
    statsFrame_.title = L"Inbound call statistics";
    carrierFrame_.title = L"Negotiation & carrier";
    logFrame_.title = L"Event log";
}

void MainWindow::CreateToolbar() {
    toolbar_ = CreateWindowExW(0, TOOLBARCLASSNAMEW, L"",
                               WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST | CCS_NODIVIDER | CCS_NORESIZE |
                                   CCS_NOPARENTALIGN,
                               0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);
    SendMessageW(toolbar_, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(toolbar_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    SendMessageW(toolbar_, TB_SETBITMAPSIZE, 0, MAKELONG(0, 0));

    struct Item {
        int id;
        const wchar_t* text;
    };
    const Item items[] = {{IDM_ANSWER, L"Answer now"},   {IDM_HANGUP, L"Hang up"},
                          {IDM_RESET, L"Reset line"},     {IDM_NEW_LINE, L"New line…"},
                          {IDM_REREGISTER, L"Re-register"}, {IDM_RELOAD, L"Reload config"},
                          {IDM_SETTINGS, L"Settings…"}};
    std::vector<TBBUTTON> buttons;
    for (const Item& item : items) {
        TBBUTTON b{};
        b.iBitmap = I_IMAGENONE;
        b.idCommand = item.id;
        b.fsState = TBSTATE_ENABLED;
        b.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
        b.iString = reinterpret_cast<INT_PTR>(item.text);
        buttons.push_back(b);
    }
    TBBUTTON sep{};
    sep.iBitmap = Scale(8);
    sep.fsStyle = BTNS_SEP;
    buttons.push_back(sep);
    SendMessageW(toolbar_, TB_ADDBUTTONSW, buttons.size(), reinterpret_cast<LPARAM>(buttons.data()));
    SendMessageW(toolbar_, TB_AUTOSIZE, 0, 0);

    filterLabel_ = CreateWindowExW(0, L"STATIC", L"Show:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, 0, 0, 0, 0, toolbar_,
                                   nullptr, instance_, nullptr);
    SendMessageW(filterLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    filter_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                              0, 0, 0, 0, toolbar_, nullptr, instance_, nullptr);
    SendMessageW(filter_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    for (const wchar_t* text : {L"All lines", L"On a call", L"Waiting for calls", L"Faulted"}) {
        SendMessageW(filter_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    }
    SendMessageW(filter_, CB_SETCURSEL, 0, 0);
    toolbarInfo_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE, 0, 0, 0, 0,
                                   toolbar_, nullptr, instance_, nullptr);
    SendMessageW(toolbarInfo_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
}

// ---------------------------------------------------------------- layout

void MainWindow::Layout() {
    RECT client;
    GetClientRect(hwnd_, &client);
    const int w = client.right;
    const int pad = Scale(4);

    int top = 0;
    if (showToolbar_) {
        const int tbHeight = Scale(29);
        MoveWindow(toolbar_, 0, 0, w, tbHeight, TRUE);
        ShowWindow(toolbar_, SW_SHOW);
        // "Show:" and the filter after the last button; the info text at the right.
        RECT last{};
        const int count = static_cast<int>(SendMessageW(toolbar_, TB_BUTTONCOUNT, 0, 0));
        SendMessageW(toolbar_, TB_GETITEMRECT, count - 1, reinterpret_cast<LPARAM>(&last));
        int x = last.right + Scale(2);
        MoveWindow(filterLabel_, x, 0, Scale(34), tbHeight, TRUE);
        x += Scale(36);
        MoveWindow(filter_, x, (tbHeight - Scale(20)) / 2, Scale(120), Scale(200), TRUE);
        x += Scale(126);
        MoveWindow(toolbarInfo_, x, 0, max(0, w - x - Scale(6)), tbHeight, TRUE);
        top = tbHeight;
    } else {
        ShowWindow(toolbar_, SW_HIDE);
    }

    int bottom = client.bottom;
    if (showStatusBar_) {
        SendMessageW(statusBar_, WM_SIZE, 0, 0);
        RECT sb;
        GetWindowRect(statusBar_, &sb);
        bottom -= sb.bottom - sb.top;
        ShowWindow(statusBar_, SW_SHOW);
        const int parts[] = {max(Scale(100), w - Scale(214 + 132 + 122 + 86 + 148)),
                             w - Scale(132 + 122 + 86 + 148),
                             w - Scale(122 + 86 + 148),
                             w - Scale(86 + 148),
                             w - Scale(148),
                             -1};
        SendMessageW(statusBar_, SB_SETPARTS, 6, reinterpret_cast<LPARAM>(parts));
    } else {
        ShowWindow(statusBar_, SW_HIDE);
    }

    const int bottomRow = Scale(242);
    const int y0 = top + pad;
    const int y1 = bottom - pad;
    const int listBottom = max(y0 + Scale(80), y1 - bottomRow - pad);

    linesFrame_.rc = {pad, y0, w - pad, listBottom};
    const int legend = Scale(14);
    MoveWindow(list_, linesFrame_.rc.left + Scale(7), linesFrame_.rc.top + legend + Scale(2),
               linesFrame_.rc.right - linesFrame_.rc.left - Scale(14),
               linesFrame_.rc.bottom - linesFrame_.rc.top - legend - Scale(9), TRUE);
    ListView_SetColumnWidth(list_, kColumnCount - 1, LVSCW_AUTOSIZE_USEHEADER); // the last column takes the rest

    const int by = listBottom + pad;
    statsFrame_.rc = {pad, by, pad + Scale(236), y1};
    const int carrierRight = showLog_ ? statsFrame_.rc.right + pad + Scale(296) : w - pad;
    carrierFrame_.rc = {statsFrame_.rc.right + pad, by, carrierRight, y1};
    logFrame_.rc = {carrierRight + pad, by, w - pad, y1};
    logFrame_.visible = showLog_ && logFrame_.rc.right - logFrame_.rc.left > Scale(60);
    statsArea_ = {statsFrame_.rc.left + Scale(8), statsFrame_.rc.top + legend + Scale(4), statsFrame_.rc.right - Scale(8),
                  statsFrame_.rc.bottom - Scale(6)};
    carrierArea_ = {carrierFrame_.rc.left + Scale(8), carrierFrame_.rc.top + legend + Scale(4),
                    carrierFrame_.rc.right - Scale(8), carrierFrame_.rc.bottom - Scale(6)};
    if (logFrame_.visible) {
        MoveWindow(logList_, logFrame_.rc.left + Scale(7), logFrame_.rc.top + legend + Scale(2),
                   logFrame_.rc.right - logFrame_.rc.left - Scale(14),
                   logFrame_.rc.bottom - logFrame_.rc.top - legend - Scale(9), TRUE);
        ShowWindow(logList_, SW_SHOW);
    } else {
        ShowWindow(logList_, SW_HIDE);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------------------------------------------------------------- paint

void MainWindow::PaintFrame(HDC dc, const Frame& frame) {
    if (!frame.visible) return;
    RECT edge = frame.rc;
    edge.top += Scale(6);
    DrawEdge(dc, &edge, EDGE_ETCHED, BF_RECT);
    SelectObject(dc, bold_);
    SIZE size;
    GetTextExtentPoint32W(dc, frame.title.c_str(), static_cast<int>(frame.title.size()), &size);
    RECT text{frame.rc.left + Scale(7), frame.rc.top, frame.rc.left + Scale(7) + size.cx + Scale(8),
              frame.rc.top + size.cy};
    FillRect(dc, &text, GetSysColorBrush(COLOR_BTNFACE));
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    TextOutW(dc, text.left + Scale(4), text.top, frame.title.c_str(), static_cast<int>(frame.title.size()));
}

void MainWindow::PaintStats(HDC dc, const RECT& rc) {
    const Json& s = status_.contains("stats") ? status_["stats"] : Json::object();
    const Json& svc = status_.contains("service") ? status_["service"] : Json::object();
    const size_t lines = status_.contains("lines") ? status_["lines"].size() : 0;
    const bool have = connected_ && status_.is_object() && !status_.empty();
    auto n = [&](const char* key) { return have ? FormatCount(JInt(s, key)) : std::wstring(L"—"); };
    std::wstring configLoaded = Widen(JStr(svc, "configLoadedAt"));
    if (configLoaded.size() >= 16) configLoaded = configLoaded.substr(0, 16);
    const std::pair<std::wstring, std::wstring> rows[] = {
        {L"Calls answered", n("callsAnswered")},
        {L"Trained successfully", n("trained")},
        {L"Failed to train", n("failedToTrain")},
        {L"Rejected (line busy)", n("rejectedBusy")},
        {L"Caller hung up early", n("hungUpEarly")},
        {L"Avg. call length", have && JInt(s, "completedCalls") > 0 ? FormatDuration(JInt(s, "averageCallSeconds"))
                                                                    : L"—"},
        {L"Longest call", have && JInt(s, "completedCalls") > 0 ? FormatDuration(JInt(s, "longestCallSeconds"))
                                                                : L"—"},
        {L"Peak lines in use", have ? std::to_wstring(JInt(s, "peakBusyLines")) + L" of " + std::to_wstring(lines)
                                    : L"—"},
        {L"Bytes in / out", have ? FormatBytes(static_cast<std::uint64_t>(JNum(s, "bytesIn"))) + L" / " +
                                       FormatBytes(static_cast<std::uint64_t>(JNum(s, "bytesOut")))
                                 : L"—"},
        {L"AT commands parsed", n("atCommands")},
        {L"Service uptime", have ? FormatDuration(JInt(svc, "uptimeSeconds")) : L"—"},
        {L"Config loaded", have ? configLoaded : L"—"},
    };
    const int rowHeight = Scale(17);
    int y = rc.top;
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    for (const auto& row : rows) {
        if (y + rowHeight > rc.bottom + Scale(4)) break;
        RECT r{rc.left, y, rc.right, y + rowHeight};
        SelectObject(dc, font_);
        DrawTextW(dc, row.first.c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, bold_);
        DrawTextW(dc, row.second.c_str(), -1, &r, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        y += rowHeight;
    }
}

void MainWindow::PaintCarriers(HDC dc, const RECT& rc) {
    const bool have = connected_ && status_.is_object() && status_.contains("carriers");
    const Json carriers = have ? status_["carriers"] : Json::array();
    long long total = 0;
    for (const Json& c : carriers) total += JInt(c, "count");

    SelectObject(dc, font_);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    const int barRow = Scale(18);
    int y = rc.top;
    if (total == 0) {
        RECT r{rc.left, y, rc.right, y + barRow * 5};
        SetTextColor(dc, kDim);
        DrawTextW(dc, have ? L"No modem connections since the service started." : L"—", -1, &r,
                  DT_LEFT | DT_TOP | DT_WORDBREAK);
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        y += barRow * 5;
    } else {
        int shown = 0;
        for (const Json& c : carriers) {
            if (shown++ == 5) break;
            const long long count = JInt(c, "count");
            const int percent = static_cast<int>((count * 100 + total / 2) / total);
            RECT label{rc.left, y, rc.left + Scale(104), y + barRow};
            DrawTextW(dc, Widen(JStr(c, "mode")).c_str(), -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            RECT box{label.right + Scale(5), y + (barRow - Scale(14)) / 2, rc.right - Scale(36), y + (barRow + Scale(14)) / 2};
            FillRect(dc, &box, GetSysColorBrush(COLOR_WINDOW));
            DrawEdge(dc, &box, BDR_SUNKENOUTER, BF_RECT);
            RECT bar{box.left + 2, box.top + 2, box.left + 2 + (box.right - box.left - 4) * percent / 100, box.bottom - 2};
            HBRUSH brush = CreateSolidBrush(kBar);
            FillRect(dc, &bar, brush);
            DeleteObject(brush);
            RECT pct{rc.right - Scale(32), y, rc.right, y + barRow};
            const std::wstring p = std::to_wstring(percent) + L"%";
            DrawTextW(dc, p.c_str(), -1, &pct, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            y += barRow;
        }
        y = rc.top + barRow * 5;
    }

    y += Scale(4);
    RECT line{rc.left, y, rc.right, y + 2};
    DrawEdge(dc, &line, EDGE_ETCHED, BF_TOP);
    y += Scale(6);

    const Json& s = have ? status_["stats"] : Json::object();
    const Json& media = have ? status_["media"] : Json::object();
    const Json& def = have ? status_["defaults"] : Json::object();
    wchar_t jitter[64] = L"—";
    if (JNum(media, "jitterMeanMs", -1) >= 0) {
        swprintf(jitter, 64, L"%.0f ms / %.0f ms", JNum(media, "jitterMeanMs"), JNum(media, "jitterMaxMs"));
    }
    wchar_t loss[32] = L"—";
    if (JNum(media, "lossPercent", -1) >= 0) {
        swprintf(loss, 32, L"%.2f %%", JNum(media, "lossPercent"));
    }
    std::wstring ec = L"—";
    if (have) {
        ec = !JBool(def, "errorControl", true) ? L"off"
                                               : (JBool(def, "compression", true) ? L"V.42 / V.42bis, V.44" : L"V.42");
    }
    const std::wstring codec = JStr(media, "preferredCodec") == "PCMA" ? L"G.711 A-law" : L"G.711 µ-law";
    const std::pair<std::wstring, std::wstring> rows[] = {
        {L"Retrains / renegotiations",
         have ? std::to_wstring(JInt(s, "retrains")) + L" / " + std::to_wstring(JInt(s, "renegotiations")) : L"—"},
        {L"Error correction", ec},
        {L"Softmodem engine", have ? Widen(JStr(hello_, "engine", "libv90modem")) : L"—"},
        {L"RTP codec / ptime", have ? codec + L" / " + std::to_wstring(JInt(media, "ptimeMs", 20)) + L" ms" : L"—"},
        {L"Jitter (avg / max)", jitter},
        {L"Packet loss", loss},
    };
    const int rowHeight = Scale(16);
    for (const auto& row : rows) {
        if (y + rowHeight > rc.bottom + Scale(4)) break;
        RECT r{rc.left, y, rc.right, y + rowHeight};
        SelectObject(dc, font_);
        DrawTextW(dc, row.first.c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, bold_);
        DrawTextW(dc, row.second.c_str(), -1, &r, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        y += rowHeight;
    }
}

void MainWindow::Paint(HDC target) {
    RECT client;
    GetClientRect(hwnd_, &client);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    HGDIOBJ oldFont = SelectObject(dc, font_);
    FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));
    SetBkMode(dc, TRANSPARENT);

    if (showToolbar_) {
        RECT tb;
        GetWindowRect(toolbar_, &tb);
        RECT line{0, tb.bottom - tb.top, client.right, tb.bottom - tb.top + 2};
        DrawEdge(dc, &line, EDGE_ETCHED, BF_TOP);
    }
    PaintFrame(dc, linesFrame_);
    PaintFrame(dc, statsFrame_);
    PaintFrame(dc, carrierFrame_);
    PaintFrame(dc, logFrame_);
    PaintStats(dc, statsArea_);
    PaintCarriers(dc, carrierArea_);

    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// --------------------------------------------------------------- status

void MainWindow::Poll() {
    if (paused_ || promptOpen_) return;
    if (client_.Pending() > 0) return; // still waiting on the last round
    client_.Request(hwnd_, "status", Json::object(), MakeTag(TagStatus));
    client_.Request(hwnd_, "log", {{"after", logSeq_}, {"max", 500}}, MakeTag(TagLog));
}

void MainWindow::ApplyStatus(const Json& status) {
    status_ = status;
    RebuildRows();

    const Json& lines = status_["lines"];
    int busy = 0, waiting = 0;
    for (const Json& l : lines) {
        const std::string kind = JStr(l, "kind");
        busy += (kind == "up" || kind == "neg") ? 1 : 0;
        waiting += kind == "idle" ? 1 : 0;
    }
    linesFrame_.title = L"Registered modem lines — " + std::to_wstring(lines.size()) + L" configured, " +
                        std::to_wstring(busy) + L" on a call, " + std::to_wstring(waiting) + L" waiting";
    std::wstring logFile = Widen(JStr(status_["service"], "logFile"));
    const auto slash = logFile.find_last_of(L"\\/");
    if (slash != std::wstring::npos) logFile = logFile.substr(slash + 1);
    logFrame_.title = L"Event log" + (logFile.empty() ? std::wstring() : L" — " + logFile);

    const long long rings = JInt(status_["defaults"], "autoAnswerRings");
    std::wstring info = rings > 0 ? L"Auto-answer after " + std::to_wstring(rings) + L" ring(s)" : L"Auto-answer off";
    info += L" · Poll " + std::to_wstring(kPollMs) + L" ms";
    if (JBool(status_["service"], "restartRequired")) {
        info = L"Restart the service to apply saved settings · " + info;
    }
    SetWindowTextW(toolbarInfo_, info.c_str());

    UpdateTitle();
    UpdateStatusBar();
    UpdateCommands();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::RebuildRows() {
    rows_.clear();
    const Json lines = status_.contains("lines") ? status_["lines"] : Json::array();
    int index = 0;
    for (const Json& l : lines) {
        ++index;
        const std::string kind = JStr(l, "kind");
        const bool show = filterKind_ == "all" || (filterKind_ == "busy" && (kind == "up" || kind == "neg")) ||
                          (filterKind_ == "idle" && kind == "idle") ||
                          (filterKind_ == "err" && (kind == "err" || kind == "off"));
        if (!show) continue;
        Row row;
        row.id = JStr(l, "id");
        row.kind = kind;
        const std::wstring dash = L"—";
        const bool inCall = JInt(l, "callSeconds", -1) >= 0;
        row.cells[0] = std::to_wstring(index);
        row.cells[1] = Widen(JStr(l, "comPort"));
        row.cells[2] = Widen(JStr(l, "extension"));
        row.cells[3] = Widen(JStr(l, "state"));
        if (JBool(l, "pendingChanges")) row.cells[3] += L" *";
        row.cells[4] = JStr(l, "remoteParty").empty() ? dash : Widen(JStr(l, "remoteParty"));
        row.cells[5] = JStr(l, "modulation").empty() ? dash : Widen(JStr(l, "modulation"));
        row.cells[6] = JInt(l, "txBps") > 0 ? std::to_wstring(JInt(l, "txBps")) + L"/" + std::to_wstring(JInt(l, "rxBps")) : dash;
        row.cells[7] = inCall ? FormatDuration(JInt(l, "callSeconds")) : dash;
        row.cells[8] = inCall ? FormatBytes(static_cast<std::uint64_t>(JNum(l, "bytesTx"))) + L" / " +
                                    FormatBytes(static_cast<std::uint64_t>(JNum(l, "bytesRx")))
                              : dash;
        row.cells[9] = std::to_wstring(JInt(l, "retrains"));
        row.cells[10] = std::to_wstring(JInt(l, "errors"));
        row.cells[11] = Widen(JStr(l, "driver"));
        rows_.push_back(std::move(row));
    }

    ListView_SetItemCountEx(list_, static_cast<int>(rows_.size()), LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    int selected = -1;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].id == selectedId_) selected = static_cast<int>(i);
    }
    const int current = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
    if (selected != current) {
        ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        if (selected >= 0) {
            ListView_SetItemState(list_, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
    }
    if (selected < 0 && !selectedId_.empty()) {
        selectedId_.clear();
    }
    InvalidateRect(list_, nullptr, FALSE);
}

const Json* MainWindow::SelectedLine() const {
    if (selectedId_.empty() || !status_.contains("lines")) return nullptr;
    for (const Json& l : status_["lines"]) {
        if (JStr(l, "id") == selectedId_) return &l;
    }
    return nullptr;
}

void MainWindow::UpdateCommands() {
    const Json* sel = SelectedLine();
    SendMessageW(toolbar_, TB_ENABLEBUTTON, IDM_ANSWER, sel != nullptr && JBool(*sel, "canAnswer"));
    SendMessageW(toolbar_, TB_ENABLEBUTTON, IDM_HANGUP, sel != nullptr && JBool(*sel, "canHangUp"));
    SendMessageW(toolbar_, TB_ENABLEBUTTON, IDM_RESET, sel != nullptr);
    for (int id : {IDM_NEW_LINE, IDM_REREGISTER, IDM_RELOAD, IDM_SETTINGS}) {
        SendMessageW(toolbar_, TB_ENABLEBUTTON, id, connected_);
    }
}

void MainWindow::UpdateTitle() {
    std::wstring title = L"VoIPModem Manager";
    if (connected_ && status_.contains("service")) {
        std::wstring ini = Widen(JStr(status_["service"], "configPath"));
        const auto slash = ini.find_last_of(L"\\/");
        if (slash != std::wstring::npos) ini = ini.substr(slash + 1);
        title += L" — " + ini + L" [" + std::to_wstring(status_["lines"].size()) + L" lines] — " +
                 Widen(JStr(status_["service"], "host"));
    } else {
        title += L" — not connected";
    }
    wchar_t current[512];
    GetWindowTextW(hwnd_, current, 512);
    if (title != current) SetWindowTextW(hwnd_, title.c_str());

    HMENU menu = GetMenu(hwnd_);
    std::wstring label;
    if (connected_) {
        label = L"Service on " + Widen(JStr(hello_, "host")) +
                (JBool(hello_, "runningAsService") ? L" (Windows service)" : L" (console)");
    } else {
        label = paused_ ? L"Disconnected" : L"Not connected";
    }
    MENUITEMINFOW mii{sizeof mii};
    mii.fMask = MIIM_STRING;
    mii.dwTypeData = const_cast<wchar_t*>(label.c_str());
    SetMenuItemInfoW(menu, IDM_STATUS_LABEL, FALSE, &mii);
    DrawMenuBar(hwnd_);
}

void MainWindow::UpdateStatusBar() {
    SendMessageW(statusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(hint_.c_str()));
    SendMessageW(statusBar_, SB_SETTEXTW, 1 | SBT_OWNERDRAW, 0); // SIP: drawn in WM_DRAWITEM
    std::wstring answering = L"Answering: —", rtp = L"RTP: —", cpu = L"CPU: —";
    if (connected_ && status_.contains("lines")) {
        int busy = 0;
        for (const Json& l : status_["lines"]) {
            const std::string kind = JStr(l, "kind");
            busy += (kind == "up" || kind == "neg") ? 1 : 0;
        }
        answering = L"Answering: " + std::to_wstring(busy) + L" / " + std::to_wstring(status_["lines"].size());
        wchar_t buf[64];
        swprintf(buf, 64, L"RTP: %.0f kbit/s", JNum(status_["media"], "rtpKbps"));
        rtp = buf;
        swprintf(buf, 64, L"CPU: %.0f %%", JNum(status_["service"], "cpuPercent"));
        cpu = buf;
    }
    SendMessageW(statusBar_, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(answering.c_str()));
    SendMessageW(statusBar_, SB_SETTEXTW, 3, reinterpret_cast<LPARAM>(rtp.c_str()));
    SendMessageW(statusBar_, SB_SETTEXTW, 4, reinterpret_cast<LPARAM>(cpu.c_str()));
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t date[64], time[32];
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &now, nullptr, date, 64);
    GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &now, nullptr, time, 32);
    const std::wstring clock = std::wstring(date) + L" " + time;
    SendMessageW(statusBar_, SB_SETTEXTW, 5, reinterpret_cast<LPARAM>(clock.c_str()));
}

void MainWindow::SetHint(const std::wstring& hint) {
    hint_ = hint;
    SendMessageW(statusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(hint_.c_str()));
}

void MainWindow::ApplyLog(const Json& result) {
    const unsigned long long last = static_cast<unsigned long long>(JNum(result, "last"));
    if (last < logSeq_) {
        // The service restarted: its log numbering did too.
        log_.clear();
        logSeq_ = 0;
        return;
    }
    const int top = static_cast<int>(SendMessageW(logList_, LB_GETTOPINDEX, 0, 0));
    bool added = false;
    for (const Json& e : result["entries"]) {
        LogLine line;
        line.seq = static_cast<unsigned long long>(JNum(e, "seq"));
        if (line.seq <= logSeq_) continue;
        const std::string time = JStr(e, "time");
        line.time = Widen(time.size() >= 19 ? time.substr(11, 8) : time);
        line.level = Widen(JStr(e, "level"));
        line.message = Widen(JStr(e, "message"));
        logSeq_ = line.seq;
        log_.push_front(std::move(line));
        added = true;
    }
    while (log_.size() > kMaxLogLines) log_.pop_back();
    if (added) {
        SendMessageW(logList_, LB_SETCOUNT, log_.size(), 0);
        if (top > 0) SendMessageW(logList_, LB_SETTOPINDEX, static_cast<WPARAM>(top), 0); // reading back: stay put
        InvalidateRect(logList_, nullptr, FALSE);
    }
}

// -------------------------------------------------------------- drawing

LRESULT MainWindow::OnListCustomDraw(NMLVCUSTOMDRAW* cd) {
    switch (cd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT:
            return CDRF_NOTIFYSUBITEMDRAW;
        case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
            const size_t row = static_cast<size_t>(cd->nmcd.dwItemSpec);
            const bool selected = ListView_GetItemState(list_, static_cast<int>(row), LVIS_SELECTED) != 0;
            if (cd->iSubItem == 3 && row < rows_.size()) {
                SelectObject(cd->nmcd.hdc, bold_);
                if (!selected) cd->clrText = KindColor(rows_[row].kind);
                return CDRF_NEWFONT;
            }
            SelectObject(cd->nmcd.hdc, font_);
            if (!selected) cd->clrText = GetSysColor(COLOR_WINDOWTEXT);
            return CDRF_NEWFONT;
        }
    }
    return CDRF_DODEFAULT;
}

void MainWindow::OnDrawItem(DRAWITEMSTRUCT* di) {
    if (di->hwndItem == statusBar_) {
        // The SIP part: a coloured square, then the registration summary.
        std::wstring text = L"SIP: —";
        COLORREF square = kOff;
        if (connected_ && status_.contains("sip")) {
            const Json& sip = status_["sip"];
            const long long lines = JInt(sip, "lines");
            const long long reg = JInt(sip, "registered");
            const std::string server = JStr(sip, "server");
            if (!JBool(sip, "enabled")) {
                text = L"SIP: off ([SIP] LocalPort)";
            } else if (lines == 0) {
                text = L"SIP: no line has a server";
                square = kErr;
            } else {
                text = (reg == lines ? L"SIP: registered" : L"SIP: " + std::to_wstring(reg) + L" of " + std::to_wstring(lines) + L" registered") +
                       (server.empty() ? L"" : L" (" + Widen(server) + L")");
                square = reg == lines ? kUp : reg > 0 ? kNeg : kErr;
            }
        }
        RECT rc = di->rcItem;
        const int s = Scale(7);
        RECT box{rc.left + Scale(3), (rc.top + rc.bottom - s) / 2, rc.left + Scale(3) + s, (rc.top + rc.bottom + s) / 2};
        HBRUSH brush = CreateSolidBrush(square);
        FillRect(di->hDC, &box, brush);
        DeleteObject(brush);
        FrameRect(di->hDC, &box, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        rc.left = box.right + Scale(5);
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, GetSysColor(COLOR_BTNTEXT));
        DrawTextW(di->hDC, text.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }
    if (di->hwndItem == logList_) {
        if (di->itemID == static_cast<UINT>(-1)) return;
        FillRect(di->hDC, &di->rcItem, GetSysColorBrush(COLOR_WINDOW));
        if (di->itemID >= log_.size()) return;
        const LogLine& line = log_[di->itemID];
        COLORREF color = RGB(0, 0, 0);
        if (line.level == L"ERROR") color = kErr;
        else if (line.level == L"WARN") color = kNeg;
        else if (line.message.find(L"CONNECTED") != std::wstring::npos ||
                 line.message.find(L"registration result: 200") != std::wstring::npos ||
                 line.message.find(L"V.42 LAPM connected") != std::wstring::npos) color = kUp;
        else if (line.message.find(L"RING") != std::wstring::npos || line.message.find(L"nswering") != std::wstring::npos) color = kNeg;
        SetBkMode(di->hDC, TRANSPARENT);
        RECT rc = di->rcItem;
        rc.left += Scale(3);
        SetTextColor(di->hDC, kDim);
        DrawTextW(di->hDC, line.time.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SIZE size;
        GetTextExtentPoint32W(di->hDC, L"00:00:00 ", 9, &size);
        rc.left += size.cx;
        SetTextColor(di->hDC, color);
        DrawTextW(di->hDC, line.message.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
}

LRESULT MainWindow::OnNotify(NMHDR* hdr) {
    if (hdr->hwndFrom == list_) {
        switch (hdr->code) {
            case LVN_GETDISPINFOW: {
                auto* di = reinterpret_cast<NMLVDISPINFOW*>(hdr);
                const size_t row = static_cast<size_t>(di->item.iItem);
                if ((di->item.mask & LVIF_TEXT) && row < rows_.size() && di->item.iSubItem < kColumnCount) {
                    lstrcpynW(di->item.pszText, rows_[row].cells[di->item.iSubItem].c_str(), di->item.cchTextMax);
                }
                return 0;
            }
            case LVN_ITEMCHANGED: {
                auto* nm = reinterpret_cast<NMLISTVIEW*>(hdr);
                if ((nm->uChanged & LVIF_STATE) && (nm->uNewState & LVIS_SELECTED) && nm->iItem >= 0 &&
                    static_cast<size_t>(nm->iItem) < rows_.size()) {
                    selectedId_ = rows_[static_cast<size_t>(nm->iItem)].id;
                    if (const Json* l = SelectedLine()) {
                        std::wstring hint = L"Line " + Widen(JStr(*l, "comPort")) + L" — ext " +
                                            Widen(JStr(*l, "extension")) + L" — " + Widen(JStr(*l, "state"));
                        if (!JStr(*l, "fault").empty()) hint += L": " + Widen(JStr(*l, "fault"));
                        SetHint(hint);
                    }
                } else if ((nm->uChanged & LVIF_STATE) && (nm->uOldState & LVIS_SELECTED) &&
                           !(nm->uNewState & LVIS_SELECTED) && ListView_GetSelectedCount(list_) == 0) {
                    selectedId_.clear(); // clicked on empty space
                }
                UpdateCommands();
                return 0;
            }
            case NM_DBLCLK:
                if (SelectedLine() != nullptr) OnCommand(IDM_PROPERTIES);
                return 0;
            case NM_CUSTOMDRAW:
                return OnListCustomDraw(reinterpret_cast<NMLVCUSTOMDRAW*>(hdr));
        }
    }
    return 0;
}

// -------------------------------------------------------------- commands

void MainWindow::Action(const std::string& cmd, const Json& args, const std::wstring& doing) {
    if (!connected_) {
        SetHint(L"Not connected to the service");
        return;
    }
    SetHint(doing);
    client_.Request(hwnd_, cmd, args, MakeTag(TagAction));
}

void MainWindow::OnCommand(int id) {
    const Json* sel = SelectedLine();
    const std::string selId = sel ? JStr(*sel, "id") : std::string();
    const std::wstring selName = sel ? Widen(JStr(*sel, "comPort")) : std::wstring();
    switch (id) {
        case IDM_CONNECT: {
            std::string host = host_, password = password_;
            int port = port_;
            promptOpen_ = true;
            const bool ok = ShowConnectDialog(hwnd_, host, port, password);
            promptOpen_ = false;
            if (ok) {
                host_ = host;
                port_ = port;
                password_ = password;
                paused_ = false;
                log_.clear();
                logSeq_ = 0;
                SendMessageW(logList_, LB_SETCOUNT, 0, 0);
                client_.SetTarget(host_, port_, password_);
                SaveSettings();
                SetHint(L"Connecting to " + Widen(host_) + L":" + std::to_wstring(port_) + L"…");
                Poll();
            }
            break;
        }
        case IDM_DISCONNECT:
            paused_ = true;
            client_.Disconnect();
            connected_ = false;
            status_ = Json::object();
            RebuildRows();
            UpdateTitle();
            UpdateStatusBar();
            UpdateCommands();
            SetHint(L"Disconnected (File > Connect to service to reconnect)");
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case IDM_RELOAD:
            Action("config.reload", Json::object(), L"Reloading voipmodem.ini…");
            break;
        case IDM_EXPORT_STATS:
            ExportStatistics();
            break;
        case IDM_EXPORT_LOG:
            ExportLog();
            break;
        case IDM_SERVICE_START:
            StartService();
            break;
        case IDM_SERVICE_STOP: {
            const bool busy = std::any_of(status_["lines"].begin(), status_["lines"].end(), [](const Json& l) {
                return JBool(l, "canHangUp");
            });
            std::wstring question = L"Stop the VoIPModem service? Every line stops taking calls.";
            if (busy) question = L"A line is in a call; stopping the service hangs it up. Stop anyway?";
            if (MessageBoxW(hwnd_, question.c_str(), L"Stop service", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                SetHint(L"Stopping the service…");
                client_.Request(hwnd_, "service.stop", Json::object(), MakeTag(TagStop));
            }
            break;
        }
        case IDM_SERVICE_RESTART:
            SetHint(L"Restarting the service…");
            client_.Request(hwnd_, "service.restart", Json::object(), MakeTag(TagRestart));
            break;
        case IDM_SERVICE_INSTALL:
            InstallService();
            break;
        case IDM_EXIT:
            DestroyWindow(hwnd_);
            break;

        case IDM_ANSWER:
            if (sel) Action("line.answer", {{"id", selId}}, L"Answering on " + selName + L"…");
            break;
        case IDM_HANGUP:
            if (sel) Action("line.hangup", {{"id", selId}}, L"Hanging up " + selName + L"…");
            break;
        case IDM_RESET:
            if (sel) {
                if (JBool(*sel, "canHangUp") &&
                    MessageBoxW(hwnd_, (selName + L" is in a call. Reset it anyway (the call is dropped)?").c_str(),
                                L"Reset line", MB_YESNO | MB_ICONQUESTION) != IDYES) {
                    break;
                }
                Action("line.reset", {{"id", selId}}, L"Resetting " + selName + L"…");
            }
            break;
        case IDM_TOGGLE_SERVICE:
            if (sel) {
                const bool inService = JBool(*sel, "inService", true);
                if (inService && JBool(*sel, "canHangUp") &&
                    MessageBoxW(hwnd_, (selName + L" is in a call. Take it out of service and hang up?").c_str(),
                                L"Out of service", MB_YESNO | MB_ICONQUESTION) != IDYES) {
                    break;
                }
                Action("line.service", {{"id", selId}, {"inService", !inService}},
                       (inService ? L"Taking " : L"Putting ") + selName + (inService ? L" out of service" : L" in service"));
            }
            break;
        case IDM_ALL_IN_SERVICE:
            Action("line.service", {{"inService", true}}, L"Putting every line in service…");
            break;
        case IDM_PROPERTIES:
            if (sel && connected_ && modal_ == 0) {
                client_.Request(hwnd_, "config.get", Json::object(), MakeTag(TagEditLineConfig));
                client_.Request(hwnd_, "system.ports", Json::object(), MakeTag(TagEditLinePorts));
            }
            break;

        case IDM_REREGISTER:
            Action("sip.register", {{"register", true}}, L"Re-registering every extension…");
            break;
        case IDM_UNREGISTER:
            if (MessageBoxW(hwnd_, L"Unregister every extension? No line takes calls until you re-register or restart.",
                            L"Unregister", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                Action("sip.register", {{"register", false}}, L"Unregistering every extension…");
            }
            break;
        case IDM_ACCOUNT_SETTINGS:
        case IDM_SETTINGS:
        case IDM_MODEM_DEFAULTS:
            if (connected_ && modal_ == 0) {
                client_.Request(hwnd_, "config.get", Json::object(),
                                MakeTag(TagSettingsConfig, id == IDM_MODEM_DEFAULTS ? 2 : 0));
            }
            break;
        case IDM_SIP_TRACE:
            ++modal_;
            ShowTraceDialog(hwnd_, log_, client_);
            --modal_;
            break;

        case IDM_NEW_LINE:
            if (connected_ && modal_ == 0) {
                client_.Request(hwnd_, "config.get", Json::object(), MakeTag(TagNewLineConfig));
                client_.Request(hwnd_, "system.ports", Json::object(), MakeTag(TagNewLinePorts));
            }
            break;
        case IDM_REMOVE_LINE:
            if (sel && MessageBoxW(hwnd_, (L"Remove line " + selName + L" (extension " + Widen(JStr(*sel, "extension")) +
                                           L") from voipmodem.ini?").c_str(),
                                   L"Remove line", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                SetHint(L"Removing " + selName + L"…");
                client_.Request(hwnd_, "line.remove", {{"id", selId}}, MakeTag(TagRemove));
            }
            break;
        case IDM_SELFTEST:
            if (connected_) {
                ++modal_;
                ShowSelfTestDialog(hwnd_, client_);
                --modal_;
            }
            break;

        case IDM_VIEW_TOOLBAR:
            showToolbar_ = !showToolbar_;
            Layout();
            break;
        case IDM_VIEW_STATUSBAR:
            showStatusBar_ = !showStatusBar_;
            Layout();
            break;
        case IDM_VIEW_LOG:
            showLog_ = !showLog_;
            Layout();
            break;
        case IDM_REFRESH:
            Poll();
            break;

        case IDM_AT_CONSOLE:
            if (connected_) {
                client_.Request(hwnd_, "system.ports", Json::object(), MakeTag(TagConsolePorts));
            } else {
                ++modal_;
                ShowConsoleDialog(hwnd_, {}, "");
                --modal_;
            }
            break;
        case IDM_PORT_PAIRS:
        case IDM_DRIVERS:
            if (connected_) {
                ++modal_;
                ShowPortsDialog(hwnd_, client_, id == IDM_DRIVERS);
                --modal_;
            }
            break;

        case IDM_HELP_CONTENTS:
            ShellExecuteW(hwnd_, L"open", kDocsUrl, nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case IDM_README:
            ShellExecuteW(hwnd_, L"open", kReadmeUrl, nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case IDM_ABOUT:
            ShowAboutDialog(hwnd_, hello_, connected_);
            break;
    }
}

void MainWindow::OnInitMenu(HMENU menu) {
    const Json* sel = SelectedLine();
    auto enable = [menu](int id, bool on) { EnableMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED)); };
    const bool c = connected_;
    enable(IDM_DISCONNECT, c);
    enable(IDM_RELOAD, c);
    enable(IDM_EXPORT_STATS, c);
    enable(IDM_EXPORT_LOG, !log_.empty());
    enable(IDM_SERVICE_START, !c);
    enable(IDM_SERVICE_STOP, c);
    enable(IDM_SERVICE_RESTART, c);
    enable(IDM_ANSWER, c && sel && JBool(*sel, "canAnswer"));
    enable(IDM_HANGUP, c && sel && JBool(*sel, "canHangUp"));
    enable(IDM_RESET, c && sel);
    enable(IDM_TOGGLE_SERVICE, c && sel);
    enable(IDM_ALL_IN_SERVICE, c);
    enable(IDM_PROPERTIES, c && sel);
    enable(IDM_REREGISTER, c);
    enable(IDM_UNREGISTER, c);
    enable(IDM_ACCOUNT_SETTINGS, c);
    enable(IDM_NEW_LINE, c);
    enable(IDM_REMOVE_LINE, c && sel);
    enable(IDM_MODEM_DEFAULTS, c);
    enable(IDM_SELFTEST, c);
    enable(IDM_SETTINGS, c);
    enable(IDM_PORT_PAIRS, c);
    enable(IDM_DRIVERS, c);
    const std::wstring toggle = sel && !JBool(*sel, "inService", true) ? L"Put line &in service" : L"&Take line out of service";
    ModifyMenuW(menu, IDM_TOGGLE_SERVICE, MF_BYCOMMAND | MF_STRING | (c && sel ? MF_ENABLED : MF_GRAYED),
                IDM_TOGGLE_SERVICE, toggle.c_str());
    CheckMenuItem(menu, IDM_VIEW_TOOLBAR, MF_BYCOMMAND | (showToolbar_ ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, IDM_VIEW_STATUSBAR, MF_BYCOMMAND | (showStatusBar_ ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, IDM_VIEW_LOG, MF_BYCOMMAND | (showLog_ ? MF_CHECKED : MF_UNCHECKED));
}

void MainWindow::OpenLineDialog(bool isNew, const Json& config, const Json& ports) {
    LineDialogData data;
    data.isNew = isNew;
    data.defaults = config["defaults"];
    // First the ports a new line should take: the free side of each
    // com0com pair nobody uses (the side without a modem driver bound);
    // then every other port no line uses.
    const std::string current = isNew || SelectedLine() == nullptr ? std::string() : JStr(*SelectedLine(), "comPort");
    if (!current.empty()) data.ports.push_back(current);
    auto hasDriver = [&ports](const std::string& port) {
        for (const Json& m : ports["modems"]) {
            if (JStr(m, "attachedTo") == port) return true;
        }
        return false;
    };
    auto add = [&data](const std::string& port) {
        if (std::find(data.ports.begin(), data.ports.end(), port) == data.ports.end()) data.ports.push_back(port);
    };
    for (const Json& p : ports["com0comPairs"]) {
        if (!JStr(p, "lineA").empty() || !JStr(p, "lineB").empty()) continue;
        add(hasDriver(JStr(p, "a")) ? JStr(p, "b") : JStr(p, "a"));
    }
    for (const Json& p : ports["serialPorts"]) {
        if (JStr(p, "line").empty()) add(JStr(p, "port"));
    }
    std::string id;
    if (!isNew) {
        const Json* sel = SelectedLine();
        if (sel == nullptr) return;
        id = JStr(*sel, "id");
        data.driver = JStr(*sel, "driver");
        for (const Json& l : config["lines"]) {
            if (JStr(l, "id") == id) data.config = l;
        }
        if (data.config.is_null()) return;
    }
    ++modal_;
    const bool ok = ShowLineDialog(hwnd_, data);
    --modal_;
    if (!ok) return;
    if (isNew) {
        SetHint(L"Adding the line…");
        client_.Request(hwnd_, "line.add", {{"line", data.out}}, MakeTag(TagLineSaved));
    } else {
        SetHint(L"Saving the line…");
        client_.Request(hwnd_, "line.update", {{"id", id}, {"line", data.out}}, MakeTag(TagLineSaved));
    }
}

void MainWindow::OnResponse(WPARAM wp, Response* r) {
    std::unique_ptr<Response> owned(r);
    const Tag tag = static_cast<Tag>(wp & 0xffff);
    const int extra = static_cast<int>(wp >> 16);

    auto failed = [&](const std::wstring& what) {
        if (r->ok) return false;
        SetHint(what + L": " + Widen(r->error));
        if (r->error != "not connected" && r->error != "connection lost") {
            MessageBoxW(hwnd_, (what + L":\r\n" + Widen(r->error)).c_str(), L"VoIPModem Manager", MB_OK | MB_ICONEXCLAMATION);
        }
        return true;
    };
    auto askRestart = [&](const Json& result) {
        if (JBool(result, "restartRequired")) {
            if (MessageBoxW(hwnd_,
                            L"Saved. Some of these settings (SIP transport, audio path, control API) take effect "
                            L"when the service restarts. Restart it now?",
                            L"VoIPModem settings", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                client_.Request(hwnd_, "service.restart", Json::object(), MakeTag(TagRestart));
            }
        }
    };

    switch (tag) {
        case TagStatus:
            if (r->ok) ApplyStatus(r->result);
            break;
        case TagLog:
            if (r->ok) ApplyLog(r->result);
            break;
        case TagAction:
            if (!failed(L"The service refused")) SetHint(L"Done");
            Poll();
            break;
        case TagStop:
            if (!failed(L"Could not stop the service")) SetHint(L"The service is stopping");
            break;
        case TagRestart:
            if (!r->ok && r->error.find("in a call") != std::string::npos) {
                if (MessageBoxW(hwnd_, L"A line is in a call. Restart anyway (the call is dropped)?", L"Restart service",
                                MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    client_.Request(hwnd_, "service.restart", {{"force", true}}, MakeTag(TagRestart));
                }
                break;
            }
            if (!failed(L"Could not restart the service")) SetHint(L"The service is restarting…");
            break;
        case TagSettingsConfig: {
            if (failed(L"Could not read the settings")) break;
            Json changes;
            ++modal_;
            const bool ok = ShowSettingsDialog(hwnd_, r->result, extra, changes);
            --modal_;
            if (ok && !changes.empty()) {
                SetHint(L"Saving settings…");
                client_.Request(hwnd_, "config.set", changes, MakeTag(TagSettingsSaved));
            }
            break;
        }
        case TagSettingsSaved:
            if (failed(L"Could not save the settings")) break;
            SetHint(L"Settings saved to voipmodem.ini");
            askRestart(r->result);
            Poll();
            break;
        case TagNewLineConfig:
        case TagEditLineConfig:
            pendingConfig_ = r->ok ? r->result : Json();
            failed(L"Could not read the configuration");
            break;
        case TagNewLinePorts:
        case TagEditLinePorts:
            if (pendingConfig_.is_null()) break;
            if (modal_ == 0) {
                OpenLineDialog(tag == TagNewLinePorts, pendingConfig_, r->ok ? r->result : Json::object());
            }
            pendingConfig_ = Json();
            break;
        case TagLineSaved:
            if (failed(L"Could not save the line")) break;
            SetHint(JBool(r->result, "deferred") ? L"Saved; the line is in a call and takes the new settings when it ends"
                                                 : L"Line saved");
            if (r->result.contains("id")) selectedId_ = JStr(r->result, "id");
            askRestart(r->result);
            Poll();
            break;
        case TagRemove:
            if (!r->ok && r->error.find("in a call") != std::string::npos) {
                if (MessageBoxW(hwnd_, L"The line is in a call. Hang up and remove it?", L"Remove line",
                                MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    client_.Request(hwnd_, "line.remove", {{"id", selectedId_}, {"force", true}}, MakeTag(TagRemove));
                }
                break;
            }
            if (!failed(L"Could not remove the line")) SetHint(L"Line removed");
            Poll();
            break;
        case TagConsolePorts: {
            std::vector<std::string> ports;
            std::string initial;
            const Json* sel = SelectedLine();
            if (r->ok) {
                for (const Json& p : r->result["serialPorts"]) ports.push_back(JStr(p, "port"));
                if (sel) {
                    for (const Json& p : r->result["com0comPairs"]) {
                        if (JStr(p, "a") == JStr(*sel, "comPort")) initial = JStr(p, "b");
                        if (JStr(p, "b") == JStr(*sel, "comPort")) initial = JStr(p, "a");
                    }
                }
            }
            ++modal_;
            ShowConsoleDialog(hwnd_, ports, initial);
            --modal_;
            break;
        }
    }
}

void MainWindow::OnConnection(ConnectionInfo* info) {
    std::unique_ptr<ConnectionInfo> owned(info);
    if (info->connected) {
        const bool was = connected_;
        connected_ = true;
        hello_ = info->hello;
        connectionError_.clear();
        if (!was) {
            SetHint(L"Connected to VoIPModem " + Widen(JStr(hello_, "version")) + L" on " + Widen(JStr(hello_, "host")));
        }
        UpdateTitle();
        UpdateCommands();
        return;
    }
    const bool was = connected_;
    connected_ = false;
    connectionError_ = Widen(info->error);
    if (was || status_.contains("lines")) {
        status_ = Json::object();
        RebuildRows();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (!paused_) SetHint(connectionError_);
    UpdateTitle();
    UpdateStatusBar();
    UpdateCommands();
    const bool needsPassword = info->passwordRejected || info->error.find("authentication") != std::string::npos;
    if (needsPassword && !promptOpen_ && modal_ == 0) {
        MessageBoxW(hwnd_,
                    info->passwordRejected ? L"The service refused the password." : L"The service needs a password.",
                    L"VoIPModem Manager", MB_OK | MB_ICONEXCLAMATION);
        OnCommand(IDM_CONNECT);
        if (!connected_ && password_.empty()) paused_ = true; // don't ask again every second
    }
}

// ----------------------------------------------------- export, service

std::wstring SaveFileName(HWND owner, const wchar_t* filter, const wchar_t* defaultName, const wchar_t* ext) {
    wchar_t file[MAX_PATH];
    lstrcpynW(file, defaultName, MAX_PATH);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = ext;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    return GetSaveFileNameW(&ofn) ? std::wstring(file) : std::wstring();
}

std::string Csv(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        out += c;
        if (c == '"') out += '"';
    }
    return out + "\"";
}

void MainWindow::ExportStatistics() {
    const std::wstring path = SaveFileName(hwnd_, L"CSV files (*.csv)\0*.csv\0All files\0*.*\0", L"voipmodem-statistics.csv", L"csv");
    if (path.empty()) return;
    std::ofstream out(path.c_str(), std::ios::binary);
    out << "\xEF\xBB\xBF"; // UTF-8 BOM, for Excel
    out << "Statistic,Value\r\n";
    const Json& s = status_["stats"];
    for (auto it = s.begin(); it != s.end(); ++it) {
        out << Csv(it.key()) << "," << Csv(it.value().dump()) << "\r\n";
    }
    for (const Json& c : status_["carriers"]) {
        out << Csv("connects " + JStr(c, "mode")) << "," << JInt(c, "count") << "\r\n";
    }
    out << "\r\nLine,COM port,Extension,State,Calling party,Modulation,TX bps,RX bps,Call seconds,Bytes TX,Bytes RX,"
           "Retrains,Errors,Registration,Driver\r\n";
    for (const Json& l : status_["lines"]) {
        out << Csv(JStr(l, "id")) << "," << Csv(JStr(l, "comPort")) << "," << Csv(JStr(l, "extension")) << ","
            << Csv(JStr(l, "state")) << "," << Csv(JStr(l, "remoteParty")) << "," << Csv(JStr(l, "modulation")) << ","
            << JInt(l, "txBps") << "," << JInt(l, "rxBps") << "," << JInt(l, "callSeconds") << ","
            << static_cast<unsigned long long>(JNum(l, "bytesTx")) << ","
            << static_cast<unsigned long long>(JNum(l, "bytesRx")) << "," << JInt(l, "retrains") << ","
            << JInt(l, "errors") << "," << Csv(JStr(l, "registration")) << "," << Csv(JStr(l, "driver")) << "\r\n";
    }
    SetHint(out.good() ? L"Statistics exported to " + path : L"Could not write " + path);
}

void MainWindow::ExportLog() {
    const std::wstring path = SaveFileName(hwnd_, L"Text files (*.txt)\0*.txt\0All files\0*.*\0", L"voipmodem-events.txt", L"txt");
    if (path.empty()) return;
    std::ofstream out(path.c_str(), std::ios::binary);
    for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
        out << Narrow(it->time) << " [" << Narrow(it->level) << "] " << Narrow(it->message) << "\r\n";
    }
    SetHint(out.good() ? L"Event log exported to " + path : L"Could not write " + path);
}

void MainWindow::StartService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    SC_HANDLE service = scm ? OpenServiceW(scm, L"VoIPModem", SERVICE_START | SERVICE_QUERY_STATUS) : nullptr;
    if (service == nullptr) {
        const DWORD err = GetLastError();
        if (scm) CloseServiceHandle(scm);
        MessageBoxW(hwnd_,
                    err == ERROR_SERVICE_DOES_NOT_EXIST
                        ? L"The VoIPModem service is not installed on this computer. Use File > Install service, "
                          L"or run VoIPModem.exe from a console."
                        : (L"Cannot open the VoIPModem service (error " + std::to_wstring(err) +
                           L"). Starting a service needs administrator rights.")
                              .c_str(),
                    L"Start service", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    if (!::StartServiceW(service, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
        const DWORD err = GetLastError();
        MessageBoxW(hwnd_, (L"The service did not start (error " + std::to_wstring(err) + L"). See its log.").c_str(),
                    L"Start service", MB_OK | MB_ICONEXCLAMATION);
    } else {
        SetHint(L"Starting the VoIPModem service…");
        paused_ = false;
    }
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
}

void MainWindow::InstallService() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring path = exe;
    path = path.substr(0, path.find_last_of(L'\\') + 1) + L"VoIPModem.exe";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(hwnd_, (L"VoIPModem.exe is not next to the Manager (" + path + L").").c_str(), L"Install service",
                    MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    if (MessageBoxW(hwnd_,
                    L"Install VoIPModem as a Windows service that starts with Windows, using voipmodem.ini next to "
                    L"VoIPModem.exe? Administrator rights are needed.",
                    L"Install service", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    SHELLEXECUTEINFOW sei{sizeof sei};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = hwnd_;
    sei.lpVerb = L"runas";
    sei.lpFile = path.c_str();
    sei.lpParameters = L"--install";
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        SetHint(L"The service was not installed");
        return;
    }
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 30000);
        DWORD code = 1;
        GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);
        MessageBoxW(hwnd_,
                    code == 0 ? L"Installed. Start it with File > Start service (or net start VoIPModem)."
                              : L"Installation failed (is it already installed?).",
                    L"Install service", MB_OK | (code == 0 ? MB_ICONINFORMATION : MB_ICONEXCLAMATION));
    }
}

// --------------------------------------------------------- window proc

LRESULT CALLBACK MainWindow::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->OnMessage(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            CreateChildren();
            return 0;
        case WM_SIZE:
            Layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize = {Scale(760), Scale(520)};
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            Paint(dc);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_TIMER:
            if (wp == kPollTimer) {
                UpdateStatusBar();
                Poll();
            }
            return 0;
        case WM_MEASUREITEM: {
            auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            mi->itemHeight = static_cast<UINT>(Scale(14));
            return TRUE;
        }
        case WM_DRAWITEM:
            OnDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lp));
            return TRUE;
        case WM_NOTIFY:
            return OnNotify(reinterpret_cast<NMHDR*>(lp));
        case WM_COMMAND:
            if (reinterpret_cast<HWND>(lp) == filter_ && HIWORD(wp) == CBN_SELCHANGE) {
                static const char* const kinds[] = {"all", "busy", "idle", "err"};
                const int sel = static_cast<int>(SendMessageW(filter_, CB_GETCURSEL, 0, 0));
                filterKind_ = kinds[sel < 0 || sel > 3 ? 0 : sel];
                RebuildRows();
                SetFocus(list_);
                return 0;
            }
            OnCommand(LOWORD(wp));
            return 0;
        case WM_INITMENUPOPUP:
            OnInitMenu(reinterpret_cast<HMENU>(wp));
            return 0;
        case WM_APP_RESPONSE:
            OnResponse(wp, reinterpret_cast<Response*>(lp));
            return 0;
        case WM_APP_CONNECTION:
            OnConnection(reinterpret_cast<ConnectionInfo*>(lp));
            return 0;
        case WM_SETFOCUS:
            SetFocus(list_);
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, kPollTimer);
            SaveSettings();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

} // namespace

} // namespace manager

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);

    static manager::MainWindow window;
    if (!window.Create(instance, show)) {
        return 1;
    }
    HACCEL accel = LoadAcceleratorsW(instance, MAKEINTRESOURCEW(IDR_ACCEL));
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!TranslateAcceleratorW(window.Handle(), accel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}
