#include "Application.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <ctime>
#include <future>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

#include "../Lib/Config/IniConfig.h"
#include "../Lib/Config/IniDocument.h"
#include "../Lib/Modem/LoopbackTest.h"
#include "../Lib/Version.h"

namespace voipmodem {

namespace {

std::atomic<bool> g_stopRequested{false};

constexpr std::chrono::seconds kOpenRetryInterval{30};
constexpr std::chrono::seconds kPortsQueryInterval{10};
constexpr std::chrono::seconds kMainTaskTimeout{30};

bool IsAbsolutePath(const std::string& path) {
    return (path.size() > 1 && path[1] == ':') || (!path.empty() && (path[0] == '\\' || path[0] == '/'));
}

std::string DirectoryOf(const std::string& path) {
    const auto slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// A relative log file is relative to the ini file's folder (a service's
// working directory is System32, not the install folder).
std::string ResolveNextTo(const std::string& configPath, const std::string& path) {
    if (path.empty() || IsAbsolutePath(path)) {
        return path;
    }
    return DirectoryOf(configPath) + path;
}

std::string LowerCase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool SameLine(const ModemInstanceConfig& a, const ModemInstanceConfig& b, bool compareService) {
    return a.name == b.name && a.comPort == b.comPort && a.baudRate == b.baudRate &&
           a.modemIdentification == b.modemIdentification && a.autoAnswerRingCount == b.autoAnswerRingCount &&
           a.enableLocalAudio == b.enableLocalAudio && a.modulations == b.modulations &&
           a.errorControl == b.errorControl && a.compression == b.compression &&
           (!compareService || a.inService == b.inService) && a.sipServer == b.sipServer &&
           a.sipExtension == b.sipExtension && a.sipPassword == b.sipPassword && a.sipRealm == b.sipRealm;
}

// The settings only a restart applies: the SIP transport, the media path
// and the control API are created once per process.
bool SameProcessSettings(const AppConfig& a, const AppConfig& b) {
    return a.sipLocalPort == b.sipLocalPort && a.sipTransport == b.sipTransport &&
           a.sipRegisterExpiry == b.sipRegisterExpiry && a.sipRegisterOnStart == b.sipRegisterOnStart &&
           a.preferredCodec == b.preferredCodec && a.jitterBufferMs == b.jitterBufferMs && a.rtpPort == b.rtpPort &&
           a.rtpPortRange == b.rtpPortRange && a.echoCanceller == b.echoCanceller &&
           a.controlPort == b.controlPort && a.controlBindAddress == b.controlBindAddress &&
           a.controlPassword == b.controlPassword;
}

std::string IsoTime(std::chrono::system_clock::time_point tp) {
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tmBuf);
    return buf;
}

std::string HostName() {
#ifdef _WIN32
    char name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof name;
    if (GetComputerNameA(name, &size)) {
        return std::string(name, size);
    }
#endif
    return "";
}

// Process CPU time so far, in 100 ns units.
std::uint64_t ProcessCpuTime() {
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        const auto value = [](const FILETIME& ft) {
            return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        };
        return value(kernel) + value(user);
    }
#endif
    return 0;
}

unsigned ProcessorCount() {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors > 0 ? info.dwNumberOfProcessors : 1;
#else
    return 1;
#endif
}

// JSON value -> ini text: numbers as integers, booleans as 1/0.
std::string IniValue(const nlohmann::json& v) {
    if (v.is_boolean()) return v.get<bool>() ? "1" : "0";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number()) return std::to_string(static_cast<long long>(v.get<double>()));
    if (v.is_string()) {
        std::string s = v.get<std::string>();
        if (s.find_first_of("\r\n") != std::string::npos) {
            throw ControlError("values cannot contain line breaks");
        }
        return s;
    }
    throw ControlError("unsupported value type");
}

int IntArg(const nlohmann::json& v, const char* name, int lo, int hi) {
    if (!v.is_number_integer()) {
        throw ControlError(std::string(name) + " must be a whole number");
    }
    const long long n = v.get<long long>();
    if (n < lo || n > hi) {
        throw ControlError(std::string(name) + " must be between " + std::to_string(lo) + " and " + std::to_string(hi));
    }
    return static_cast<int>(n);
}

std::string StringArg(const nlohmann::json& args, const char* name) {
    if (!args.contains(name) || !args[name].is_string()) {
        throw ControlError(std::string("missing \"") + name + "\"");
    }
    return args[name].get<std::string>();
}

bool ValidLineName(const std::string& name) {
    if (name.empty() || name.size() > 32) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; });
}

nlohmann::json LineConfigJson(const ModemInstanceConfig& c) {
    return {{"id", c.name},
            {"comPort", c.comPort},
            {"baudRate", c.baudRate},
            {"identification", c.modemIdentification},
            {"autoAnswerRings", c.autoAnswerRingCount},
            {"modulations", c.modulations},
            {"errorControl", c.errorControl},
            {"compression", c.compression},
            {"inService", c.inService},
            {"localAudio", c.enableLocalAudio},
            {"sipServer", c.sipServer},
            {"sipExtension", c.sipExtension},
            {"sipRealm", c.sipRealm},
            {"passwordSet", !c.sipPassword.empty()}};
}

} // namespace

void RequestStop() {
    g_stopRequested = true;
}

void ResetStopRequest() {
    g_stopRequested = false;
}

Application::Application(std::string configPath, bool runningAsService)
    : configPath_(std::move(configPath)), runningAsService_(runningAsService) {
    config_ = LoadConfigFile();
    startConfig_ = config_;
    lineDefaults_ = LoadDefaultsFile();
    startedAt_ = configLoadedAt_ = std::chrono::system_clock::now();
}

