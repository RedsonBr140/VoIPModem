// Linux only (built by CMakeLists.txt, not the Visual Studio project): the
// PTY serial port and its emulated DTR/DCD, driven from the slave side the
// way pppd or a terminal program would. See docs/LINUX.md.

#include <cerrno>
#include <cstdlib>
#include <string>

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include "TestFramework.h"
#include "../src/Lib/Logging/Logger.h"
#include "../src/Lib/Logging/TaggedLogger.h"
#include "../src/Lib/Serial/PtySerialPort.h"

using voipmodem::Logger;
using voipmodem::PtySerialPort;
using voipmodem::TaggedLogger;

namespace {

// A temporary directory for the symlink, removed afterwards.
struct TempDir {
    std::string path;
    TempDir() {
        char tmpl[] = "/tmp/voipmodem-pty-XXXXXX";
        path = mkdtemp(tmpl);
    }
    ~TempDir() { rmdir(path.c_str()); }
};

struct Rig {
    TempDir dir;
    std::string link = dir.path + "/ttyVM0";
    Logger logger;
    TaggedLogger tagged{logger, "Test"};
    PtySerialPort port{tagged};

    ~Rig() {
        port.Close();
        unlink(link.c_str());
    }

    // Opens the port the way a DTE program does. O_NOCTTY: the test process
    // must not take it as its controlling terminal (and get SIGHUP).
    int OpenSlave(bool followCarrier = false) {
        const int fd = open(link.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd >= 0) {
            termios t{};
            tcgetattr(fd, &t);
            cfmakeraw(&t);
            if (followCarrier) t.c_cflag &= ~CLOCAL; else t.c_cflag |= CLOCAL;
            tcsetattr(fd, TCSANOW, &t);
        }
        return fd;
    }

    bool Dsr() {
        bool on = false;
        port.ReadDsr(on);
        return on;
    }

    // What a slave reader gets within `ms`: the bytes, or "" for none,
    // "<EOF>" or "<EIO>" after a hangup.
    static std::string ReadSlave(int fd, int ms = 200) {
        pollfd p{fd, POLLIN, 0};
        if (poll(&p, 1, ms) <= 0) return "";
        char buf[256];
        const ssize_t n = read(fd, buf, sizeof buf);
        if (n == 0) return "<EOF>";
        if (n < 0) return errno == EIO ? "<EIO>" : (errno == EAGAIN ? "" : "<error>");
        return std::string(buf, static_cast<size_t>(n));
    }

    std::string LinkTarget() const {
        char target[128] = {};
        const ssize_t n = readlink(link.c_str(), target, sizeof target - 1);
        return n > 0 ? std::string(target, static_cast<size_t>(n)) : "";
    }
};

} // namespace

TEST(Pty_OpenPublishesSymlinkAndCloseRemovesIt) {
    Rig rig;
    EXPECT_TRUE(rig.port.Open(rig.link, 115200));
    EXPECT_EQ(rig.LinkTarget(), rig.port.SlaveName());
    EXPECT_TRUE(rig.port.SlaveName().rfind("/dev/pts/", 0) == 0);
    rig.port.Close();
    struct stat st{};
    EXPECT_TRUE(lstat(rig.link.c_str(), &st) != 0);
}

TEST(Pty_ReplacesStaleSymlinkButNotARegularFile) {
    Rig rig;
    EXPECT_EQ(symlink("/dev/pts/nonexistent", rig.link.c_str()), 0);
    EXPECT_TRUE(rig.port.Open(rig.link, 115200));
    EXPECT_EQ(rig.LinkTarget(), rig.port.SlaveName());
    rig.port.Close();

    const int fd = open(rig.link.c_str(), O_CREAT | O_WRONLY, 0600);
    close(fd);
    Rig other; // a second port on the same path must refuse the file
    EXPECT_TRUE(!other.port.Open(rig.link, 115200));
    unlink(rig.link.c_str());
}

TEST(Pty_DsrFollowsWhetherAProgramHasThePortOpen) {
    Rig rig;
    rig.port.Open(rig.link, 115200);
    EXPECT_TRUE(!rig.Dsr());
    const int fd = rig.OpenSlave();
    EXPECT_TRUE(fd >= 0);
    EXPECT_TRUE(rig.Dsr());
    close(fd);
    EXPECT_TRUE(!rig.Dsr());
    const int again = rig.OpenSlave();
    EXPECT_TRUE(rig.Dsr());
    close(again);
}

