#include "WindowsAudioOutput.h"

#include <algorithm>

namespace voipmodem {

namespace {
constexpr int kChannels = 1;
constexpr int kBitsPerSample = 16;
} // namespace

WindowsAudioOutput::WindowsAudioOutput() {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(kChannels);
    format.nSamplesPerSec = static_cast<DWORD>(dsp::kSampleRateHz);
    format.wBitsPerSample = static_cast<WORD>(kBitsPerSample);
    format.nBlockAlign = static_cast<WORD>(kChannels * kBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    if (waveOutOpen(&waveOut_, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        waveOut_ = nullptr;
    }
}

WindowsAudioOutput::~WindowsAudioOutput() {
    if (waveOut_ == nullptr) {
        return;
    }
    waveOutReset(waveOut_);
    for (Buffer& buffer : buffers_) {
        if ((buffer.header.dwFlags & WHDR_PREPARED) != 0) {
            waveOutUnprepareHeader(waveOut_, &buffer.header, sizeof(WAVEHDR));
        }
    }
    waveOutClose(waveOut_);
}

void WindowsAudioOutput::Write(const std::vector<dsp::Sample>& samples) {
    if (waveOut_ == nullptr || samples.empty()) {
        return;
    }

    Buffer& buffer = buffers_[nextBufferIndex_];
    nextBufferIndex_ = (nextBufferIndex_ + 1) % kBufferCount;

    // Still queued with the driver (prepared but not yet marked done):
    // drop this write rather than block.
    if ((buffer.header.dwFlags & WHDR_PREPARED) != 0 && (buffer.header.dwFlags & WHDR_DONE) == 0) {
        return;
    }
    if ((buffer.header.dwFlags & WHDR_PREPARED) != 0) {
        waveOutUnprepareHeader(waveOut_, &buffer.header, sizeof(WAVEHDR));
    }

    buffer.pcm.resize(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        const float clamped = std::clamp(samples[i], -1.0f, 1.0f);
        buffer.pcm[i] = static_cast<std::int16_t>(clamped * 32767.0f);
    }

    buffer.header = WAVEHDR{};
    buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.pcm.data());
    buffer.header.dwBufferLength = static_cast<DWORD>(buffer.pcm.size() * sizeof(std::int16_t));

    if (waveOutPrepareHeader(waveOut_, &buffer.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        return;
    }
    waveOutWrite(waveOut_, &buffer.header, sizeof(WAVEHDR));
}

} // namespace voipmodem
