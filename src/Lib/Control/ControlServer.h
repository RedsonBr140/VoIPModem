#pragma once

#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "../Logging/Logger.h"

namespace voipmodem {

// A request the handler refuses (bad arguments, unknown line, ...): the
// client gets {"ok":false,"error":<what()>}.
class ControlError : public std::runtime_error {
public:
    explicit ControlError(const std::string& message) : std::runtime_error(message) {}
};

// The control API's transport (docs/CONTROL_API.md): a TCP listener; each
// connection carries one JSON object per line each way.
//
//   request:  {"id": 7, "cmd": "status", "args": {...}}
//   response: {"id": 7, "ok": true, "result": ...}
//             {"id": 7, "ok": false, "error": "..."}
//
// When a password is set, a connection must first send
// {"cmd":"hello","args":{"password":...}}; anything else before that is
// refused. Each connection runs on its own thread, so the handler must be
// thread-safe. Plain BSD sockets (Winsock on Windows), no Vista+ calls.
class ControlServer {
public:
    using Json = nlohmann::json;
    // (cmd, args) -> result. Throws ControlError to refuse.
    using Handler = std::function<Json(const std::string& cmd, const Json& args)>;

    explicit ControlServer(Logger& logger);
    ~ControlServer();

    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    // Binds `bindAddress` (IPv4 dotted quad; "0.0.0.0" = every interface)
    // and `port` (0 = any free port, see Port()). Returns false and logs
    // why on failure.
    bool Start(const std::string& bindAddress, int port, const std::string& password, Handler handler);

    // Closes the listener and every connection, joining their threads.
    void Stop();

    int Port() const { return boundPort_; }

    static constexpr int kProtocolVersion = 1;
    static constexpr size_t kMaxLineBytes = 1 << 20;
    static constexpr int kMaxClients = 16;

private:
    struct Client;
    void AcceptLoop();
    void ClientLoop(Client* client);
    std::string HandleLine(const std::string& line, bool& authenticated);
    void ReapFinishedClients();

    Logger& logger_;
    std::string password_;
    Handler handler_;
    std::atomic<bool> stop_{false};
    std::uintptr_t listenSocket_;
    int boundPort_ = 0;
    std::thread acceptThread_;
    std::mutex clientsMutex_;
    std::list<std::unique_ptr<Client>> clients_;
    bool winsockStarted_ = false;
};

} // namespace voipmodem
