#pragma once

#include <string>
#include <vector>

namespace voipmodem {

// What the host has in the way of serial ports and modem drivers, for the
// Manager's "Driver binding" column and its com0com / driver windows.
// Read from the Windows registry; empty elsewhere.
struct PortPair {
    std::string a; // e.g. "COM3" (com0com's CNCA<n>)
    std::string b; // e.g. "COM4" (CNCB<n>)
};

struct ModemDriver {
    std::string name;       // "Standard 56000 bps Modem"
    std::string attachedTo; // "COM3"
};

struct SystemPorts {
    std::vector<std::string> serialPorts; // every COM port present now
    std::vector<PortPair> com0comPairs;
    std::vector<ModemDriver> modems;
};

SystemPorts QuerySystemPorts();

// The other end of `port`'s com0com pair; empty if it is not in a pair.
std::string PairedPort(const SystemPorts& ports, const std::string& port);

// The modem driver a line on `port` is reached through: the driver bound
// to the other side of its com0com pair, as "<driver> on COM3";
// "(not bound) COM3" if no driver is, "(no com0com pair)" if `port` is
// not in a pair.
std::string DriverBinding(const SystemPorts& ports, const std::string& port);

} // namespace voipmodem
