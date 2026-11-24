#include "TestFramework.h"
#include "../src/Lib/Config/AppConfig.h"
#include "../src/Lib/Config/IniConfig.h"
#include "../src/Lib/Config/IniDocument.h"

using voipmodem::AppConfig;
using voipmodem::IniConfig;
using voipmodem::IniDocument;
using voipmodem::LoadAppConfig;

namespace {

const char kSample[] =
    "; header comment\r\n"
    "[Logging]\r\n"
    "Level=Info\r\n"
    "\r\n"
    "; the line\r\n"
    "[Modem.Line1]\r\n"
    "; its port\r\n"
    "ComPort=COM3\r\n"
    "SipExtension=1001\r\n"
    "\r\n"
    "; --- trailing notes about other things ---\r\n";

} // namespace

TEST(IniDocument_UnchangedRoundTripKeepsEveryLine) {
    IniDocument doc;
    doc.LoadFromString(kSample);
    EXPECT_EQ(doc.ToString(), std::string(kSample));
}

TEST(IniDocument_SetReplacesInPlaceCaseInsensitively) {
    IniDocument doc;
    doc.LoadFromString(kSample);
    doc.Set("modem.line1", "comport", "COM7");
    const std::string out = doc.ToString();
    EXPECT_TRUE(out.find("comport=COM7\r\n") != std::string::npos);
    EXPECT_TRUE(out.find("ComPort=COM3") == std::string::npos);
    EXPECT_TRUE(out.find("; its port\r\n") != std::string::npos);
}

TEST(IniDocument_SetAddsMissingKeyAfterTheSectionsLastKey) {
    IniDocument doc;
    doc.LoadFromString(kSample);
    doc.Set("Modem.Line1", "AutoAnswerRingCount", "2");
    const std::string out = doc.ToString();
    EXPECT_TRUE(out.find("SipExtension=1001\r\nAutoAnswerRingCount=2\r\n") != std::string::npos);
}

TEST(IniDocument_SetAddsMissingSectionAtTheEnd) {
    IniDocument doc;
    doc.LoadFromString(kSample);
    doc.Set("Control", "Port", "7880");
    const std::string out = doc.ToString();
    const std::string tail = "\r\n[Control]\r\nPort=7880\r\n"; // after a blank line
    EXPECT_TRUE(out.size() > tail.size() && out.substr(out.size() - tail.size()) == tail);

    IniConfig ini;
    ini.LoadFromString(out);
    EXPECT_EQ(ini.GetInt("Control", "Port", 0), 7880);
    EXPECT_EQ(ini.GetString("Modem.Line1", "ComPort", ""), std::string("COM3"));
}

TEST(IniDocument_RemoveKeyAndSection) {
    IniDocument doc;
    doc.LoadFromString(kSample);
    doc.Remove("Logging", "Level");
    EXPECT_TRUE(!doc.HasKey("Logging", "Level"));
    EXPECT_TRUE(doc.HasSection("Logging"));

    doc.RemoveSection("Modem.Line1");
    const std::string out = doc.ToString();
    EXPECT_TRUE(!doc.HasSection("Modem.Line1"));
    EXPECT_TRUE(out.find("COM3") == std::string::npos);
    // Comments after the section's last key introduce what follows: kept.
    EXPECT_TRUE(out.find("trailing notes") != std::string::npos);
}

TEST(IniDocument_CommentedOutKeyIsNotMistakenForAKey) {
    IniDocument doc;
    doc.LoadFromString("[SIP]\r\n; LocalPort=5060\r\n");
    EXPECT_TRUE(!doc.HasKey("SIP", "LocalPort"));
    doc.Set("SIP", "LocalPort", "5070");
    EXPECT_TRUE(doc.ToString().find("; LocalPort=5060\r\n") != std::string::npos);
    EXPECT_TRUE(doc.HasKey("SIP", "LocalPort"));
}

