#pragma once

#include <string>

#include "ISerialPort.h"
#include "../Logging/TaggedLogger.h"

namespace voipmodem {

// Linux: the line's "serial port" is a pseudo-terminal that VoIPModem
// creates, published as a symlink at the configured path (ComPort=, for
// example /run/voipmodem/ttyVM0). pppd, mgetty or a terminal program opens
// the symlink as if it were a modem's port. See docs/LINUX.md "PTY serial
// side".
//
// A PTY has no modem control lines, so they are emulated:
// - ReadDsr() (the DTE's DTR): true while any program has the port open.
//   When the last one closes it, whatever it left unread is flushed, so
//   the next program does not get stale output.
// - SetDtr() (our DCD): lowering it while a program that follows carrier
//   (CLOCAL off) has the port open hangs that program up, by replacing the
//   PTY behind the symlink; it gets SIGHUP, then EOF and EIO. A program
//   with CLOCAL on is left alone, as a real modem's DCD drop would be.
//   Raising it cannot be signalled.
// - Output written while no program has the port open is dropped.
//
// Not thread-safe: the line's thread does all calls, as with
// WindowsSerialPort.
class PtySerialPort : public ISerialPort {
public:
    explicit PtySerialPort(TaggedLogger& logger);
    ~PtySerialPort() override;

    PtySerialPort(const PtySerialPort&) = delete;
    PtySerialPort& operator=(const PtySerialPort&) = delete;

    // `portName`: the symlink to publish. An existing symlink there is
    // replaced; anything else there is an error. Its directory must exist.
    // `baudRate` is ignored: a PTY has no line speed.
    bool Open(const std::string& portName, int baudRate) override;
    void Close() override;
    bool IsOpen() const override { return master_ >= 0; }

    // Waits up to 200 ms for a byte.
    bool ReadByte(uint8_t& outByte) override;

    // Drops the data and returns true when no program has the port open;
    // otherwise waits up to 200 ms for room.
    bool Write(const std::string& data) override;
    bool Write(const std::vector<uint8_t>& data) override;

    bool SetDtr(bool asserted) override;
    bool ReadDsr(bool& asserted) override;

    // The PTY's slave device behind the symlink ("/dev/pts/3"), for logs
    // and tests. Changes when SetDtr(false) hangs a program up.
    const std::string& SlaveName() const { return slaveName_; }

private:
    bool CreatePty();      // new master_ and slaveName_, symlink pointed at it
    void UpdateAttached(); // reads whether a program has the slave open
    void FlushSlaveInput();
    bool SlaveFollowsCarrier() const;
    bool WriteBytes(const uint8_t* data, size_t size);

    TaggedLogger& logger_;
    std::string linkPath_;
    std::string slaveName_;
    int master_ = -1;
    bool attached_ = false; // a program has the slave open
    bool carrier_ = false;  // last SetDtr() value
};

} // namespace voipmodem
