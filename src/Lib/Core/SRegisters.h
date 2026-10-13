#pragma once

#include <array>
#include <cstdint>

namespace voipmodem {

// A minimal S-register bank. Only S0 (auto-answer ring count) has defined
// semantics anywhere in Phase 1, and even that is write/read-only for now —
// nothing yet triggers auto-answer on a RING event, since there is no SIP
// layer to produce one (see docs/ROADMAP.md, Phase 2). Registers S1-S9 are
// generic storage so ATSn=/ATSn? never fail for registers a real modem
// would also expose.
class SRegisters {
public:
    static constexpr int kCount = 10;

    uint8_t Get(int index) const {
        if (index < 0 || index >= kCount) {
            return 0;
        }
        return values_[static_cast<size_t>(index)];
    }

    void Set(int index, uint8_t value) {
        if (index < 0 || index >= kCount) {
            return;
        }
        values_[static_cast<size_t>(index)] = value;
    }

    void ResetToFactoryDefaults() {
        values_.fill(0);
    }

private:
    std::array<uint8_t, kCount> values_{};
};

} // namespace voipmodem
