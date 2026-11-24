#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ModemInstance.h"
#include "../Lib/Config/AppConfig.h"
#include "../Lib/Control/ControlServer.h"
#include "../Lib/Control/ServiceStats.h"
#include "../Lib/Control/SystemInfo.h"
#include "../Lib/Logging/Logger.h"
#include "../Lib/Sip/PjsipEndpoint.h"

namespace voipmodem {

class IniDocument;

// Requests that a running Application::Run() loop stop at the next
// opportunity. Safe to call from a console control handler (Ctrl+C) or a
// service control handler.
void RequestStop();

// Clears a stop request, before running a new Application after a restart.
void ResetStopRequest();

// Top-level orchestrator: loads config, initializes the shared logger,
// starts the one process-wide PJSIP endpoint if SIP is configured, runs
// one ModemInstance per configured modem line, and serves the control API
// (docs/CONTROL_API.md) the Manager uses, until a stop request. Owns no
// serial port, call controller, or state machine itself — those belong to
// ModemInstance, one per modem line. See docs/ARCHITECTURE.md "Multiple
// modem instances" and "Control API and the Manager".
//
// Threads: Run()'s own thread (the "main" thread, which created the PJSIP
// endpoint) owns the list of lines and every change to it; control API
// requests that change something are queued to it (RunOnMain) and waited
// for. Read-only requests take snapshots under linesMutex_ directly.
class Application {
public:
    Application(std::string configPath, bool runningAsService);
    ~Application();

    // Returns a process exit code (0 on clean shutdown).
    int Run();

    // True when Run() returned because of a "service.restart" request: the
    // caller should construct a new Application and run it.
    bool RestartRequested() const { return restartRequested_; }

private:
    using Json = nlohmann::json;

    struct Line {
        ModemInstanceConfig config;
        std::unique_ptr<ModemInstance> instance;
        std::chrono::steady_clock::time_point nextOpenAttempt{};
        std::optional<ModemInstanceConfig> pendingConfig; // applied once the line is idle
        bool pendingRemoval = false;
    };

    // Lines.
    std::unique_ptr<ModemInstance> MakeInstance(const ModemInstanceConfig& config);
    void OpenLine(Line& line);
    void ApplyLineConfig(Line& line, const ModemInstanceConfig& config);
    void ApplyConfig(const AppConfig& fresh); // diff against what runs; main thread
    Line* FindLine(const std::string& id);
    void Tick();

    // Control API.
    Json Handle(const std::string& cmd, const Json& args);
    Json RunOnMain(std::function<Json()> task);
    Json Hello();
    Json Status();
    Json LineJson(const Line& line) const;
    Json ConfigJson();
    Json SetConfig(const Json& args);
    Json AddLine(const Json& args);
    Json UpdateLine(const Json& args);
    Json RemoveLine(const Json& args);
    Json LineCommandRequest(const Json& args, LineCommand command);
    Json SetInService(const Json& args);
    Json SystemPortsJson();

    // Writes `edit`'s changes to the ini, re-reads it and applies it.
    Json EditConfig(const std::function<void(IniDocument&)>& edit);
    AppConfig LoadConfigFile() const;
    ModemInstanceConfig LoadDefaultsFile() const;
    void WriteLineKeys(IniDocument& ini, const std::string& section, const Json& line, bool isNew);

    bool AnyCallActive();

    std::string configPath_;
    bool runningAsService_;
    Logger logger_;
    AppConfig config_;      // as loaded / last applied
    AppConfig startConfig_; // the process-wide parts in force (restart to change)
    ModemInstanceConfig lineDefaults_; // [Defaults]; under infoMutex_
    ServiceStats stats_;
    std::unique_ptr<PjsipEndpoint> sipEndpoint_; // null if SIP is disabled or failed to start
    std::string pjsipVersion_;
    std::unique_ptr<ControlServer> control_;

    std::mutex linesMutex_; // the vector; each Line is only changed on the main thread
    std::vector<std::unique_ptr<Line>> lines_;

    // Tasks for the main thread (RunOnMain).
    std::mutex tasksMutex_;
    std::condition_variable tasksCv_;
    std::deque<std::function<void()>> tasks_;

    // Refreshed by Tick(); read by Status() under infoMutex_.
    mutable std::mutex infoMutex_;
    SystemPorts ports_;
    double cpuPercent_ = 0;
    double rtpKbps_ = 0;
    double lastJitterMeanMs_ = -1, lastJitterMaxMs_ = -1, lastLossPercent_ = -1;
    std::map<std::string, std::uint64_t> lastRtpBytes_;
    std::uint64_t lastCpuTime100ns_ = 0;
    std::chrono::steady_clock::time_point lastTick_{};
    std::chrono::steady_clock::time_point lastPortsQuery_{};

    std::chrono::system_clock::time_point startedAt_;
    std::chrono::system_clock::time_point configLoadedAt_;
    std::atomic<bool> restartRequired_{false}; // written on the main thread, read by API threads
    bool restartRequested_ = false;
};

} // namespace voipmodem