Application::~Application() = default;

ModemInstanceConfig Application::LoadDefaultsFile() const {
    IniConfig ini;
    ini.LoadFromFile(configPath_);
    return LoadLineDefaults(ini);
}

AppConfig Application::LoadConfigFile() const {
    IniConfig ini;
    ini.LoadFromFile(configPath_); // missing/unreadable file just leaves defaults in place
    AppConfig config = LoadAppConfig(ini);
    config.logFilePath = ResolveNextTo(configPath_, config.logFilePath);
    return config;
}

int Application::Run() {
    logger_.SetConsoleEcho(!runningAsService_);
    logger_.Init(config_.logLevel, config_.logFilePath, static_cast<std::uint64_t>(config_.logRotateKB) * 1024,
                 config_.logKeepFiles);
    logger_.Info(std::string("VoIPModem ") + VOIPMODEM_VERSION + " starting" +
                 (runningAsService_ ? " as a Windows service" : "") + " (config " + configPath_ +
                 "; modem engine: libv90modem " + ModemEngineVersion() + ")");

    if (config_.sipLocalPort > 0) {
        auto endpoint = std::make_unique<PjsipEndpoint>(logger_);
        EndpointSettings settings;
        settings.localPort = config_.sipLocalPort;
        settings.transport = config_.sipTransport;
        settings.preferredCodec = config_.preferredCodec;
        settings.jitterBufferMs = config_.jitterBufferMs;
        settings.echoCanceller = config_.echoCanceller;
        if (endpoint->Start(settings)) {
            pjsipVersion_ = endpoint->LibraryVersion();
            sipEndpoint_ = std::move(endpoint);
        } else {
            logger_.Error("SIP transport failed to start; every modem instance will use "
                          "NullCallController regardless of its own SipServer setting.");
        }
    }

    ports_ = QuerySystemPorts();
    lastPortsQuery_ = std::chrono::steady_clock::now();

    for (const ModemInstanceConfig& lineConfig : config_.modems) {
        auto line = std::make_unique<Line>();
        line->config = lineConfig;
        OpenLine(*line);
        std::lock_guard<std::mutex> lock(linesMutex_);
        lines_.push_back(std::move(line));
    }

    if (config_.controlPort > 0) {
        control_ = std::make_unique<ControlServer>(logger_);
        if (!control_->Start(config_.controlBindAddress, config_.controlPort, config_.controlPassword,
                             [this](const std::string& cmd, const Json& args) { return Handle(cmd, args); })) {
            control_.reset();
            logger_.Error("The control API is not available; the Manager cannot connect.");
        }
    }

    size_t running = 0;
    {
        std::lock_guard<std::mutex> lock(linesMutex_);
        for (const auto& line : lines_) {
            running += line->instance->IsRunning() ? 1 : 0;
        }
    }
    if (running == 0 && !control_) {
        logger_.Error("No modem line could be opened and there is no control API; exiting.");
        return 1;
    }
    logger_.Info(std::to_string(running) + " of " + std::to_string(lines_.size()) + " modem line(s) running." +
                 (runningAsService_ ? "" : " Press Ctrl+C to stop."));

    lastTick_ = std::chrono::steady_clock::now();
    lastCpuTime100ns_ = ProcessCpuTime();
    while (!g_stopRequested) {
        std::deque<std::function<void()>> tasks;
        {
            std::unique_lock<std::mutex> lock(tasksMutex_);
            tasksCv_.wait_for(lock, std::chrono::milliseconds(200), [this] { return !tasks_.empty(); });
            tasks.swap(tasks_);
        }
        for (auto& task : tasks) {
            task();
        }
        if (std::chrono::steady_clock::now() - lastTick_ >= std::chrono::seconds(1)) {
            Tick();
        }
    }

    logger_.Info(restartRequested_ ? "VoIPModem restarting" : "VoIPModem shutting down");
    {
        // Requests still queued get an answer, so their clients don't hang.
        std::lock_guard<std::mutex> lock(tasksMutex_);
        for (auto& task : tasks_) {
            task();
        }
        tasks_.clear();
    }
    if (control_) {
        control_->Stop();
        control_.reset();
    }
    std::vector<std::unique_ptr<Line>> lines;
    {
        std::lock_guard<std::mutex> lock(linesMutex_);
        lines.swap(lines_);
    }
    for (auto& line : lines) {
        line->instance->Stop();
    }
    lines.clear();          // unregisters each SIP account
    sipEndpoint_.reset();   // then the endpoint itself
    return 0;
}

// --------------------------------------------------------------- lines

std::unique_ptr<ModemInstance> Application::MakeInstance(const ModemInstanceConfig& config) {
    LineRuntimeSettings runtime;
    runtime.sipTransport = startConfig_.sipTransport;
    runtime.sipRegisterExpiry = startConfig_.sipRegisterExpiry;
    runtime.sipRegisterOnStart = startConfig_.sipRegisterOnStart;
    runtime.rtpPort = startConfig_.rtpPort;
    runtime.rtpPortRange = startConfig_.rtpPortRange;
    runtime.traceAtCommands = config_.traceAtCommands;
    return std::make_unique<ModemInstance>(config, logger_, sipEndpoint_.get(), runtime, stats_);
}

