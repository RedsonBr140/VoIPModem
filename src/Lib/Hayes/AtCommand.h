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
    Dial,             // ATDxxxx
    Answer,           // ATA
    HangUp,           // ATHn
    FactoryDefaults,  // AT&Fn
    Unknown
};

struct AtCommand {
    AtCommandType type = AtCommandType::Unknown;
    std::optional<int> numericParam;  // E/V/Q/Z/H/&F parameter, or SetRegister value
    std::optional<int> registerIndex; // S-register index for SetRegister/QueryRegister
    std::string stringParam;          // dial string for Dial
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
