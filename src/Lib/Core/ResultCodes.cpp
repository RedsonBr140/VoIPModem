#include "ResultCodes.h"

namespace voipmodem {

std::string ToVerboseText(ResultCode code) {
    switch (code) {
        case ResultCode::OK: return "OK";
        case ResultCode::Connect: return "CONNECT";
        case ResultCode::Ring: return "RING";
        case ResultCode::NoCarrier: return "NO CARRIER";
        case ResultCode::Error: return "ERROR";
        case ResultCode::NoDialtone: return "NO DIALTONE";
        case ResultCode::Busy: return "BUSY";
        case ResultCode::NoAnswer: return "NO ANSWER";
    }
    return "ERROR";
}

int ToNumericCode(ResultCode code) {
    return static_cast<int>(code);
}

} // namespace voipmodem