// (Re)creates the line's modem from line.config and starts it if its port
// opens; otherwise it shows "Port closed" and Tick() tries again later.
void Application::OpenLine(Line& line) {
    std::unique_ptr<ModemInstance> fresh = MakeInstance(line.config);
    std::unique_ptr<ModemInstance> old;
    if (fresh->Open()) {
        fresh->Start();
    } else {
        const std::string label = line.config.name.empty() ? line.config.comPort
                                                           : line.config.name + "/" + line.config.comPort;
        logger_.Error("Cannot open modem line '" + label + "'; retrying every " +
                      std::to_string(kOpenRetryInterval.count()) + " s.");
        line.nextOpenAttempt = std::chrono::steady_clock::now() + kOpenRetryInterval;
    }
    {
        std::lock_guard<std::mutex> lock(linesMutex_);
        old = std::move(line.instance);
        line.instance = std::move(fresh);
    }
    if (old) {
        old->Stop();
    }
}

void Application::ApplyLineConfig(Line& line, const ModemInstanceConfig& config) {
    if (line.instance) {
        line.instance->Stop(); // free the COM port and the SIP extension first
        std::unique_ptr<ModemInstance> old;
        {
            std::lock_guard<std::mutex> lock(linesMutex_);
            old = std::move(line.instance);
            line.instance = MakeInstance(config); // placeholder while reopening, for status reads
        }
    }
    {
        std::lock_guard<std::mutex> lock(linesMutex_);
        line.config = config;
        line.pendingConfig.reset();
    }
    OpenLine(line);
}

Application::Line* Application::FindLine(const std::string& id) {
    for (auto& line : lines_) {
        if (LowerCase(line->config.name) == LowerCase(id)) {
            return line.get();
        }
    }
    return nullptr;
}

void Application::ApplyConfig(const AppConfig& fresh) {
    logger_.Reconfigure(fresh.logLevel, fresh.logFilePath, static_cast<std::uint64_t>(fresh.logRotateKB) * 1024,
                        fresh.logKeepFiles);
    if (fresh.traceAtCommands != config_.traceAtCommands) {
        std::lock_guard<std::mutex> lock(linesMutex_);
        for (auto& line : lines_) {
            line->instance->SetTrace(fresh.traceAtCommands);
        }
    }
    restartRequired_ = !SameProcessSettings(fresh, startConfig_);

    // Lines that are gone.
    std::unique_lock<std::mutex> goneLock(linesMutex_);
    for (auto& line : lines_) {
        const bool kept = std::any_of(fresh.modems.begin(), fresh.modems.end(), [&](const ModemInstanceConfig& c) {
            return LowerCase(c.name) == LowerCase(line->config.name);
        });
        if (!kept) {
            line->pendingRemoval = true;
        }
    }
    goneLock.unlock();

    // New and changed lines, in the ini's order.
    std::vector<std::unique_ptr<Line>> ordered;
    {
        std::lock_guard<std::mutex> lock(linesMutex_);
        for (const ModemInstanceConfig& c : fresh.modems) {
            for (auto it = lines_.begin(); it != lines_.end(); ++it) {
                if (LowerCase((*it)->config.name) == LowerCase(c.name)) {
                    ordered.push_back(std::move(*it));
                    lines_.erase(it);
                    break;
                }
            }
        }
        for (auto& rest : lines_) {
            ordered.push_back(std::move(rest)); // being removed, kept until idle
        }
        lines_.swap(ordered);
    }
    for (const ModemInstanceConfig& c : fresh.modems) {
        Line* line = FindLine(c.name);
        if (line == nullptr) {
            auto added = std::make_unique<Line>();
            added->config = c;
            logger_.Info("Adding modem line '" + c.name + "' on " + c.comPort);
            OpenLine(*added);
            std::lock_guard<std::mutex> lock(linesMutex_);
            // Insert at its position among the configured lines.
            size_t position = 0;
            for (const ModemInstanceConfig& other : fresh.modems) {
                if (&other == &c) break;
                ++position;
            }
            lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(std::min(position, lines_.size())),
                          std::move(added));
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(linesMutex_);
            line->pendingRemoval = false; // back in the ini
        }
        if (SameLine(line->config, c, false)) {
            std::lock_guard<std::mutex> lock(linesMutex_);
            if (line->config.inService != c.inService) {
                line->config.inService = c.inService;
                line->instance->Post(c.inService ? LineCommand::InService : LineCommand::OutOfService);
            }
            line->pendingConfig.reset();
            continue;
        }
        if (line->instance->InCall()) {
            logger_.Info("Line '" + c.name + "' is in a call; its new settings apply when the call ends.");
            std::lock_guard<std::mutex> lock(linesMutex_);
            line->pendingConfig = c;
        } else {
            logger_.Info("Applying new settings to line '" + c.name + "'");
            ApplyLineConfig(*line, c);
        }
    }

    config_ = fresh;
    configLoadedAt_ = std::chrono::system_clock::now();
    {
        ModemInstanceConfig defaults = LoadDefaultsFile();
        std::lock_guard<std::mutex> lock(infoMutex_);
        lineDefaults_ = std::move(defaults);
    }
    Tick(); // removals of idle lines happen now
}