TEST(Pty_BytesFlowBothWays) {
    Rig rig;
    rig.port.Open(rig.link, 115200);
    const int fd = rig.OpenSlave();
    EXPECT_EQ(write(fd, "AT\r", 3), 3);
    std::string got;
    uint8_t b = 0;
    for (int i = 0; i < 10 && got.size() < 3; ++i) {
        if (rig.port.ReadByte(b)) got += static_cast<char>(b);
    }
    EXPECT_EQ(got, std::string("AT\r"));
    EXPECT_TRUE(rig.port.Write(std::string("\r\nOK\r\n")));
    EXPECT_EQ(Rig::ReadSlave(fd), std::string("\r\nOK\r\n"));
    close(fd);
}

TEST(Pty_ReadByteTimesOutWithNothingToRead) {
    Rig rig;
    rig.port.Open(rig.link, 115200);
    uint8_t b = 0;
    EXPECT_TRUE(!rig.port.ReadByte(b)); // nobody attached
    const int fd = rig.OpenSlave();
    EXPECT_TRUE(!rig.port.ReadByte(b)); // attached, silent
    close(fd);
}

TEST(Pty_OutputWithNobodyAttachedIsDropped) {
    Rig rig;
    rig.port.Open(rig.link, 115200);
    EXPECT_TRUE(rig.port.Write(std::string("\r\nRING\r\n")));
    const int fd = rig.OpenSlave();
    EXPECT_EQ(Rig::ReadSlave(fd, 100), std::string(""));
    close(fd);
}

TEST(Pty_UnreadOutputDoesNotReachTheNextProgram) {
    Rig rig;
    rig.port.Open(rig.link, 115200);
    const int first = rig.OpenSlave();
    rig.Dsr();
    rig.port.Write(std::string("\r\nRING\r\n"));
    close(first); // left unread
    EXPECT_TRUE(!rig.Dsr()); // the port notices the close and flushes
    const int second = rig.OpenSlave();
    EXPECT_EQ(Rig::ReadSlave(second, 100), std::string(""));
    close(second);
}

TEST(Pty_CarrierDropHangsUpAProgramThatFollowsCarrier) {
    Rig rig;
    rig.port.Open(rig.link, 115200);
    const std::string before = rig.port.SlaveName();
    const int fd = rig.OpenSlave(/*followCarrier=*/true);
    EXPECT_TRUE(rig.Dsr());
    EXPECT_TRUE(rig.port.SetDtr(true));
    EXPECT_TRUE(rig.port.SetDtr(false));
    const std::string got = Rig::ReadSlave(fd);
    EXPECT_TRUE(got == "<EOF>" || got == "<EIO>");
    EXPECT_TRUE(write(fd, "x", 1) < 0);
    close(fd);
    // A fresh port behind the same path, ready for the next program.
    EXPECT_TRUE(rig.port.SlaveName() != before);
    EXPECT_EQ(rig.LinkTarget(), rig.port.SlaveName());
    EXPECT_TRUE(!rig.Dsr());
    const int next = rig.OpenSlave();
    EXPECT_TRUE(rig.Dsr());
    rig.port.Write(std::string("OK"));
    EXPECT_EQ(Rig::ReadSlave(next), std::string("OK"));
    close(next);
}

TEST(Pty_CarrierDropLeavesAProgramWithClocalAlone) {
    Rig rig;
    rig.port.Open(rig.link, 115200);
    const std::string before = rig.port.SlaveName();
    const int fd = rig.OpenSlave(/*followCarrier=*/false);
    rig.Dsr();
    rig.port.SetDtr(true);
    rig.port.SetDtr(false);
    EXPECT_EQ(rig.port.SlaveName(), before);
    rig.port.Write(std::string("NO CARRIER"));
    EXPECT_EQ(Rig::ReadSlave(fd), std::string("NO CARRIER"));
    close(fd);
}

TEST(Pty_CarrierOffWithoutAPreviousOnDoesNothing) {
    Rig rig;
    rig.port.Open(rig.link, 115200);
    const std::string before = rig.port.SlaveName();
    const int fd = rig.OpenSlave(/*followCarrier=*/true);
    rig.Dsr();
    rig.port.SetDtr(false); // &C1 at idle: carrier was never on
    EXPECT_EQ(rig.port.SlaveName(), before);
    close(fd);
}
