#pragma once

#include <Windows.h>

#include "ISerialPort.h"
#include "../Logging/Logger.h"

namespace voipmodem {

// Win32 COM-port backed implementation of ISerialPort. Intended for use
// against a com0com virtual pair during development (see
// docs/PROJECT_SPEC.md section 3), but works against any real COM port
// the same way.
class WindowsSerialPort : public ISerialPort {
public:
    explicit WindowsSerialPort(Logger& logger);
    ~WindowsSerialPort() override;

    bool Open(const std::string& portName, int baudRate) override;
    void Close() override;
    bool IsOpen() const override;

    bool ReadByte(uint8_t& outByte) override;
    bool Write(const std::string& data) override;
    bool Write(const std::vector<uint8_t>& data) override;

private:
    Logger& logger_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

} // namespace voipmodem
