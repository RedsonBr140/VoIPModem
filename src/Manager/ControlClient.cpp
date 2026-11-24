// inet_addr / gethostbyname on purpose: their replacements are Vista+.
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>

#include "ControlClient.h"

namespace manager {

namespace {

constexpr int kReplyTimeoutMs = 40000; // the service waits up to 30 s on its main thread

} // namespace

ControlClient::ControlClient() : socket_(INVALID_SOCKET) {
    WSADATA wsa;
    winsock_ = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    thread_ = std::thread(&ControlClient::Run, this);
}

ControlClient::~ControlClient() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        queue_.clear();
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    CloseSocket();
    if (winsock_) {
        WSACleanup();
    }
}

void ControlClient::SetTarget(const std::string& host, int port, const std::string& password) {
    std::lock_guard<std::mutex> lock(mutex_);
    host_ = host;
    port_ = port;
    password_ = password;
    targetChanged_ = true;
    disconnected_ = false;
}

void ControlClient::Disconnect() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        disconnected_ = true;
        queue_.push_back(Item{nullptr, "", Json(), 0}); // wakes the worker to close the socket
    }
    cv_.notify_one();
}

void ControlClient::Request(HWND hwnd, const std::string& cmd, const Json& args, WPARAM tag) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(Item{hwnd, cmd, args, tag});
    }
    cv_.notify_one();
}

size_t ControlClient::Pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size() + (busy_ ? 1 : 0);
}

void ControlClient::CloseSocket() {
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    buffer_.clear();
}

bool ControlClient::Exchange(const std::string& line, std::string& reply) {
    const std::string data = line + "\n";
    size_t sent = 0;
    while (sent < data.size()) {
        const int n = send(socket_, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    for (;;) {
        const size_t newline = buffer_.find('\n');
        if (newline != std::string::npos) {
            reply = buffer_.substr(0, newline);
            buffer_.erase(0, newline + 1);
            return true;
        }
        char chunk[8192];
        const int n = recv(socket_, chunk, sizeof chunk, 0);
        if (n <= 0) return false;
        buffer_.append(chunk, static_cast<size_t>(n));
    }
}

// Connects (and says hello) if not connected. Posts the outcome to
// `notify` when it changes.
bool ControlClient::EnsureConnected(HWND notify) {
    std::string host, password;
    int port;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (targetChanged_) {
            targetChanged_ = false;
            CloseSocket();
            if (connected_) {
                connected_ = false;
                auto* info = new ConnectionInfo;
                info->error = "Disconnected";
                PostMessage(notify, WM_APP_CONNECTION, 0, reinterpret_cast<LPARAM>(info));
            }
        }
        host = host_;
        port = port_;
        password = password_;
        if (disconnected_) {
            return false;
        }
    }
    if (socket_ != INVALID_SOCKET) {
        return true;
    }

    auto fail = [&](const std::string& why, bool passwordRejected = false) {
        CloseSocket();
        const bool wasConnected = connected_;
        connected_ = false;
        auto* info = new ConnectionInfo;
        info->error = why;
        info->passwordRejected = passwordRejected;
        PostMessage(notify, WM_APP_CONNECTION, wasConnected ? 1 : 0, reinterpret_cast<LPARAM>(info));
        return false;
    };

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        hostent* he = gethostbyname(host.c_str());
        if (he == nullptr || he->h_addrtype != AF_INET) {
            return fail("Cannot find the computer \"" + host + "\"");
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof addr.sin_addr);
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return fail("Cannot create a socket");
    }
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        closesocket(s);
        return fail("The VoIPModem service is not answering on " + host + ":" + std::to_string(port));
    }
    DWORD timeout = kReplyTimeoutMs;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
    socket_ = s;

    Json hello = {{"id", nextId_++}, {"cmd", "hello"}, {"args", {{"client", "VoIPModem Manager"}}}};
    if (!password.empty()) {
        hello["args"]["password"] = password;
    }
    std::string reply;
    if (!Exchange(hello.dump(), reply)) {
        return fail("The connection closed during the greeting");
    }
    Json answer;
    try {
        answer = Json::parse(reply);
    } catch (const std::exception&) {
        return fail("The service sent something that is not JSON");
    }
    if (!answer.value("ok", false)) {
        const std::string error = answer.value("error", std::string("refused"));
        return fail(error, error == "wrong password");
    }
    connected_ = true;
    auto* info = new ConnectionInfo;
    info->connected = true;
    info->hello = answer["result"];
    PostMessage(notify, WM_APP_CONNECTION, 1, reinterpret_cast<LPARAM>(info));
    return true;
}

void ControlClient::Run() {
    for (;;) {
        Item item;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            item = std::move(queue_.front());
            queue_.pop_front();
            busy_ = true;
        }
        if (item.cmd.empty()) { // Disconnect()
            CloseSocket();
            connected_ = false;
            std::lock_guard<std::mutex> lock(mutex_);
            busy_ = false;
            continue;
        }

        auto* response = new Response;
        response->cmd = item.cmd;
        if (!EnsureConnected(item.hwnd)) {
            response->error = "not connected";
            // Requests queued behind this one would fail the same way.
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.clear();
        } else {
            Json request = {{"id", nextId_++}, {"cmd", item.cmd}, {"args", item.args}};
            std::string reply;
            if (!Exchange(request.dump(), reply)) {
                CloseSocket();
                connected_ = false;
                auto* info = new ConnectionInfo;
                info->error = "The connection to the service was lost";
                PostMessage(item.hwnd, WM_APP_CONNECTION, 1, reinterpret_cast<LPARAM>(info));
                response->error = "connection lost";
            } else {
                try {
                    const Json answer = Json::parse(reply);
                    response->ok = answer.value("ok", false);
                    if (response->ok) {
                        response->result = answer.contains("result") ? answer["result"] : Json::object();
                    } else {
                        response->error = answer.value("error", std::string("refused"));
                    }
                } catch (const std::exception&) {
                    response->error = "the answer is not JSON";
                }
            }
        }
        if (!PostMessage(item.hwnd, WM_APP_RESPONSE, item.tag, reinterpret_cast<LPARAM>(response))) {
            delete response; // the window is gone
        }
        std::lock_guard<std::mutex> lock(mutex_);
        busy_ = false;
    }
}

} // namespace manager