void Application::Tick() {
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - lastTick_).count();
    lastTick_ = now;

    // Lines: reopen closed ports, apply deferred changes once idle.
    std::vector<Line*> toReopen;
    std::vector<Line*> toReconfigure;
    std::vector<std::unique_ptr<Line>> removed;
    int busy = 0;
    std::uint64_t rtpDeltaBytes = 0;
    double jitterSum = 0, jitterMax = -1;
    int jitterCount = 0;
    unsigned long lost = 0, received = 0;
    {
        std::lock_guard<std::mutex> lock(linesMutex_);
        for (auto it = lines_.begin(); it != lines_.end();) {
            Line& line = **it;
            const bool inCall = line.instance->InCall();
            if (line.pendingRemoval && !inCall) {
                logger_.Info("Removing modem line '" + line.config.name + "'");
                removed.push_back(std::move(*it));
                it = lines_.erase(it);
                continue;
            }
            const LineStatus st = line.instance->Status();
            busy += inCall ? 1 : 0;
            if (line.pendingConfig && !inCall) {
                toReconfigure.push_back(&line);
            } else if (!st.portOpen && now >= line.nextOpenAttempt && !line.pendingRemoval) {
                toReopen.push_back(&line);
            }
            std::uint64_t& last = lastRtpBytes_[line.config.name];
            if (st.rtpBytes >= last) {
                rtpDeltaBytes += st.rtpBytes - last;
            }
            last = st.rtpBytes;
            if (st.jitterMeanMs >= 0) {
                jitterSum += st.jitterMeanMs;
                ++jitterCount;
                jitterMax = std::max(jitterMax, st.jitterMaxMs);
                lost += st.rtpPacketsLost;
                received += st.rtpPacketsReceived;
            }
            ++it;
        }
    }
    for (auto& line : removed) {
        line->instance->Stop();
    }
    removed.clear();
    for (Line* line : toReconfigure) {
        logger_.Info("Line '" + line->config.name + "' is idle; applying its new settings");
        ApplyLineConfig(*line, *line->pendingConfig);
    }
    for (Line* line : toReopen) {
        OpenLine(*line);
    }
    stats_.ObserveBusyLines(busy);

    const std::uint64_t cpu = ProcessCpuTime();
    const double cpuPercent = elapsed > 0 ? (cpu - lastCpuTime100ns_) / (elapsed * 1e7 * ProcessorCount()) * 100.0 : 0;
    lastCpuTime100ns_ = cpu;

    std::lock_guard<std::mutex> lock(infoMutex_);
    cpuPercent_ = std::min(100.0, std::max(0.0, cpuPercent));
    rtpKbps_ = elapsed > 0 ? rtpDeltaBytes * 8.0 / 1000.0 / elapsed : 0;
    if (jitterCount > 0) {
        lastJitterMeanMs_ = jitterSum / jitterCount;
        lastJitterMaxMs_ = jitterMax;
        lastLossPercent_ = received + lost > 0 ? 100.0 * lost / (received + lost) : 0;
    }
    if (now - lastPortsQuery_ >= kPortsQueryInterval) {
        ports_ = QuerySystemPorts();
        lastPortsQuery_ = now;
    }
}

bool Application::AnyCallActive() {
    std::lock_guard<std::mutex> lock(linesMutex_);
    return std::any_of(lines_.begin(), lines_.end(), [](const std::unique_ptr<Line>& l) { return l->instance->InCall(); });
}

// --------------------------------------------------------- control API

