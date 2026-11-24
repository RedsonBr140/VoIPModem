#pragma once

#include <windows.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

namespace manager {

using Json = nlohmann::json;

// One answer from the service, posted to the window that asked:
// PostMessage(hwnd, WM_APP_RESPONSE, tag, (LPARAM)new Response). The
// receiver deletes it.
struct Response {
    std::string cmd;
    bool ok = false;
    Json result;
    std::string error;
};

// Connection changes: PostMessage(hwnd, WM_APP_CONNECTION, connected,
// (LPARAM)new ConnectionInfo). The receiver deletes it.
struct ConnectionInfo {
    bool connected = false;
    Json hello;            // the service's "hello" answer when connected
    std::string error;     // why not, when not
    bool passwordRejected = false;
};

constexpr UINT WM_APP_RESPONSE = WM_APP + 1;
constexpr UINT WM_APP_CONNECTION = WM_APP + 2;

// The Manager's end of the control API (docs/CONTROL_API.md): one TCP
// connection, one request at a time, on a worker thread so the window
// never waits on the network. Reconnects by itself on the next request
// after the connection drops.
class ControlClient {
public:
    ControlClient();
    ~ControlClient();

    void SetTarget(const std::string& host, int port, const std::string& password);
    void Disconnect();
    bool Connected() const { return connected_; }

    // Queues a request; the answer is posted to `hwnd` with `tag`.
    void Request(HWND hwnd, const std::string& cmd, const Json& args = Json::object(), WPARAM tag = 0);

    // Requests not yet answered (to avoid piling up polls).
    size_t Pending() const;

private:
    struct Item {
        HWND hwnd;
        std::string cmd;
        Json args;
        WPARAM tag;
    };
    void Run();
    bool EnsureConnected(HWND notify);
    bool Exchange(const std::string& line, std::string& reply);
    void CloseSocket();

    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Item> queue_;
    bool stop_ = false;
    bool busy_ = false;

    std::string host_ = "127.0.0.1";
    int port_ = 7880;
    std::string password_;
    bool targetChanged_ = false;
    bool disconnected_ = false; // Disconnect() until the next SetTarget()

    UINT_PTR socket_;
    std::string buffer_;
    bool connected_ = false;
    int nextId_ = 1;
    bool winsock_ = false;
};

} // namespace manager
