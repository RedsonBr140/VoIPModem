#include "PtySerialPort.h"

#include <cerrno>
#include <cstring>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

namespace voipmodem {

namespace {

constexpr int kReadTimeoutMs = 200;  // as WindowsSerialPort's COMMTIMEOUTS
constexpr int kWriteTimeoutMs = 200;
constexpr int kDetachedPollMs = 50; // no program attached: poll() would return at once

std::string ErrnoText() {
    return std::strerror(errno);
}

// A program that never sets termios gets a raw 8-bit port that ignores
// carrier (CLOCAL on), so only programs that ask to follow carrier are hung
// up by SetDtr(false).
termios InitialTermios() {
    termios t{};
    cfmakeraw(&t);
    t.c_cflag |= CLOCAL | CREAD;
    cfsetispeed(&t, B115200);
    cfsetospeed(&t, B115200);
    return t;
}

} // namespace

PtySerialPort::PtySerialPort(TaggedLogger& logger) : logger_(logger) {}

PtySerialPort::~PtySerialPort() {
    Close();
}

bool PtySerialPort::Open(const std::string& portName, int /*baudRate*/) {
    Close();
    struct stat st{};
    if (lstat(portName.c_str(), &st) == 0 && !S_ISLNK(st.st_mode)) {
        logger_.Error(portName + " exists and is not a symlink; not replacing it");
        return false;
    }
    linkPath_ = portName;
    if (!CreatePty()) {
        linkPath_.clear();
        return false;
    }
    attached_ = false;
    carrier_ = false;
    logger_.Info("Opened " + linkPath_ + " -> " + slaveName_ + " (pseudo-terminal)");
    return true;
}

bool PtySerialPort::CreatePty() {
    termios t = InitialTermios();
    int master = -1;
    int slave = -1;
    char name[128] = {};
    if (openpty(&master, &slave, name, &t, nullptr) != 0) {
        logger_.Error("openpty failed: " + ErrnoText());
        return false;
    }
    // Our copy of the slave must not count as a program having the port open.
    close(slave);
    fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
    fcntl(master, F_SETFD, FD_CLOEXEC);

    // Point the symlink at the new slave in one step (rename replaces it), so
    // a program reopening the port never finds it missing.
    const std::string temp = linkPath_ + ".new." + std::to_string(getpid());
    unlink(temp.c_str());
    if (symlink(name, temp.c_str()) != 0 || rename(temp.c_str(), linkPath_.c_str()) != 0) {
        logger_.Error("Cannot create " + linkPath_ + ": " + ErrnoText());
        unlink(temp.c_str());
        close(master);
        return false;
    }

    const int old = master_;
    master_ = master;
    slaveName_ = name;
    // Closed last: a program still on the old PTY is hung up only once the
    // symlink already leads to the new one.
    if (old >= 0) close(old);
    return true;
}

void PtySerialPort::Close() {
    if (master_ < 0) return;
    // Remove the symlink only if it is still ours.
    char target[128] = {};
    const ssize_t n = readlink(linkPath_.c_str(), target, sizeof target - 1);
    if (n > 0 && slaveName_ == std::string(target, static_cast<size_t>(n))) {
        unlink(linkPath_.c_str());
    }
    close(master_);
    master_ = -1;
    attached_ = false;
    logger_.Info("Closed " + linkPath_);
}

void PtySerialPort::UpdateAttached() {
    pollfd p{master_, POLLIN, 0};
    if (poll(&p, 1, 0) < 0) return;
    const bool attached = (p.revents & POLLHUP) == 0;
    if (attached_ && !attached) {
        FlushSlaveInput();
    }
    attached_ = attached;
}

// The last program closed the port. What we wrote that it did not read is
// still queued in the slave and would reach the next program; the master
// cannot flush it, so open the slave briefly and flush it there.
void PtySerialPort::FlushSlaveInput() {
    const int fd = open(slaveName_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return;
    tcflush(fd, TCIFLUSH);
    close(fd);
}

bool PtySerialPort::ReadByte(uint8_t& outByte) {
    if (master_ < 0) return false;
    pollfd p{master_, POLLIN, 0};
    const int r = poll(&p, 1, kReadTimeoutMs);
    if (r > 0 && (p.revents & POLLIN) != 0) {
        const ssize_t n = read(master_, &outByte, 1);
        if (n == 1) {
            attached_ = true;
            return true;
        }
        // EIO: the program closed the port; handled below.
    }
    UpdateAttached();
    if (!attached_) {
        // With no program attached, poll() reports POLLHUP at once; wait a
        // little so the line's loop does not spin.
        poll(nullptr, 0, kDetachedPollMs);
    }
    return false;
}

bool PtySerialPort::Write(const std::string& data) {
    return WriteBytes(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

bool PtySerialPort::Write(const std::vector<uint8_t>& data) {
    return WriteBytes(data.data(), data.size());
}

bool PtySerialPort::WriteBytes(const uint8_t* data, size_t size) {
    if (master_ < 0) return false;
    UpdateAttached();
    if (!attached_) {
        return true; // nobody to hear it, as with a real modem and no DTE
    }
    size_t done = 0;
    while (done < size) {
        const ssize_t n = write(master_, data + done, size - done);
        if (n > 0) {
            done += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EAGAIN) {
            pollfd p{master_, POLLOUT, 0};
            if (poll(&p, 1, kWriteTimeoutMs) > 0 && (p.revents & POLLOUT) != 0) continue;
            logger_.Warn("The program on " + linkPath_ + " is not reading; dropped " +
                         std::to_string(size - done) + " byte(s)");
            return false;
        }
        if (n < 0 && errno == EINTR) continue;
        return false; // EIO: the program closed the port meanwhile
    }
    return true;
}

bool PtySerialPort::SlaveFollowsCarrier() const {
    // On Linux the master reports the slave's termios.
    termios t{};
    if (tcgetattr(master_, &t) != 0) return false;
    return (t.c_cflag & CLOCAL) == 0;
}

bool PtySerialPort::SetDtr(bool asserted) {
    if (master_ < 0) return false;
    const bool dropped = carrier_ && !asserted;
    carrier_ = asserted;
    if (!dropped) return true;
    UpdateAttached();
    if (!attached_ || !SlaveFollowsCarrier()) return true;
    logger_.Info("Carrier off: hanging up the program on " + linkPath_ + " (it follows carrier)");
    const std::string old = slaveName_;
    if (!CreatePty()) {
        logger_.Error("Could not replace the pseudo-terminal; the program on " + old + " was not hung up");
        return false;
    }
    attached_ = false;
    logger_.Debug(linkPath_ + " now -> " + slaveName_);
    return true;
}

bool PtySerialPort::ReadDsr(bool& asserted) {
    if (master_ < 0) return false;
    UpdateAttached();
    asserted = attached_;
    return true;
}

} // namespace voipmodem
