#include "ControlServer.h"

#ifdef _WIN32
// inet_addr / inet_ntoa on purpose: inet_pton / inet_ntop do not exist on
// Windows XP and Server 2003.
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using SocketHandle = SOCKET;
static const SocketHandle kNoSocket = INVALID_SOCKET;
static void CloseSocket(SocketHandle s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
static const SocketHandle kNoSocket = -1;
static void CloseSocket(SocketHandle s) { close(s); }
#endif

namespace voipmodem {

namespace {

SocketHandle AsSocket(std::uintptr_t s) { return static_cast<SocketHandle>(s); }

// Waits up to `ms` for `s` to become readable; false on timeout.
bool WaitReadable(SocketHandle s, int ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return select(static_cast<int>(s) + 1, &set, nullptr, nullptr, &tv) > 0;
}

bool SendAll(SocketHandle s, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const int n = send(s, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

// Constant time in the length of `given`, so the password's content is
// not revealed by how long a refusal takes.
bool SamePassword(const std::string& expected, const std::string& given) {
    unsigned char diff = expected.size() == given.size() ? 0 : 1;
    for (size_t i = 0; i < given.size(); ++i) {
        diff |= static_cast<unsigned char>(given[i] ^ (i < expected.size() ? expected[i] : 0));
    }
    return diff == 0;
}

} // namespace

struct ControlServer::Client {
    std::uintptr_t socket = 0;
    std::string peer;
    std::thread thread;
    std::atomic<bool> finished{false};
};

ControlServer::ControlServer(Logger& logger)
    : logger_(logger), listenSocket_(static_cast<std::uintptr_t>(kNoSocket)) {}

ControlServer::~ControlServer() {
    Stop();
}

bool ControlServer::Start(const std::string& bindAddress, int port, const std::string& password, Handler handler) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        logger_.Error("Control API: WSAStartup failed");
        return false;
    }
    winsockStarted_ = true;
#endif
    password_ = password;
    handler_ = std::move(handler);
    stop_ = false;

    SocketHandle s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kNoSocket) {
        logger_.Error("Control API: cannot create a socket");
        return false;
    }
#ifdef _WIN32
    BOOL exclusive = TRUE; // nobody else may bind the same port while we have it
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof exclusive);
#else
    int reuse = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr = inet_addr(bindAddress.c_str()); // inet_pton is Vista+
    if (addr.sin_addr.s_addr == INADDR_NONE && bindAddress != "255.255.255.255") {
        logger_.Error("Control API: BindAddress '" + bindAddress + "' is not an IPv4 address");
        CloseSocket(s);
        return false;
    }
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(s, 8) != 0) {
        logger_.Error("Control API: cannot listen on " + bindAddress + ":" + std::to_string(port) +
                      " (in use, or not an address of this machine)");
        CloseSocket(s);
        return false;
    }
    socklen_t len = sizeof addr;
    getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
    boundPort_ = ntohs(addr.sin_port);
    listenSocket_ = static_cast<std::uintptr_t>(s);

    acceptThread_ = std::thread(&ControlServer::AcceptLoop, this);
    logger_.Info("Control API listening on " + bindAddress + ":" + std::to_string(boundPort_) +
                 (password_.empty() ? " (no password)" : " (password required)"));
    return true;
}

void ControlServer::Stop() {
    stop_ = true;
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
    if (AsSocket(listenSocket_) != kNoSocket) {
        CloseSocket(AsSocket(listenSocket_));
        listenSocket_ = static_cast<std::uintptr_t>(kNoSocket);
    }
    std::list<std::unique_ptr<Client>> clients;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients.swap(clients_);
    }
    for (auto& client : clients) {
        if (client->thread.joinable()) {
            client->thread.join(); // each loop notices stop_ within its select timeout
        }
    }
#ifdef _WIN32
    if (winsockStarted_) {
        WSACleanup();
        winsockStarted_ = false;
    }
#endif
}

