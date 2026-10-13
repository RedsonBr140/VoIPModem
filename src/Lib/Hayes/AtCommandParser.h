#pragma once

#include <string>

#include "AtCommand.h"

namespace voipmodem {

// Parses a single command-mode input line into a sequence of AtCommand
// values. Supports the subset required by docs/PROJECT_SPEC.md section 3:
// AT, ATI, ATZ, ATE, ATV, ATQ, ATS<n>=<v>, ATS<n>?, ATD, ATA, ATH, AT&F.
//
// Commands may be chained after a single "AT" prefix (e.g. "ATE1V0Q0"),
// except ATD, which consumes the remainder of the line as the dial string
// and therefore must be the last command in a chain.
class AtCommandParser {
public:
    static AtParseResult Parse(const std::string& line);
};

} // namespace voipmodem
