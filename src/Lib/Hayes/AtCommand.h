#pragma once

#include <optional>
#include <string>
#include <vector>

namespace voipmodem {

enum class AtCommandType {
    Attention,        // AT (bare)
    Identify,         // ATIn
    Reset,            // ATZn
    SetEcho,          // ATEn
    SetVerbose,       // ATVn
    SetQuiet,         // ATQn
    SetSpeakerVolume, // ATLn -- real modems' physical speaker volume; see ModemProfile.h
    SetSpeakerMode,   // ATMn -- real modems' physical speaker on/off/when; see ModemProfile.h
    SetRegister,      // ATSn=v
    QueryRegister,    // ATSn?
    Dial,             // ATDxxxx / ATDTxxxx / ATDPxxxx
    Answer,           // ATA
    HangUp,           // ATHn
    FactoryDefaults,  // AT&Fn
    SetDcdMode,       // AT&Cn (0-1) -- stored only, not honored; see ModemProfile.h
    SetDtrMode,       // AT&Dn (0-3) -- stored only, not honored; see ModemProfile.h
    SetFlowControl,   // AT&Kn (0,3,4,5,6) -- stored only, not honored; see ModemProfile.h
    SetResultCodeSet, // ATXn (0-4) -- stored only, not honored; see ModemProfile.h
    SetSpeedNegotiation, // ATNn (0-1) -- stored only, not honored; see ModemProfile.h
    ExtendedCommand,  // AT+<name>[=?|?|=<value>] or AT#<name>[...] (V.25ter / vendor extensions)
    Unknown
};

// How an ExtendedCommand was suffixed: "=?" asks what values/behavior are
// supported (Test), "?" asks the current value (Query/Read), "=<value>"
// sets one (SetValue), and no suffix at all is a bare action command.
enum class AtExtendedSuffix {
    None,
    Test,     // +NAME=?
    Query,    // +NAME?
    SetValue  // +NAME=<value>
};

// The T/P immediately after D selects tone vs. pulse dialing on a real
// POTS line. There is no physical line here — calls go out over SIP
// regardless — but the character must still be recognized and stripped
// from the dial string rather than being dialed as a literal digit. Most
// Windows modem drivers send ATDT by default, so this is required for
// interoperability with Windows RAS, not just a cosmetic nicety.
enum class AtDialMode {
    Default, // bare ATD, no T/P given
    Tone,    // ATDT
    Pulse    // ATDP
};

struct AtCommand {
    AtCommandType type = AtCommandType::Unknown;
    std::optional<int> numericParam;  // E/V/Q/L/M/X/N/Z/H/&C/&D/&F/&K parameter, or SetRegister value
    std::optional<int> registerIndex; // S-register index for SetRegister/QueryRegister
    AtDialMode dialMode = AtDialMode::Default; // meaningful only for Dial
    std::string stringParam;          // dial string for Dial, with any T/P modifier removed
    char extendedPrefix = '+';                        // '+' or '#'; meaningful only for ExtendedCommand
    std::string extendedName;                         // e.g. "FCLASS", "GMM"; always uppercased
    AtExtendedSuffix extendedSuffix = AtExtendedSuffix::None; // meaningful only for ExtendedCommand
    std::string extendedValue;                         // the <value> in "+NAME=<value>"
};

// Result of parsing one command line. A syntactically invalid line
// (unrecognized token, malformed register syntax, etc.) yields
// isValid == false and the caller should respond ERROR without
// executing any of the parsed commands.
struct AtParseResult {
    bool isValid = false;
    std::vector<AtCommand> commands;
};

} // namespace voipmodem
