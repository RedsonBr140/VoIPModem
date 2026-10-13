#pragma once

#include <string>

namespace voipmodem {

// Standard Hayes result codes used by docs/PROJECT_SPEC.md section 3.
// Numeric values match the de facto Hayes numbering (ATV0 mode).
enum class ResultCode {
    OK = 0,
    Connect = 1,
    Ring = 2,
    NoCarrier = 3,
    Error = 4,
    NoDialtone = 6,
    Busy = 7,
    NoAnswer = 8
};

std::string ToVerboseText(ResultCode code);
int ToNumericCode(ResultCode code);

} // namespace voipmodem
