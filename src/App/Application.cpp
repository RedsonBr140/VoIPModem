#include "Application.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "../Lib/Config/IniConfig.h"

namespace voipmodem {

namespace {

std::atomic<bool> g_stopRequested{false};

AppConfig LoadConfig(const std::string& path) {
    IniConfig ini;
    ini.LoadFromFile(path); // missing/unreadable file just leaves defaults in place
    return LoadAppConfig(ini);
}

} // namespace

void RequestStop() {
    g_stopRequested = true;
}

Application::Application(std::string configPath)
    : configPath_(std::move(configPath)), config_(LoadConfig(configPath_)) {}

int Application::Run() {
    logger_.Init(config_.logLevel, config_.logFilePath);
    logger_.Info("VoIPModem starting (Phase 2: Hayes command skeleton + SIP call control)");

    if (config_.sipLocalPort > 0) {
        auto endpoint = std::make_unique<PjsipEndpoint>(logger_);
        if (endpoint->Start(config_.sipLocalPort)) {
            sipEndpoint_ = std::move(endpoint);
        } else {
            logger_.Error("SIP transport failed to start; every modem instance will use "
                           "NullCallController regardless of its own SipServer setting.");
        }
    }

    for (const ModemInstanceConfig& instanceConfig : config_.modems) {
        auto instance = std::make_unique<ModemInstance>(instanceConfig, logger_, sipEndpoint_.get());
        if (!instance->Open()) {
            const std::string label = instanceConfig.name.empty() ? instanceConfig.comPort
                                                                    : instanceConfig.name + "/" + instanceConfig.comPort;
            logger_.Error("Failed to open modem instance '" + label + "'; skipping it.");
            continue; // one bad port shouldn't take down the others
        }
        instances_.push_back(std::move(instance));
    }

    if (instances_.empty()) {
        logger_.Error("No modem instance could be opened; exiting.");
        return 1;
    }

    for (auto& instance : instances_) {
        instance->Start();
    }

    logger_.Info(std::to_string(instances_.size()) + " modem instance(s) running. Press Ctrl+C to stop.");

    while (!g_stopRequested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    logger_.Info("VoIPModem shutting down");
    for (auto& instance : instances_) {
        instance->Stop();
    }
    return 0;
}

} // namespace voipmodem
