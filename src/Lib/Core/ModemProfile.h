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
//
// AT&C, AT&D, AT&K, ATX and ATN follow the same precedent. Windows'
// generic modem driver (mdmgen.inf, "Standard 56000 bps Modem") sends
// "AT&FE0V1&C1&D2S95=47S0=0" and "ATS7=60L0M1&K3N1X4" when RAS opens the
// line, and closes the line if either gets ERROR. Each value below is
// ACCEPTED AND STORED, NOT YET HONORED: changing it changes nothing about
// how the modem behaves. Factory defaults are the common Hayes-compatible
// (Rockwell/Conexant-style) data-modem profile: &C1 &D2 &K3 X4 N1.
struct ModemProfile {
    bool echoEnabled = true;
    bool verboseResultCodes = true;
    bool quietMode = false; // when true, suppress all result codes
    int speakerVolume = 2;  // ATLn, 0-3
    int speakerMode = 1;    // ATMn, 0-3

    // AT&Cn, 0-1. Accepted and stored, not yet honored. Honoring it means
    // driving the serial port's DCD line: &C0 = DCD always on, &C1 = DCD
    // on only while a modem data link is up.
    int dcdMode = 1;

    // AT&Dn, 0-3. Accepted and stored, not yet honored. Honoring it means
    // acting on the DTE dropping DTR: &D0 = ignore, &D1 = return to command
    // mode with the call up, &D2 = hang up, &D3 = hang up and reset (ATZ).
    int dtrMode = 2;

    // AT&Kn, 0/3/4/5/6. Accepted and stored, not yet honored. Honoring it
    // means flow control between the DTE and this modem: &K0 = none,
    // &K3 = RTS/CTS, &K4 = XON/XOFF, &K5 = transparent XON/XOFF,
    // &K6 = RTS/CTS and XON/XOFF.
    int flowControl = 3;

    // ATXn, 0-4. Accepted and stored, not yet honored. Honoring it means
    // choosing which result codes are reported: X0 reports a bare CONNECT
    // and no BUSY/NO DIALTONE, X4 the full extended set. Today the full set
    // (CONNECT <rate>, BUSY, NO ANSWER, NO CARRIER) is always used.
    int resultCodeSet = 4;

    // ATNn, 0-1. Accepted and stored, not yet honored. Honoring it means
    // N0 = connect only at the rate set by S37, N1 = negotiate the best
    // rate both ends support. Today the modulations offered come only from
    // the line's Modulations= setting.
    int speedNegotiation = 1;

    void ResetToFactoryDefaults() {
        echoEnabled = true;
        verboseResultCodes = true;
        quietMode = false;
        speakerVolume = 2;
        speakerMode = 1;
        dcdMode = 1;
        dtrMode = 2;
        flowControl = 3;
        resultCodeSet = 4;
        speedNegotiation = 1;
    }
};

} // namespace voipmodem
