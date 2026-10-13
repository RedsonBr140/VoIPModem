#include "TestFramework.h"
#include "../src/Lib/Config/AppConfig.h"
#include "../src/Lib/Config/IniConfig.h"

using voipmodem::AppConfig;
using voipmodem::IniConfig;
using voipmodem::LoadAppConfig;

TEST(NoModemSection_FallsBackToOneDefaultInstance) {
    IniConfig ini;
    ini.LoadFromString("[Logging]\nLevel=Info\n");

    const AppConfig config = LoadAppConfig(ini);
    EXPECT_EQ(config.modems.size(), static_cast<size_t>(1));
    EXPECT_EQ(config.modems[0].name, std::string(""));
    EXPECT_EQ(config.modems[0].comPort, std::string("COM3"));
}

TEST(LegacySingleModemSection_YieldsOneUnnamedInstance) {
    IniConfig ini;
    ini.LoadFromString(
        "[Modem]\n"
        "ComPort=COM7\n"
        "BaudRate=57600\n"
        "Identification=Legacy Modem\n");

    const AppConfig config = LoadAppConfig(ini);
    EXPECT_EQ(config.modems.size(), static_cast<size_t>(1));
    EXPECT_EQ(config.modems[0].name, std::string(""));
    EXPECT_EQ(config.modems[0].comPort, std::string("COM7"));
    EXPECT_EQ(config.modems[0].baudRate, 57600);
    EXPECT_EQ(config.modems[0].modemIdentification, std::string("Legacy Modem"));
}

TEST(MultipleModemSections_YieldOneInstanceEachInFileOrder) {
    IniConfig ini;
    ini.LoadFromString(
        "[Logging]\n"
        "Level=Debug\n"
        "\n"
        "[Modem.Line1]\n"
        "ComPort=COM3\n"
        "Identification=Line 1\n"
        "\n"
        "[Modem.Line2]\n"
        "ComPort=COM5\n"
        "Identification=Line 2\n");

    const AppConfig config = LoadAppConfig(ini);
    EXPECT_EQ(config.modems.size(), static_cast<size_t>(2));
    EXPECT_EQ(config.modems[0].name, std::string("Line1"));
    EXPECT_EQ(config.modems[0].comPort, std::string("COM3"));
    EXPECT_EQ(config.modems[1].name, std::string("Line2"));
    EXPECT_EQ(config.modems[1].comPort, std::string("COM5"));
}

TEST(NonModemSections_AreIgnoredForInstanceDiscovery) {
    IniConfig ini;
    ini.LoadFromString(
        "[Logging]\n"
        "Level=Info\n"
        "\n"
        "[Modem.Line1]\n"
        "ComPort=COM3\n");

    const AppConfig config = LoadAppConfig(ini);
    EXPECT_EQ(config.modems.size(), static_cast<size_t>(1));
    EXPECT_EQ(config.modems[0].name, std::string("Line1"));
}

TEST(IniConfig_SectionNames_PreservesOriginalCaseAndOrder) {
    IniConfig ini;
    ini.LoadFromString("[Modem.Line1]\nComPort=COM3\n\n[Modem.Line2]\nComPort=COM5\n");

    const auto& sections = ini.SectionNames();
    EXPECT_EQ(sections.size(), static_cast<size_t>(2));
    EXPECT_EQ(sections[0], std::string("Modem.Line1"));
    EXPECT_EQ(sections[1], std::string("Modem.Line2"));
}
