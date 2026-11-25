#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace voipmodem {

// Abstraction over a serial/virtual-COM connection, kept separate from
// Windows API details so Core/Hayes logic and the App-layer command
// session can be unit tested without opening a real COM port, and so a
// future non-Windows or Server-2003-specific backend can implement the
// same interface (see CLAUDE.md "Architectural Boundaries").
class ISerialPort {
public:
    virtual ~ISerialPort() = default;

    virtual bool Open(const std::string& portName, int baudRate) = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;

    // Blocking read of up to one byte. Returns false on timeout or error;
    // `outByte` is only valid when this returns true.
    virtual bool ReadByte(uint8_t& outByte) = 0;

    virtual bool Write(const std::string& data) = 0;
    virtual bool Write(const std::vector<uint8_t>& data) = 0;

    // Modem control lines (see docs/ARCHITECTURE.md "Serial control
    // signals"). This port is the modem (DCE) end, but a com0com pair has
    // no DCD output of its own: with com0com's default wiring, the DTR set
    // here is what the other side reads as both DCD and DSR, and the other
    // side's DTR is what ReadDsr() reports.
    //
    // SetDtr() drives this port's DTR output. Returns false if the port is
    // closed or the driver refuses.
    virtual bool SetDtr(bool asserted) = 0;

    // Reads this port's DSR input into `asserted`. Returns false (leaving
    // `asserted` unchanged) if the port is closed or the read fails.
    virtual bool ReadDsr(bool& asserted) = 0;
};

} // namespace voipmodem
