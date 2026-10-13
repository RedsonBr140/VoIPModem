#include <Windows.h>

#include <iostream>
#include <string>

#include "Application.h"

namespace {

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

} // namespace

int main(int argc, char** argv) {
    const std::string configPath = (argc > 1) ? argv[1] : "voipmodem.ini";

    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
        std::cerr << "Warning: failed to install console control handler; Ctrl+C may not shut down cleanly.\n";
    }

    voipmodem::Application app(configPath);
    return app.Run();
}
