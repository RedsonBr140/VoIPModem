#include <Windows.h>

#include <iostream>
#include <string>
#include <vector>

#include "Application.h"

// Command line:
//   VoIPModem.exe [config.ini]               run in this console (Ctrl+C stops)
//   VoIPModem.exe --install [config.ini]     install the Windows service (needs admin)
//   VoIPModem.exe --uninstall                remove it
//   VoIPModem.exe --service <config.ini>     how the service control manager starts it
//
// The service runs the same Application as the console; the Manager
// (VoIPModemManager.exe) talks to either through the control API.

namespace {

const char kServiceName[] = "VoIPModem";
const char kServiceDisplayName[] = "VoIPModem modem bank";
const char kServiceDescription[] =
    "Hayes-compatible modem lines over SIP: answers and places modem calls through Asterisk and "
    "serves them on (virtual) COM ports. Managed with VoIPModem Manager.";

std::string g_configPath;
SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
SERVICE_STATUS g_status{};

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    switch (ctrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            voipmodem::RequestStop();
            return TRUE;
        default:
            return FALSE;
    }
}

std::string ExePath() {
    char path[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    return std::string(path, n);
}

std::string FullPath(const std::string& path) {
    char full[MAX_PATH];
    const DWORD n = GetFullPathNameA(path.c_str(), MAX_PATH, full, nullptr);
    return n > 0 && n < MAX_PATH ? std::string(full, n) : path;
}

// The default config is voipmodem.ini next to the executable.
std::string DefaultConfigPath() {
    const std::string exe = ExePath();
    return exe.substr(0, exe.find_last_of('\\') + 1) + "voipmodem.ini";
}

// Runs the Application, again after each restart it asks for.
int RunApplication(const std::string& configPath, bool asService) {
    for (;;) {
        voipmodem::ResetStopRequest();
        int code;
        bool restart;
        {
            voipmodem::Application app(configPath, asService);
            code = app.Run();
            restart = app.RestartRequested();
        }
        if (!restart) {
            return code;
        }
    }
}

void ReportServiceStatus(DWORD state, DWORD exitCode = NO_ERROR) {
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = state;
    g_status.dwWin32ExitCode = exitCode;
    g_status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    g_status.dwWaitHint = state == SERVICE_RUNNING || state == SERVICE_STOPPED ? 0 : 10000;
    static DWORD checkPoint = 1;
    g_status.dwCheckPoint = state == SERVICE_RUNNING || state == SERVICE_STOPPED ? 0 : checkPoint++;
    SetServiceStatus(g_statusHandle, &g_status);
}

DWORD WINAPI ServiceControlHandler(DWORD control, DWORD, LPVOID, LPVOID) {
    switch (control) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            ReportServiceStatus(SERVICE_STOP_PENDING);
            voipmodem::RequestStop();
            return NO_ERROR;
        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;
        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

void WINAPI ServiceMain(DWORD, LPSTR*) {
    g_statusHandle = RegisterServiceCtrlHandlerExA(kServiceName, ServiceControlHandler, nullptr);
    if (g_statusHandle == nullptr) {
        return;
    }
    ReportServiceStatus(SERVICE_START_PENDING);
    // The working directory of a service is System32; relative paths in the
    // ini are taken relative to the ini itself (see Application).
    ReportServiceStatus(SERVICE_RUNNING);
    const int code = RunApplication(g_configPath, true);
    ReportServiceStatus(SERVICE_STOPPED, code == 0 ? NO_ERROR : ERROR_SERVICE_SPECIFIC_ERROR);
}

int InstallService(const std::string& configPath) {
    const std::string command = "\"" + ExePath() + "\" --service \"" + FullPath(configPath) + "\"";
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (scm == nullptr) {
        std::cerr << "Cannot open the service control manager (error " << GetLastError()
                  << "). Run this from an administrator command prompt.\n";
        return 1;
    }
    SC_HANDLE service = CreateServiceA(scm, kServiceName, kServiceDisplayName, SERVICE_ALL_ACCESS,
                                       SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                                       command.c_str(), nullptr, nullptr, "Tcpip\0\0", nullptr, nullptr);
    if (service == nullptr) {
        const DWORD err = GetLastError();
        CloseServiceHandle(scm);
        std::cerr << (err == ERROR_SERVICE_EXISTS ? "The VoIPModem service is already installed.\n"
                                                  : "CreateService failed (error " + std::to_string(err) + ").\n");
        return 1;
    }
    SERVICE_DESCRIPTIONA description{const_cast<char*>(kServiceDescription)};
    ChangeServiceConfig2A(service, SERVICE_CONFIG_DESCRIPTION, &description);
    // Restart after a crash: 5 s, 30 s, then every 60 s.
    SC_ACTION actions[3] = {{SC_ACTION_RESTART, 5000}, {SC_ACTION_RESTART, 30000}, {SC_ACTION_RESTART, 60000}};
    SERVICE_FAILURE_ACTIONSA failure{};
    failure.dwResetPeriod = 24 * 3600;
    failure.cActions = 3;
    failure.lpsaActions = actions;
    ChangeServiceConfig2A(service, SERVICE_CONFIG_FAILURE_ACTIONS, &failure);
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    std::cout << "Installed the \"" << kServiceDisplayName << "\" service (starts automatically) with\n  "
              << command << "\nStart it now with: net start " << kServiceName << "\n";
    return 0;
}

int UninstallService() {
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        std::cerr << "Cannot open the service control manager (error " << GetLastError()
                  << "). Run this from an administrator command prompt.\n";
        return 1;
    }
    SC_HANDLE service = OpenServiceA(scm, kServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
    if (service == nullptr) {
        std::cerr << "The VoIPModem service is not installed (error " << GetLastError() << ").\n";
        CloseServiceHandle(scm);
        return 1;
    }
    SERVICE_STATUS status;
    if (ControlService(service, SERVICE_CONTROL_STOP, &status)) {
        for (int i = 0; i < 30 && QueryServiceStatus(service, &status) && status.dwCurrentState != SERVICE_STOPPED; ++i) {
            Sleep(500);
        }
    }
    const BOOL deleted = DeleteService(service);
    const DWORD err = GetLastError();
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    if (!deleted) {
        std::cerr << "DeleteService failed (error " << err << ").\n";
        return 1;
    }
    std::cout << "Removed the VoIPModem service.\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    const std::string first = args.empty() ? "" : args[0];

    if (first == "--install") {
        return InstallService(args.size() > 1 ? args[1] : DefaultConfigPath());
    }
    if (first == "--uninstall") {
        return UninstallService();
    }
    if (first == "--service") {
        g_configPath = args.size() > 1 ? args[1] : DefaultConfigPath();
        SERVICE_TABLE_ENTRYA table[] = {{const_cast<char*>(kServiceName), ServiceMain}, {nullptr, nullptr}};
        if (!StartServiceCtrlDispatcherA(table)) {
            std::cerr << "--service is for the service control manager; to run in this console, leave it out.\n";
            return 1;
        }
        return 0;
    }
    if (first == "--help" || first == "-h" || first == "/?") {
        std::cout << "Usage:\n"
                     "  VoIPModem [config.ini]            run in this console (Ctrl+C stops)\n"
                     "  VoIPModem --install [config.ini]  install as a Windows service (administrator)\n"
                     "  VoIPModem --uninstall             remove the service\n"
                     "Default config: voipmodem.ini in the current folder.\n";
        return 0;
    }

    const std::string configPath = first.empty() ? "voipmodem.ini" : first;
    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
        std::cerr << "Warning: failed to install console control handler; Ctrl+C may not shut down cleanly.\n";
    }
    return RunApplication(configPath, false);
}