TEST(AppConfig_LinesInheritDefaultsSection) {
    IniConfig ini;
    ini.LoadFromString(
        "[SIP]\nServer=pbx.lan:5060\n"
        "[Defaults]\nAutoAnswerRingCount=2\nModulations=V21,V34\nCompression=0\n"
        "[Modem.A]\nComPort=COM5\nSipExtension=2001\n"
        "[Modem.B]\nComPort=COM6\nSipExtension=2002\nAutoAnswerRingCount=0\nSipServer=other:5060\n");
    const AppConfig c = LoadAppConfig(ini);
    EXPECT_EQ(c.modems.size(), static_cast<size_t>(2));
    EXPECT_EQ(c.modems[0].autoAnswerRingCount, 2);
    EXPECT_EQ(c.modems[0].modulations, std::string("V21,V34"));
    EXPECT_TRUE(!c.modems[0].compression);
    EXPECT_TRUE(c.modems[0].errorControl);
    EXPECT_EQ(c.modems[0].sipServer, std::string("pbx.lan:5060"));
    EXPECT_EQ(c.modems[1].autoAnswerRingCount, 0);
    EXPECT_EQ(c.modems[1].sipServer, std::string("other:5060"));
    EXPECT_TRUE(c.modems[0].inService);
}

TEST(AppConfig_SipServerIsTheDefaultRegistrarWithoutADefaultsSection) {
    IniConfig ini;
    ini.LoadFromString("[SIP]\nLocalPort=5098\nServer=127.0.0.1:5999\n[Modem.Line1]\nComPort=COM5\nSipExtension=9001\n");
    const AppConfig c = LoadAppConfig(ini);
    EXPECT_EQ(c.modems[0].sipServer, std::string("127.0.0.1:5999"));
}

TEST(AppConfig_ControlMediaAndLoggingSections) {
    IniConfig ini;
    ini.LoadFromString(
        "[Control]\nPort=7999\nBindAddress=0.0.0.0\nPassword=pw\n"
        "[Media]\nCodec=pcma\nJitterBufferMs=5\nRtpPort=16384\nRtpPortRange=100\nEchoCanceller=1\n"
        "[SIP]\nTransport=tcp\nRegisterExpiry=10\nRegisterOnStart=0\n"
        "[Logging]\nLevel=Warning\nRotateKB=512\nKeep=3\nTraceAT=1\n"
        "[Modem]\nComPort=COM3\nInService=0\nErrorControl=0\n");
    const AppConfig c = LoadAppConfig(ini);
    EXPECT_EQ(c.controlPort, 7999);
    EXPECT_EQ(c.controlBindAddress, std::string("0.0.0.0"));
    EXPECT_EQ(c.controlPassword, std::string("pw"));
    EXPECT_EQ(c.preferredCodec, std::string("PCMA"));
    EXPECT_EQ(c.jitterBufferMs, 20); // clamped to the minimum
    EXPECT_EQ(c.rtpPort, 16384);
    EXPECT_EQ(c.rtpPortRange, 100);
    EXPECT_TRUE(c.echoCanceller);
    EXPECT_EQ(c.sipTransport, std::string("TCP"));
    EXPECT_EQ(c.sipRegisterExpiry, 30); // clamped
    EXPECT_TRUE(!c.sipRegisterOnStart);
    EXPECT_TRUE(c.logLevel == voipmodem::LogLevel::Warn);
    EXPECT_EQ(c.logRotateKB, 512);
    EXPECT_EQ(c.logKeepFiles, 3);
    EXPECT_TRUE(c.traceAtCommands);
    EXPECT_TRUE(!c.modems[0].inService);
    EXPECT_TRUE(!c.modems[0].errorControl);
}

TEST(AppConfig_ControlDefaultsToLoopbackOnly) {
    IniConfig ini;
    ini.LoadFromString("[Modem]\nComPort=COM3\n");
    const AppConfig c = LoadAppConfig(ini);
    EXPECT_EQ(c.controlPort, 7880);
    EXPECT_EQ(c.controlBindAddress, std::string("127.0.0.1"));
    EXPECT_TRUE(c.controlPassword.empty());
}
