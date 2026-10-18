#pragma once

namespace voipmodem::dsp {

// Everything in this module operates at the sample rate VoIPModem already
// negotiates end-to-end with no resampling anywhere in the chain
// (PjsipEndpoint::Start() sets clockRate = 8000 to match G.711 natively --
// see docs/ARCHITECTURE.md "RTP audio (Phase 3)"). Every primitive here
// assumes this rate rather than taking it as a runtime parameter, to keep
// call sites simple; if that ever needs to change, it changes here once.
constexpr int kSampleRateHz = 8000;

// Linear PCM, normalized to [-1.0, 1.0]. Conversion to/from the 16-bit
// integer PCM frames PJSIP's media layer actually hands over happens at
// the seam that plugs this module into PjsipCallController, not here.
using Sample = float;

} // namespace voipmodem::dsp
