#include "WindowsSerialPort.h"

namespace voipmodem {

namespace {

std::string ToDevicePath(const std::string& portName) {
    // "\\.\COMx" form is required for COM10 and above, and works fine
    // for lower-numbered ports too.
    return "\\\\.\\" + portName;
}

} // namespace

WindowsSerialPort::WindowsSerialPort(TaggedLogger& logger) : logger_(logger) {}

WindowsSerialPort::~WindowsSerialPort() {
    Close();
}

bool WindowsSerialPort::Open(const std::string& portName, int baudRate) {
    Close();

    const std::string devicePath = ToDevicePath(portName);
    handle_ = CreateFileA(devicePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        logger_.Error("Failed to open " + portName + " (GetLastError=" +
                       std::to_string(GetLastError()) + ")");
        return false;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(handle_, &dcb)) {
        logger_.Error("GetCommState failed for " + portName);
        Close();
        return false;
    }

    dcb.BaudRate = static_cast<DWORD>(baudRate);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(handle_, &dcb)) {
        logger_.Error("SetCommState failed for " + portName);
        Close();
        return false;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 200; // ms; lets ReadByte return on no activity
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 200;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    if (!SetCommTimeouts(handle_, &timeouts)) {
        logger_.Error("SetCommTimeouts failed for " + portName);
        Close();
        return false;
    }

    logger_.Info("Opened " + portName + " at " + std::to_string(baudRate) + " baud");
    return true;
}

void WindowsSerialPort::Close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

bool WindowsSerialPort::IsOpen() const {
    return handle_ != INVALID_HANDLE_VALUE;
}

bool WindowsSerialPort::ReadByte(uint8_t& outByte) {
    if (!IsOpen()) {
        return false;
    }

    DWORD bytesRead = 0;
    if (!ReadFile(handle_, &outByte, 1, &bytesRead, nullptr)) {
        logger_.Error("ReadFile failed (GetLastError=" + std::to_string(GetLastError()) + ")");
        return false;
    }
    return bytesRead == 1;
}

bool WindowsSerialPort::Write(const std::string& data) {
    return Write(std::vector<uint8_t>(data.begin(), data.end()));
}

bool WindowsSerialPort::Write(const std::vector<uint8_t>& data) {
    if (!IsOpen()) {
        return false;
    }

    DWORD bytesWritten = 0;
    if (!WriteFile(handle_, data.data(), static_cast<DWORD>(data.size()), &bytesWritten, nullptr)) {
        logger_.Error("WriteFile failed (GetLastError=" + std::to_string(GetLastError()) + ")");
        return false;
    }
    return bytesWritten == data.size();
}

} // namespace voipmodem
