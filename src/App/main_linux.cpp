// Linux entry point (docs/LINUX.md). Windows uses main.cpp.
//
// Command line:
//   voipmodem [config.ini]   run in the foreground; SIGINT or SIGTERM stops
//
// There is no --install: under systemd, use the unit file in
// config/voipmodem.service. The process stays in the foreground, which is
// what systemd expects (Type=simple); the log also goes to standard output,
// so it reaches the journal.

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <pthread.h>

#include "Application.h"
#include "../Lib/Version.h"

namespace {

// Runs the Application, again after each restart it asks for (the control
// API's service.restart).
int RunApplication(const std::string& configPath, bool underSystemd) {
    for (;;) {
        voipmodem::ResetStopRequest();
        int code;
        bool restart;
        {
            voipmodem::Application app(configPath, underSystemd);
            code = app.Run();
            restart = app.RestartRequested();
        }
        if (!restart) {
            return code;
        }
    }
}

// SIGINT and SIGTERM are blocked in every thread (the mask is inherited by
// the threads the Application starts later) and taken here with sigwait(),
// so stopping runs as ordinary code rather than in a signal handler.
void StartSignalThread() {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, nullptr);
    std::thread([set] {
        for (;;) {
            int sig = 0;
            if (sigwait(&set, &sig) == 0) {
                voipmodem::RequestStop();
            }
        }
    }).detach();
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    const std::string first = args.empty() ? "" : args[0];

    if (first == "--help" || first == "-h") {
        std::cout << "Usage:\n"
                     "  voipmodem [config.ini]   run in the foreground (Ctrl+C or SIGTERM stops)\n"
                     "  voipmodem --version\n"
                     "Default config: voipmodem.ini in the current directory.\n";
        return 0;
    }
    if (first == "--version") {
        std::cout << "VoIPModem " << VOIPMODEM_VERSION << "\n";
        return 0;
    }
    if (!first.empty() && first[0] == '-') {
        std::cerr << "Unknown option " << first << " (see --help)\n";
        return 2;
    }

    // A control API client that disconnects mid-reply must not kill us.
    std::signal(SIGPIPE, SIG_IGN);
    StartSignalThread();

    // systemd sets INVOCATION_ID for the processes of a unit.
    const bool underSystemd = std::getenv("INVOCATION_ID") != nullptr;
    const std::string configPath = first.empty() ? "voipmodem.ini" : first;
    return RunApplication(configPath, underSystemd);
}