Application::Json Application::RunOnMain(std::function<Json()> task) {
    auto promise = std::make_shared<std::promise<Json>>();
    std::future<Json> result = promise->get_future();
    {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        if (g_stopRequested) {
            throw ControlError("the service is stopping");
        }
        tasks_.push_back([this, promise, task]() {
            try {
                if (g_stopRequested && !restartRequested_) {
                    throw ControlError("the service is stopping");
                }
                promise->set_value(task());
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });
    }
    tasksCv_.notify_one();
    if (result.wait_for(kMainTaskTimeout) != std::future_status::ready) {
        throw ControlError("the service did not answer in time");
    }
    return result.get();
}

Application::Json Application::Handle(const std::string& cmd, const Json& args) {
    if (cmd == "hello") return Hello();
    if (cmd == "status") return Status();
    if (cmd == "log") {
        const std::uint64_t after = args.contains("after") && args["after"].is_number_unsigned()
                                        ? args["after"].get<std::uint64_t>()
                                        : 0;
        const size_t max = args.contains("max") && args["max"].is_number_unsigned()
                               ? std::min<size_t>(args["max"].get<size_t>(), Logger::kMemoryLines)
                               : 500;
        Json entries = Json::array();
        for (const LogEntry& e : logger_.EntriesSince(after, max)) {
            entries.push_back({{"seq", e.seq}, {"time", e.time}, {"level", Logger::ToString(e.level)}, {"message", e.message}});
        }
        return {{"entries", entries}, {"last", logger_.LastSeq()}, {"file", logger_.FilePath()}};
    }
    if (cmd == "config.get") return RunOnMain([this] { return ConfigJson(); });
    if (cmd == "config.set") return RunOnMain([this, args] { return SetConfig(args); });
    if (cmd == "config.reload") {
        return RunOnMain([this] {
            logger_.Info("Reloading " + configPath_ + " (requested through the control API)");
            ApplyConfig(LoadConfigFile());
            return Json{{"restartRequired", restartRequired_.load()}};
        });
    }
    if (cmd == "line.add") return RunOnMain([this, args] { return AddLine(args); });
    if (cmd == "line.update") return RunOnMain([this, args] { return UpdateLine(args); });
    if (cmd == "line.remove") return RunOnMain([this, args] { return RemoveLine(args); });
    if (cmd == "line.answer") return RunOnMain([this, args] { return LineCommandRequest(args, LineCommand::Answer); });
    if (cmd == "line.hangup") return RunOnMain([this, args] { return LineCommandRequest(args, LineCommand::HangUp); });
    if (cmd == "line.reset") {
        return RunOnMain([this, args] {
            Line* line = FindLine(StringArg(args, "id"));
            if (line == nullptr) throw ControlError("no such line");
            logger_.Info("Resetting line '" + line->config.name + "' (requested through the control API)");
            ApplyLineConfig(*line, line->config);
            return Json::object();
        });
    }
    if (cmd == "line.service") return RunOnMain([this, args] { return SetInService(args); });
    if (cmd == "sip.register") {
        const bool reg = !args.contains("register") || !args["register"].is_boolean() || args["register"].get<bool>();
        return RunOnMain([this, reg] {
            logger_.Info(reg ? "Re-registering every line" : "Unregistering every line");
            for (auto& line : lines_) {
                line->instance->Post(reg ? LineCommand::Register : LineCommand::Unregister);
            }
            return Json::object();
        });
    }
    if (cmd == "selftest") {
        // Pure computation, no PJSIP: runs on this connection's thread.
        const std::string modulations = args.contains("modulations") && args["modulations"].is_string()
                                            ? args["modulations"].get<std::string>()
                                            : "V34";
        logger_.Info("Loopback self-test (" + modulations + ") started");
        const auto begin = std::chrono::steady_clock::now();
        const LoopbackResult r = RunModemLoopback(modulations);
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        logger_.Info(r.connected ? "Loopback self-test: connected " + r.modulation + " " + std::to_string(r.txBps) +
                                       "/" + std::to_string(r.rxBps)
                                 : "Loopback self-test FAILED: " + r.failure);
        return {{"connected", r.connected}, {"modulation", r.modulation}, {"txBps", r.txBps}, {"rxBps", r.rxBps},
                {"snrDb", r.snrDb}, {"lineSeconds", r.simulatedSeconds}, {"wallSeconds", wall},
                {"failure", r.failure}};
    }
    if (cmd == "system.ports") return SystemPortsJson();
    if (cmd == "service.stop") {
        logger_.Info("Stop requested through the control API");
        RequestStop();
        return {{"stopping", true}};
    }
    if (cmd == "service.restart") {
        const bool force = args.contains("force") && args["force"].is_boolean() && args["force"].get<bool>();
        return RunOnMain([this, force] {
            if (!force && AnyCallActive()) {
                throw ControlError("a line is in a call; restart anyway with force, or wait");
            }
            logger_.Info("Restart requested through the control API");
            restartRequested_ = true;
            RequestStop();
            return Json{{"restarting", true}};
        });
    }
    throw ControlError("unknown command: " + cmd);
}

Application::Json Application::Hello() {
#if defined(_WIN64)
    const char* platform = "Windows, 64-bit";
#elif defined(_WIN32)
    const char* platform = "Windows, 32-bit";
#else
    const char* platform = "POSIX";
#endif
    return {{"service", "VoIPModem"},
            {"version", VOIPMODEM_VERSION},
            {"host", HostName()},
            {"platform", platform},
            {"engine", "libv90modem " + ModemEngineVersion()},
            {"pjsip", pjsipVersion_},
            {"configPath", configPath_},
            {"runningAsService", runningAsService_}};
}

Application::Json Application::LineJson(const Line& line) const {
    const LineStatus s = line.instance->Status();
    Json j = {{"id", s.id},
              {"comPort", s.comPort},
              {"extension", s.extension},
              {"server", s.server},
              {"state", s.state},
              {"kind", s.kind},
              {"direction", s.direction},
              {"remoteParty", s.remoteParty},
              {"modulation", s.modulation},
              {"txBps", s.txBps},
              {"rxBps", s.rxBps},
              {"snrDb", s.snrDb},
              {"callSeconds", s.callSeconds},
              {"bytesTx", s.bytesTx},
              {"bytesRx", s.bytesRx},
              {"retrains", s.retrains},
              {"errors", s.errors},
              {"errorControl", s.errorControl},
              {"compression", s.compression},
              {"codec", s.codec},
              {"jitterMeanMs", s.jitterMeanMs},
              {"jitterMaxMs", s.jitterMaxMs},
              {"registration", s.registration},
              {"registrationCode", s.registrationCode},
              {"inService", s.inService},
              {"portOpen", s.portOpen},
              {"fault", s.fault},
              {"autoAnswerRings", s.autoAnswerRings},
              {"canAnswer", s.canAnswer},
              {"canHangUp", s.canHangUp},
              {"pendingChanges", line.pendingConfig.has_value() || line.pendingRemoval}};
    return j;
}

Application::Json Application::Status() {
    Json lines = Json::array();
    int registered = 0;
    int sipLines = 0;
    std::vector<std::pair<std::string, std::string>> ports; // line id, COM port
    {
        std::lock_guard<std::mutex> lock(linesMutex_);
        for (const auto& line : lines_) {
            Json j = LineJson(*line);
            sipLines += j["registration"] != "none" ? 1 : 0;
            registered += j["registration"] == "registered" ? 1 : 0;
            lines.push_back(std::move(j));
        }
    }

    const ServiceStats::Snapshot st = stats_.Get();
    Json carriers = Json::array();
    unsigned long totalConnects = 0;
    for (const auto& entry : st.connectsByMode) totalConnects += entry.second;
    std::vector<std::pair<std::string, unsigned long>> modes(st.connectsByMode.begin(), st.connectsByMode.end());
    std::sort(modes.begin(), modes.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (const auto& m : modes) {
        carriers.push_back({{"mode", m.first}, {"count", m.second}});
    }

    const auto now = std::chrono::system_clock::now();
    Json result;
    {
        std::lock_guard<std::mutex> lock(infoMutex_);
        for (auto& l : lines) {
            l["driver"] = DriverBinding(ports_, l["comPort"].get<std::string>());
        }
        result["service"] = {
            {"host", HostName()},
            {"version", VOIPMODEM_VERSION},
            {"startedAt", IsoTime(startedAt_)},
            {"uptimeSeconds", std::chrono::duration_cast<std::chrono::seconds>(now - startedAt_).count()},
            {"configPath", configPath_},
            {"configLoadedAt", IsoTime(configLoadedAt_)},
            {"logFile", logger_.FilePath()},
            {"cpuPercent", cpuPercent_},
            {"runningAsService", runningAsService_},
            {"restartRequired", restartRequired_.load()}};
        result["media"] = {{"preferredCodec", startConfig_.preferredCodec},
                           {"ptimeMs", 20},
                           {"jitterMeanMs", lastJitterMeanMs_},
                           {"jitterMaxMs", lastJitterMaxMs_},
                           {"lossPercent", lastLossPercent_},
                           {"rtpKbps", rtpKbps_}};
        result["defaults"] = {{"autoAnswerRings", lineDefaults_.autoAnswerRingCount},
                              {"modulations", lineDefaults_.modulations},
                              {"errorControl", lineDefaults_.errorControl},
                              {"compression", lineDefaults_.compression}};
    }
    result["sip"] = {{"enabled", sipEndpoint_ != nullptr},
                     {"localPort", startConfig_.sipLocalPort},
                     {"transport", startConfig_.sipTransport},
                     {"server", startConfig_.sipServer},
                     {"registered", registered},
                     {"lines", sipLines}};
    result["lines"] = std::move(lines);
    result["stats"] = {{"incomingOffered", st.incomingOffered},
                       {"callsAnswered", st.callsAnswered},
                       {"trained", st.trained},
                       {"failedToTrain", st.failedToTrain},
                       {"rejectedBusy", st.rejectedBusy},
                       {"hungUpEarly", st.hungUpEarly},
                       {"outgoingDialled", st.outgoingDialled},
                       {"outgoingConnected", st.outgoingConnected},
                       {"completedCalls", st.completedCalls},
                       {"averageCallSeconds", st.completedCalls ? st.totalConnectedSeconds / st.completedCalls : 0.0},
                       {"longestCallSeconds", st.longestCallSeconds},
                       {"peakBusyLines", st.peakBusyLines},
                       {"bytesIn", st.bytesIn},
                       {"bytesOut", st.bytesOut},
                       {"atCommands", st.atCommands},
                       {"retrains", st.retrains},
                       {"renegotiations", st.renegotiations},
                       {"connects", totalConnects}};
    result["carriers"] = std::move(carriers);
    return result;
}

Application::Json Application::SystemPortsJson() {
    const SystemPorts ports = QuerySystemPorts();
    std::map<std::string, std::string> usedBy;
    {
        std::lock_guard<std::mutex> lock(linesMutex_);
        for (const auto& line : lines_) {
            usedBy[LowerCase(line->config.comPort)] = line->config.name.empty() ? "(Modem)" : line->config.name;
        }
    }
    auto lineOf = [&usedBy](const std::string& port) {
        const auto it = usedBy.find(LowerCase(port));
        return it == usedBy.end() ? std::string() : it->second;
    };
    Json serial = Json::array();
    for (const std::string& p : ports.serialPorts) {
        serial.push_back({{"port", p}, {"line", lineOf(p)}});
    }
    Json pairs = Json::array();
    for (const PortPair& p : ports.com0comPairs) {
        pairs.push_back({{"a", p.a}, {"b", p.b}, {"lineA", lineOf(p.a)}, {"lineB", lineOf(p.b)}});
    }
    Json modems = Json::array();
    for (const ModemDriver& m : ports.modems) {
        modems.push_back({{"name", m.name}, {"attachedTo", m.attachedTo}, {"pairedWith", PairedPort(ports, m.attachedTo)},
                          {"line", lineOf(PairedPort(ports, m.attachedTo))}});
    }
    return {{"serialPorts", serial}, {"com0comPairs", pairs}, {"modems", modems}};
}

Application::Json Application::ConfigJson() {
    IniConfig ini;
    ini.LoadFromFile(configPath_);
    const AppConfig c = LoadConfigFile();
    const ModemInstanceConfig d = LoadLineDefaults(ini);
    Json lines = Json::array();
    for (const ModemInstanceConfig& m : c.modems) {
        lines.push_back(LineConfigJson(m));
    }
    return {{"path", configPath_},
            {"sip",
             {{"server", c.sipServer},
              {"localPort", c.sipLocalPort},
              {"transport", c.sipTransport},
              {"realm", c.sipRealm},
              {"registerExpiry", c.sipRegisterExpiry},
              {"registerOnStart", c.sipRegisterOnStart}}},
            {"media",
             {{"codec", c.preferredCodec},
              {"jitterBufferMs", c.jitterBufferMs},
              {"rtpPort", c.rtpPort},
              {"rtpPortRange", c.rtpPortRange},
              {"echoCanceller", c.echoCanceller}}},
            {"defaults",
             {{"modulations", d.modulations},
              {"autoAnswerRings", d.autoAnswerRingCount},
              {"errorControl", d.errorControl},
              {"compression", d.compression},
              {"baudRate", d.baudRate},
              {"identification", d.modemIdentification}}},
            {"logging",
             {{"level", Logger::ToString(c.logLevel)},
              {"file", ini.GetString("Logging", "File", "voipmodem.log")},
              {"rotateKB", c.logRotateKB},
              {"keep", c.logKeepFiles},
              {"traceAt", c.traceAtCommands}}},
            {"control",
             {{"port", c.controlPort}, {"bindAddress", c.controlBindAddress}, {"passwordSet", !c.controlPassword.empty()}}},
            {"lines", lines},
            {"restartRequired", restartRequired_.load()}};
}

Application::Json Application::EditConfig(const std::function<void(IniDocument&)>& edit) {
    IniDocument doc;
    doc.LoadFromFile(configPath_); // a missing file starts empty
    edit(doc);
    if (!doc.SaveToFile(configPath_)) {
        throw ControlError("cannot write " + configPath_ + " (read-only, or no permission)");
    }
    ApplyConfig(LoadConfigFile());
    return {{"restartRequired", restartRequired_.load()}};
}

Application::Json Application::SetConfig(const Json& args) {
    struct Key {
        const char* group;
        const char* field;
        const char* section;
        const char* key;
    };
    static const Key kKeys[] = {
        {"sip", "server", "SIP", "Server"},
        {"sip", "localPort", "SIP", "LocalPort"},
        {"sip", "transport", "SIP", "Transport"},
        {"sip", "realm", "SIP", "Realm"},
        {"sip", "registerExpiry", "SIP", "RegisterExpiry"},
        {"sip", "registerOnStart", "SIP", "RegisterOnStart"},
        {"media", "codec", "Media", "Codec"},
        {"media", "jitterBufferMs", "Media", "JitterBufferMs"},
        {"media", "rtpPort", "Media", "RtpPort"},
        {"media", "rtpPortRange", "Media", "RtpPortRange"},
        {"media", "echoCanceller", "Media", "EchoCanceller"},
        {"defaults", "modulations", "Defaults", "Modulations"},
        {"defaults", "autoAnswerRings", "Defaults", "AutoAnswerRingCount"},
        {"defaults", "errorControl", "Defaults", "ErrorControl"},
        {"defaults", "compression", "Defaults", "Compression"},
        {"defaults", "baudRate", "Defaults", "BaudRate"},
        {"defaults", "identification", "Defaults", "Identification"},
        {"logging", "level", "Logging", "Level"},
        {"logging", "file", "Logging", "File"},
        {"logging", "rotateKB", "Logging", "RotateKB"},
        {"logging", "keep", "Logging", "Keep"},
        {"logging", "traceAt", "Logging", "TraceAT"},
        {"control", "port", "Control", "Port"},
        {"control", "bindAddress", "Control", "BindAddress"},
    };

    // Validate everything before writing anything.
    std::vector<std::pair<const Key*, std::string>> writes;
    for (const Key& k : kKeys) {
        if (!args.contains(k.group) || !args[k.group].is_object() || !args[k.group].contains(k.field)) {
            continue;
        }
        const Json& v = args[k.group][k.field];
        const std::string field = k.field;
        if (field == "localPort" || field == "port") IntArg(v, k.field, 0, 65535);
        if (field == "rtpPort") IntArg(v, k.field, 0, 65535);
        if (field == "rtpPortRange") IntArg(v, k.field, 0, 60000);
        if (field == "registerExpiry") IntArg(v, k.field, 30, 86400);
        if (field == "jitterBufferMs") IntArg(v, k.field, 20, 2000);
        if (field == "autoAnswerRings") IntArg(v, k.field, 0, 255);
        if (field == "rotateKB") IntArg(v, k.field, 0, 1 << 20);
        if (field == "keep") IntArg(v, k.field, 1, 100);
        if (field == "transport" && v != "UDP" && v != "TCP") throw ControlError("transport must be UDP or TCP");
        if (field == "codec" && v != "PCMU" && v != "PCMA") throw ControlError("codec must be PCMU or PCMA");
        if (field == "level" && v != "Error" && v != "Warn" && v != "Info" && v != "Debug") {
            throw ControlError("level must be Error, Warn, Info or Debug");
        }
        writes.emplace_back(&k, IniValue(v));
    }
    std::string newPassword;
    bool clearPassword = false;
    if (args.contains("control") && args["control"].is_object()) {
        const Json& c = args["control"];
        if (c.contains("password") && c["password"].is_string() && !c["password"].get<std::string>().empty()) {
            newPassword = IniValue(c["password"]);
        }
        clearPassword = c.contains("clearPassword") && c["clearPassword"].is_boolean() && c["clearPassword"].get<bool>();
    }

    logger_.Info("Saving settings to " + configPath_ + " (requested through the control API)");
    return EditConfig([&](IniDocument& doc) {
        for (const auto& w : writes) {
            doc.Set(w.first->section, w.first->key, w.second);
        }
        if (!newPassword.empty()) {
            doc.Set("Control", "Password", newPassword);
        } else if (clearPassword) {
            doc.Remove("Control", "Password");
        }
    });
}

// Writes the keys `line` carries to `section`. A modem setting equal to
// the line default is removed instead, so the line keeps following
// [Defaults]; the password is only written when one is given.
void Application::WriteLineKeys(IniDocument& doc, const std::string& section, const Json& line, bool isNew) {
    IniConfig current;
    current.LoadFromFile(configPath_);
    const ModemInstanceConfig defaults = LoadLineDefaults(current);
    struct Field {
        const char* json;
        const char* key;
        std::string defaultValue; // empty: always written
    };
    const Field fields[] = {
        {"comPort", "ComPort", ""},
        {"sipExtension", "SipExtension", ""},
        {"baudRate", "BaudRate", std::to_string(defaults.baudRate)},
        {"identification", "Identification", defaults.modemIdentification},
        {"autoAnswerRings", "AutoAnswerRingCount", std::to_string(defaults.autoAnswerRingCount)},
        {"modulations", "Modulations", defaults.modulations.empty() ? std::string("\x01") : defaults.modulations},
        {"errorControl", "ErrorControl", defaults.errorControl ? "1" : "0"},
        {"compression", "Compression", defaults.compression ? "1" : "0"},
        {"inService", "InService", "1"},
        {"localAudio", "EnableLocalAudio", "0"},
        {"sipServer", "SipServer", defaults.sipServer.empty() ? std::string("\x01") : defaults.sipServer},
        {"sipRealm", "SipRealm", defaults.sipRealm},
    };
    for (const Field& f : fields) {
        if (!line.contains(f.json)) {
            continue;
        }
        const std::string value = IniValue(line[f.json]);
        if (f.defaultValue.empty()) {
            if (value.empty()) {
                throw ControlError(std::string(f.json) + " cannot be empty");
            }
            doc.Set(section, f.key, value);
        } else if (value == f.defaultValue || (f.defaultValue == "\x01" && value.empty())) {
            doc.Remove(section, f.key);
        } else {
            doc.Set(section, f.key, value);
        }
    }
    if (line.contains("sipPassword") && line["sipPassword"].is_string() &&
        !line["sipPassword"].get<std::string>().empty()) {
        doc.Set(section, "SipPassword", IniValue(line["sipPassword"]));
    } else if (isNew) {
        // nothing to write: a line without a password registers unauthenticated
    }
}

Application::Json Application::AddLine(const Json& args) {
    if (!args.contains("line") || !args["line"].is_object()) {
        throw ControlError("missing \"line\"");
    }
    Json line = args["line"];
    const std::string comPort = line.contains("comPort") && line["comPort"].is_string() ? line["comPort"].get<std::string>() : "";
    if (comPort.empty()) {
        throw ControlError("a line needs a COM port");
    }
    std::string id = line.contains("id") && line["id"].is_string() ? line["id"].get<std::string>() : "";
    for (const auto& existing : lines_) {
        if (LowerCase(existing->config.comPort) == LowerCase(comPort) && !existing->pendingRemoval) {
            throw ControlError(comPort + " is already used by line '" + existing->config.name + "'");
        }
        const std::string ext = line.contains("sipExtension") && line["sipExtension"].is_string()
                                    ? line["sipExtension"].get<std::string>()
                                    : "";
        if (!ext.empty() && existing->config.sipExtension == ext && !existing->pendingRemoval) {
            throw ControlError("extension " + ext + " is already used by line '" + existing->config.name + "'");
        }
    }
    if (id.empty()) {
        for (int n = 1;; ++n) {
            id = "Line" + std::to_string(n);
            if (FindLine(id) == nullptr) break;
        }
    } else if (!ValidLineName(id)) {
        throw ControlError("a line name may only have letters, digits, '-' and '_' (at most 32)");
    } else if (FindLine(id) != nullptr) {
        throw ControlError("there is already a line named '" + id + "'");
    }
    if (!line.contains("sipExtension")) {
        throw ControlError("a line needs a SIP extension");
    }
    logger_.Info("Adding line '" + id + "' on " + comPort + " (requested through the control API)");
    Json result = EditConfig([&](IniDocument& doc) {
        WriteLineKeys(doc, ModemSectionName(id), line, true);
    });
    result["id"] = id;
    return result;
}

Application::Json Application::UpdateLine(const Json& args) {
    const std::string id = StringArg(args, "id");
    Line* existing = FindLine(id);
    if (existing == nullptr) throw ControlError("no such line");
    if (!args.contains("line") || !args["line"].is_object()) throw ControlError("missing \"line\"");
    const Json& line = args["line"];
    if (line.contains("comPort") && line["comPort"].is_string()) {
        const std::string port = line["comPort"].get<std::string>();
        for (const auto& other : lines_) {
            if (other.get() != existing && LowerCase(other->config.comPort) == LowerCase(port)) {
                throw ControlError(port + " is already used by line '" + other->config.name + "'");
            }
        }
    }
    logger_.Info("Updating line '" + existing->config.name + "' (requested through the control API)");
    const std::string section = ModemSectionName(existing->config.name);
    Json result = EditConfig([&](IniDocument& doc) { WriteLineKeys(doc, section, line, false); });
    result["deferred"] = FindLine(id) != nullptr && FindLine(id)->pendingConfig.has_value();
    return result;
}

Application::Json Application::RemoveLine(const Json& args) {
    const std::string id = StringArg(args, "id");
    Line* line = FindLine(id);
    if (line == nullptr) throw ControlError("no such line");
    if (lines_.size() == 1) {
        throw ControlError("the last line cannot be removed; change it instead");
    }
    const bool force = args.contains("force") && args["force"].is_boolean() && args["force"].get<bool>();
    if (line->instance->InCall()) {
        if (!force) {
            throw ControlError("line '" + id + "' is in a call; remove it anyway with force, or wait");
        }
        line->instance->Post(LineCommand::HangUp);
    }
    logger_.Info("Removing line '" + line->config.name + "' from " + configPath_ + " (requested through the control API)");
    const std::string section = ModemSectionName(line->config.name);
    return EditConfig([&](IniDocument& doc) { doc.RemoveSection(section); });
}

Application::Json Application::LineCommandRequest(const Json& args, LineCommand command) {
    Line* line = FindLine(StringArg(args, "id"));
    if (line == nullptr) throw ControlError("no such line");
    if (!line->instance->IsRunning()) throw ControlError("the line is not running (its COM port is closed)");
    line->instance->Post(command);
    return Json::object();
}

// Persisted (InService= in the ini), and applied at once: a line taken
// out of service hangs up and unregisters.
Application::Json Application::SetInService(const Json& args) {
    if (!args.contains("inService") || !args["inService"].is_boolean()) {
        throw ControlError("missing \"inService\" (true or false)");
    }
    const bool inService = args["inService"].get<bool>();
    std::vector<std::string> sections;
    if (args.contains("id")) {
        Line* line = FindLine(StringArg(args, "id"));
        if (line == nullptr) throw ControlError("no such line");
        sections.push_back(ModemSectionName(line->config.name));
    } else {
        for (const auto& line : lines_) {
            sections.push_back(ModemSectionName(line->config.name));
        }
    }
    return EditConfig([&](IniDocument& doc) {
        for (const std::string& section : sections) {
            if (inService) {
                doc.Remove(section, "InService");
            } else {
                doc.Set(section, "InService", "0");
            }
        }
    });
}

} // namespace voipmodem