void ControlServer::ReapFinishedClients() {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto it = clients_.begin(); it != clients_.end();) {
        if ((*it)->finished) {
            if ((*it)->thread.joinable()) {
                (*it)->thread.join();
            }
            it = clients_.erase(it);
        } else {
            ++it;
        }
    }
}

void ControlServer::AcceptLoop() {
    const SocketHandle listener = AsSocket(listenSocket_);
    while (!stop_) {
        ReapFinishedClients();
        if (!WaitReadable(listener, 250)) {
            continue;
        }
        sockaddr_in peer{};
        socklen_t len = sizeof peer;
        SocketHandle s = accept(listener, reinterpret_cast<sockaddr*>(&peer), &len);
        if (s == kNoSocket) {
            continue;
        }
        std::lock_guard<std::mutex> lock(clientsMutex_);
        if (static_cast<int>(clients_.size()) >= kMaxClients) {
            CloseSocket(s);
            continue;
        }
        auto client = std::make_unique<Client>();
        client->socket = static_cast<std::uintptr_t>(s);
        client->peer = std::string(inet_ntoa(peer.sin_addr)) + ":" + std::to_string(ntohs(peer.sin_port));
        Client* raw = client.get();
        clients_.push_back(std::move(client));
        raw->thread = std::thread(&ControlServer::ClientLoop, this, raw);
    }
}

void ControlServer::ClientLoop(Client* client) {
    const SocketHandle s = AsSocket(client->socket);
    logger_.Debug("Control API: client connected from " + client->peer);
    bool authenticated = password_.empty();
    std::string buffer;
    char chunk[4096];
    while (!stop_) {
        if (!WaitReadable(s, 250)) {
            continue;
        }
        const int n = recv(s, chunk, sizeof chunk, 0);
        if (n <= 0) {
            break;
        }
        buffer.append(chunk, static_cast<size_t>(n));
        size_t newline;
        bool ok = true;
        while ((newline = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, newline);
            buffer.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }
            if (!SendAll(s, HandleLine(line, authenticated) + "\n")) {
                ok = false;
                break;
            }
        }
        if (!ok || buffer.size() > kMaxLineBytes) {
            break;
        }
    }
    CloseSocket(s);
    logger_.Debug("Control API: client " + client->peer + " disconnected");
    client->finished = true;
}

std::string ControlServer::HandleLine(const std::string& line, bool& authenticated) {
    Json response;
    Json request;
    try {
        request = Json::parse(line);
    } catch (const std::exception&) {
        response["ok"] = false;
        response["error"] = "not a JSON object";
        return response.dump();
    }
    if (request.is_object() && request.contains("id")) {
        response["id"] = request["id"];
    }
    try {
        if (!request.is_object() || !request.contains("cmd") || !request["cmd"].is_string()) {
            throw ControlError("a request needs a \"cmd\" string");
        }
        const std::string cmd = request["cmd"].get<std::string>();
        const Json args = request.contains("args") && request["args"].is_object() ? request["args"] : Json::object();

        if (cmd == "hello") {
            if (!password_.empty()) {
                const std::string given = args.contains("password") && args["password"].is_string()
                                              ? args["password"].get<std::string>()
                                              : std::string();
                authenticated = SamePassword(password_, given);
                if (!authenticated) {
                    logger_.Warn("Control API: wrong password from a client");
                    throw ControlError("wrong password");
                }
            }
        } else if (!authenticated) {
            throw ControlError("authentication required: send hello with the password first");
        }

        Json result = handler_(cmd, args);
        if (cmd == "hello") {
            result["protocol"] = kProtocolVersion;
            result["passwordRequired"] = !password_.empty();
        }
        response["ok"] = true;
        response["result"] = std::move(result);
    } catch (const ControlError& e) {
        response["ok"] = false;
        response["error"] = e.what();
    } catch (const std::exception& e) {
        response["ok"] = false;
        response["error"] = std::string("internal error: ") + e.what();
    }
    // Invalid UTF-8 from a COM port name or a log line must not throw here.
    return response.dump(-1, ' ', false, Json::error_handler_t::replace);
}

} // namespace voipmodem
