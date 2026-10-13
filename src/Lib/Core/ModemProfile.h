#pragma once

namespace voipmodem {

// ATE / ATV / ATQ state. Factory defaults match common Hayes convention:
// echo on, verbose text responses, result codes not suppressed.
struct ModemProfile {
    bool echoEnabled = true;
    bool verboseResultCodes = true;
    bool quietMode = false; // when true, suppress all result codes

    void ResetToFactoryDefaults() {
        echoEnabled = true;
        verboseResultCodes = true;
        quietMode = false;
    }
};

} // namespace voipmodem
