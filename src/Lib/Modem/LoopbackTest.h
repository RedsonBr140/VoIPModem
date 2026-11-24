#pragma once

#include <string>

namespace voipmodem {

struct LoopbackResult {
    bool connected = false;
    std::string modulation;  // what the two modems agreed on
    int txBps = 0;           // the calling modem's rates
    int rxBps = 0;
    double snrDb = -1;       // the calling modem's receiver
    double simulatedSeconds = 0; // line time to connect (or to give up)
    std::string failure;     // why not, when !connected
};

// The Manager's "Loopback self-test": two libv90modem modems, a caller
// and an answerer, connected back to back over a perfect in-memory line,
// offering `modulations` (ModemInstanceConfig syntax, e.g. "V34"; empty =
// V.34 and below). Runs as fast as the CPU allows, so it checks the modem
// engine in this build on this machine, not the network or any call.
LoopbackResult RunModemLoopback(const std::string& modulations, double maxLineSeconds = 40.0);

// libv90modem's version, e.g. "0.6.0".
std::string ModemEngineVersion();

} // namespace voipmodem
