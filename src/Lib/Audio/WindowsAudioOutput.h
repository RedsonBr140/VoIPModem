#pragma once

#ifndef NOMINMAX
#define NOMINMAX // Windows.h's max/min macros break std::max/std::min for every includer of this header
#endif
#include <Windows.h>
#include <mmsystem.h>

// NOMINMAX (above) only helps if this is the first header to pull in
// Windows.h in a given translation unit; CallAudioBridge.cpp includes
// pjsua2.hpp first, which drags in Windows.h without NOMINMAX, so the
// macros can already exist by the time this file's own include runs.
// Undefining them unconditionally fixes it either way.
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#include <array>
#include <cstdint>
#include <vector>

#include "../Dsp/DspTypes.h"

namespace voipmodem {

// Plays already-synthesized/received call audio out the Windows
// default sound output device, so a human can actually hear this
// modem's handshake tones the way a real modem's on-board speaker used
// to let you hear dialing/negotiation. Unlike a real modem, there's no
// separate volume/device knob here at all -- the "speaker" is just
// whatever output device and volume Windows' own Sound settings (or
// the classic Volume Mixer) already control. See ModemProfile.h's own
// comment on ATL/ATM for the honest distinction: those commands are
// accepted so real modem-aware software doesn't get ERROR, but this
// class doesn't yet honor their values (always on, ignores volume) --
// a known simplification, not a claim this is a complete speaker
// emulation. See docs/ROADMAP.md Phase 4.
//
// Built on the classic winmm waveOut API (WAVE_MAPPER = system default
// device), not DirectSound -- simpler to drive correctly for one-way
// streaming PCM playback with no need for low-latency capture or
// hardware mixing. Multi-buffered: Write() copies into whichever
// buffer isn't currently queued with the driver and submits it via
// waveOutWrite(), silently dropping audio if every buffer is still in
// flight -- this is a best-effort monitor, not a lossless sink; it
// must never block or slow down the real call's actual audio path.
class WindowsAudioOutput {
public:
    WindowsAudioOutput();
    ~WindowsAudioOutput();

    WindowsAudioOutput(const WindowsAudioOutput&) = delete;
    WindowsAudioOutput& operator=(const WindowsAudioOutput&) = delete;

    // True once the default output device opened successfully. If
    // false, Write() is a safe no-op -- e.g. a headless test/CI
    // environment with no audio device at all.
    bool IsAvailable() const { return waveOut_ != nullptr; }

    // Queues `samples` (mono, kSampleRateHz, [-1, 1]) for playback.
    void Write(const std::vector<dsp::Sample>& samples);

private:
    static constexpr size_t kBufferCount = 4;

    struct Buffer {
        WAVEHDR header{};
        std::vector<std::int16_t> pcm;
    };

    HWAVEOUT waveOut_ = nullptr;
    std::array<Buffer, kBufferCount> buffers_;
    size_t nextBufferIndex_ = 0;
};

} // namespace voipmodem
