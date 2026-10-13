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
    SetRegister,      // ATSn=v
    QueryRegister,    // ATSn?
    Dial,             // ATDxxxx / ATDTxxxx / ATDPxxxx
    Answer,           // ATA
    HangUp,           // ATHn
    FactoryDefaults,  // AT&Fn
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
    std::optional<int> numericParam;  // E/V/Q/Z/H/&F parameter, or SetRegister value
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
