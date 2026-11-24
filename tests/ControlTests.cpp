// The control API's building blocks: statistics, com0com pairing, the
// TCP server (over a real loopback socket) and the loopback self-test.
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>

#include "TestFramework.h"
#include "../src/Lib/Control/ControlServer.h"
#include "../src/Lib/Control/ServiceStats.h"
#include "../src/Lib/Control/SystemInfo.h"
#include "../src/Lib/Modem/LoopbackTest.h"

using voipmodem::ControlError;
using voipmodem::ControlServer;
using voipmodem::ServiceStats;
using Json = nlohmann::json;

namespace {

// A blocking line-at-a-time client for the tests.
class TestClient {
public:
    explicit TestClient(int port) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        s_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port));
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        connected_ = connect(s_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0;
        DWORD timeout = 5000;
        setsockopt(s_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
    }
    ~TestClient() {
        closesocket(s_);
        WSACleanup();
    }
    bool Connected() const { return connected_; }

    Json Call(const std::string& line) {
        const std::string data = line + "\n";
        send(s_, data.data(), static_cast<int>(data.size()), 0);
        std::string reply;
        char ch;
        while (recv(s_, &ch, 1, 0) == 1 && ch != '\n') reply += ch;
        try {
            return Json::parse(reply);
        } catch (const std::exception&) {
            return Json();
        }
    }

private:
    SOCKET s_;
    bool connected_ = false;
};

ControlServer::Handler EchoHandler() {
    return [](const std::string& cmd, const Json& args) -> Json {
        if (cmd == "hello") return Json{{"service", "test"}};
        if (cmd == "echo") return args;
        if (cmd == "refuse") throw ControlError("refused on purpose");
        throw ControlError("unknown command: " + cmd);
    };
}

} // namespace

TEST(ServiceStats_CountsAndModeDistribution) {
    ServiceStats stats;
    stats.IncomingOffered();
    stats.InboundAnswered();
    stats.Connected(true, "V.90", 56000, 26400);
    stats.CallCompleted(120.0, 1, 2);
    stats.IncomingOffered();
    stats.InboundAnswered();
    stats.FailedToTrain();
    stats.Connected(false, "V.34", 31200, 14400);
    stats.CallCompleted(60.0, 0, 0);
    stats.ObserveBusyLines(3);
    stats.ObserveBusyLines(1);
    stats.Bytes(100, 7);
    const ServiceStats::Snapshot s = stats.Get();
    EXPECT_EQ(s.callsAnswered, 2ul);
    EXPECT_EQ(s.trained, 1ul);
    EXPECT_EQ(s.failedToTrain, 1ul);
    EXPECT_EQ(s.outgoingConnected, 1ul);
    EXPECT_EQ(s.completedCalls, 2ul);
    EXPECT_TRUE(s.longestCallSeconds == 120.0);
    EXPECT_TRUE(s.totalConnectedSeconds == 180.0);
    EXPECT_EQ(s.peakBusyLines, 3);
    EXPECT_EQ(s.retrains, 1ul);
    EXPECT_EQ(s.renegotiations, 2ul);
    EXPECT_EQ(s.bytesIn, 100ull);
    EXPECT_EQ(s.connectsByMode.at("V.90 56000"), 1ul);
    EXPECT_EQ(s.connectsByMode.at("V.34 31200"), 1ul);
}

TEST(SystemInfo_DriverBindingFollowsTheCom0comPair) {
    voipmodem::SystemPorts ports;
    ports.com0comPairs.push_back({"COM3", "COM4"});
    ports.com0comPairs.push_back({"COM5", "COM6"});
    ports.modems.push_back({"Standard 56000 bps Modem", "COM4"});
    EXPECT_EQ(voipmodem::PairedPort(ports, "com3"), std::string("COM4"));
    EXPECT_EQ(voipmodem::DriverBinding(ports, "COM3"), std::string("Standard 56000 bps Modem on COM4"));
    EXPECT_EQ(voipmodem::DriverBinding(ports, "COM5"), std::string("(not bound) COM6"));
    EXPECT_EQ(voipmodem::DriverBinding(ports, "COM9"), std::string("(no com0com pair)"));
}

TEST(ControlServer_AnswersRequestsAndErrorsOverTcp) {
    voipmodem::Logger logger;
    logger.SetConsoleEcho(false);
    ControlServer server(logger);
    EXPECT_TRUE(server.Start("127.0.0.1", 0, "", EchoHandler()));
    TestClient client(server.Port());
    EXPECT_TRUE(client.Connected());

    Json hello = client.Call(R"({"id":1,"cmd":"hello"})");
    EXPECT_EQ(hello["id"], Json(1));
    EXPECT_EQ(hello["ok"], Json(true));
    EXPECT_EQ(hello["result"]["protocol"], Json(ControlServer::kProtocolVersion));
    EXPECT_EQ(hello["result"]["passwordRequired"], Json(false));

    Json echo = client.Call(R"({"id":"x","cmd":"echo","args":{"a":[1,2]}})");
    EXPECT_EQ(echo["id"], Json("x"));
    EXPECT_EQ(echo["result"]["a"][1], Json(2));

    Json refused = client.Call(R"({"id":3,"cmd":"refuse"})");
    EXPECT_EQ(refused["ok"], Json(false));
    EXPECT_EQ(refused["error"], Json("refused on purpose"));

    Json garbage = client.Call("this is not json");
    EXPECT_EQ(garbage["ok"], Json(false));
    server.Stop();
}

TEST(ControlServer_PasswordGatesEveryCommandButHello) {
    voipmodem::Logger logger;
    logger.SetConsoleEcho(false);
    ControlServer server(logger);
    EXPECT_TRUE(server.Start("127.0.0.1", 0, "s3cret", EchoHandler()));
    TestClient client(server.Port());

    EXPECT_EQ(client.Call(R"({"cmd":"echo"})")["ok"], Json(false));
    Json wrong = client.Call(R"({"cmd":"hello","args":{"password":"nope"}})");
    EXPECT_EQ(wrong["ok"], Json(false));
    EXPECT_EQ(wrong["error"], Json("wrong password"));
    EXPECT_EQ(client.Call(R"({"cmd":"echo"})")["ok"], Json(false));

    Json right = client.Call(R"({"cmd":"hello","args":{"password":"s3cret"}})");
    EXPECT_EQ(right["ok"], Json(true));
    EXPECT_EQ(right["result"]["passwordRequired"], Json(true));
    EXPECT_EQ(client.Call(R"({"cmd":"echo","args":{"k":1}})")["result"]["k"], Json(1));
    server.Stop();
}

TEST(ControlServer_RefusesAnAddressThatIsNotIPv4) {
    voipmodem::Logger logger;
    logger.SetConsoleEcho(false);
    ControlServer server(logger);
    EXPECT_TRUE(!server.Start("not-an-address", 0, "", EchoHandler()));
}

TEST(LoopbackSelfTest_V21Connects) {
    const voipmodem::LoopbackResult r = voipmodem::RunModemLoopback("V21", 20.0);
    EXPECT_TRUE(r.connected);
    EXPECT_EQ(r.modulation, std::string("V.21"));
    EXPECT_EQ(r.txBps, 300);
    EXPECT_TRUE(!voipmodem::ModemEngineVersion().empty());
}
