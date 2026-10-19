#pragma once

namespace voipmodem {

// ATE / ATV / ATQ / ATL / ATM state. Factory defaults match common Hayes
// convention: echo on, verbose text responses, result codes not
// suppressed, speaker at medium volume, on until carrier is detected.
//
// speakerVolume (ATLn, 0-3) and speakerMode (ATMn, 0-3) are accepted and
// stored here purely so real modem-aware software doesn't get an ERROR
// for sending them -- Windows' own "Phone and Modem" control panel's
// Properties dialog maps its "Speaker volume" slider directly to ATLn,
// and many standard init strings include ATM1 or similar. Real Hayes
// modems use these values to control a *physical* on-board speaker,
// which this software modem has no equivalent of; see
// `WindowsAudioOutput`/`CallAudioBridge` for the actual "let a human
// hear this call's audio on the PC's sound output" feature, which is
// currently a separate, simpler always-on monitor that does NOT yet
// honor these values (see docs/ROADMAP.md Phase 4 for that honesty
// caveat).
struct ModemProfile {
    bool echoEnabled = true;
    bool verboseResultCodes = true;
    bool quietMode = false; // when true, suppress all result codes
    int speakerVolume = 2;  // ATLn, 0-3
    int speakerMode = 1;    // ATMn, 0-3

    void ResetToFactoryDefaults() {
        echoEnabled = true;
        verboseResultCodes = true;
        quietMode = false;
        speakerVolume = 2;
        speakerMode = 1;
    }
};

} // namespace voipmodem
