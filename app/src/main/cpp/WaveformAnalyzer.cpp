#include "WaveformAnalyzer.h"

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace djmrec {

namespace {
// Band edges, fitted with kBandCurves against rekordbox's PWV7 (see below).
constexpr float kLowCutoffHz = 250.0f;
constexpr float kMidLowCutHz = 200.0f;
constexpr float kMidHighCutHz = 1500.0f;
constexpr float kHighCutoffHz = 2000.0f;
constexpr float kPwv7EntriesPerSecond = 150.0f;
}

// rekordbox 3-band levels (PWV7 byte / 128) from each band's peak envelope: low, mid, high.
// Fitted on a rekordbox USB export by comparing these filters' per-entry peaks with the
// tracks' own PWV7 tags. On 20 tracks outside the fit, the drawn band heights are on average
// within 3-4 % of the waveform height of rekordbox's (the previous mean-|x| and sqrt maths:
// 8 % low, 7 % mid, 16 % high).
const WaveformAnalyzer::BandCurve WaveformAnalyzer::kBandCurves[3] = {
    {109.8f / 128.0f, 1.0f, 0.955f},
    {110.8f / 128.0f, 1.0f, 0.915f},
    {104.5f / 128.0f, 1.65f, 0.875f},
};

// ---------------------------------------------------------------------------
// 2nd-order Butterworth coefficient design (bilinear transform).
// ---------------------------------------------------------------------------

void WaveformAnalyzer::designLowPass(BandFilter& f, float cutoffHz, float sampleRate) {
    const float w0 = 2.0f * static_cast<float>(M_PI) * cutoffHz / sampleRate;
    const float cosW0 = std::cos(w0);
    const float sinW0 = std::sin(w0);
    const float alpha = sinW0 / (2.0f * 0.70710678f); // Q = 1/sqrt(2) for Butterworth

    const float b0 = (1.0f - cosW0) / 2.0f;
    const float b1 = 1.0f - cosW0;
    const float b2 = (1.0f - cosW0) / 2.0f;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cosW0;
    const float a2 = 1.0f - alpha;

    f.b0 = b0 / a0;
    f.b1 = b1 / a0;
    f.b2 = b2 / a0;
    f.a1 = a1 / a0;
    f.a2 = a2 / a0;
}

void WaveformAnalyzer::designHighPass(BandFilter& f, float cutoffHz, float sampleRate) {
    const float w0 = 2.0f * static_cast<float>(M_PI) * cutoffHz / sampleRate;
    const float cosW0 = std::cos(w0);
    const float sinW0 = std::sin(w0);
    const float alpha = sinW0 / (2.0f * 0.70710678f);

    const float b0 = (1.0f + cosW0) / 2.0f;
    const float b1 = -(1.0f + cosW0);
    const float b2 = (1.0f + cosW0) / 2.0f;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cosW0;
    const float a2 = 1.0f - alpha;

    f.b0 = b0 / a0;
    f.b1 = b1 / a0;
    f.b2 = b2 / a0;
    f.a1 = a1 / a0;
    f.a2 = a2 / a0;
}

// ---------------------------------------------------------------------------
// Construction — design the three bands for the actual sample rate.
// ---------------------------------------------------------------------------

WaveformAnalyzer::WaveformAnalyzer(int sampleRate) {
    const float sr = static_cast<float>(std::max(sampleRate, 8000));
    mFramesPerBin = std::max(1, static_cast<int>(sr / 163.0f));
    mBinDurationMillis = 1000.0f * mFramesPerBin / sr;

    // Low band: up to 250 Hz.
    for (auto& filter : mLowFilter) designLowPass(filter, kLowCutoffHz, sr);

    // Mid band: 200–1500 Hz, a high-pass followed by a low-pass.
    for (auto& filter : mMidFilter1) designHighPass(filter, kMidLowCutHz, sr);
    for (auto& filter : mMidFilter2) designLowPass(filter, kMidHighCutHz, sr);

    // High band: from 2000 Hz.
    for (auto& filter : mHighFilter) designHighPass(filter, kHighCutoffHz, sr);

    // The curves' releases are per PWV7 entry; scale them to this analyzer's bin length.
    const float entriesPerBin = kPwv7EntriesPerSecond * mFramesPerBin / sr;
    for (int band = 0; band < 3; ++band) {
        mReleasePerBin[band] = std::pow(kBandCurves[band].releasePerEntry, entriesPerBin);
    }

    // Start with an empty rolling history.
    for (auto& value : mBins) value.store(0.0f, std::memory_order_relaxed);
}

WaveformAnalyzer::~WaveformAnalyzer() = default;

// ---------------------------------------------------------------------------
// Realtime push — called from audio callback / libusb event thread.
// ---------------------------------------------------------------------------

void WaveformAnalyzer::pushFrames(const int32_t* interleavedStereo, size_t frameCount) {
    for (size_t i = 0; i < frameCount; ++i) {
        // Mix stereo → mono (average L+R).
        const float left  = static_cast<float>(interleavedStereo[i * 2])     / kMaxAmplitude;
        const float right = static_cast<float>(interleavedStereo[i * 2 + 1]) / kMaxAmplitude;
        accumulateSample(left, right);
    }
}

void WaveformAnalyzer::pushFrames(const int32_t* interleaved, size_t frameCount, int stride,
                                  int offset, int channels) {
    if (!interleaved || stride <= 0 || offset < 0 || channels < 1 || offset + channels > stride) return;
    for (size_t i = 0; i < frameCount; ++i) {
        const int32_t* frame = interleaved + i * static_cast<size_t>(stride) + offset;
        const float left = static_cast<float>(frame[0]) / kMaxAmplitude;
        const float right = channels > 1 ? static_cast<float>(frame[1]) / kMaxAmplitude : left;
        accumulateSample(left, right);
    }
}

void WaveformAnalyzer::accumulateSample(float left, float right) {
    BinAccum& bin = mCurrent;
    // Never sum L+R before analysis: opposite-phase stereo is still real audio.
    const float samples[] = {left, right};
    for (int channel = 0; channel < 2; ++channel) {
        const float sample = samples[channel];
        const float low = mLowFilter[channel].process(sample);
        const float mid = mMidFilter2[channel].process(mMidFilter1[channel].process(sample));
        const float high = mHighFilter[channel].process(sample);
        bin.peakAbs = std::max(bin.peakAbs, std::fabs(sample));
        bin.bandPeak[0] = std::max(bin.bandPeak[0], std::fabs(low));
        bin.bandPeak[1] = std::max(bin.bandPeak[1], std::fabs(mid));
        bin.bandPeak[2] = std::max(bin.bandPeak[2], std::fabs(high));
    }
    bin.sampleCount++;

    if (bin.sampleCount >= mFramesPerBin) {
        commitBin();
    }
}

void WaveformAnalyzer::commitBin() {
    const uint32_t committed = mCommitted.load(std::memory_order_relaxed);
    const int index = committed % kRingCount;
    const int base = index * 4;
    mBins[base + 0].store(mCurrent.peakAbs, std::memory_order_relaxed);
    for (int band = 0; band < 3; ++band) {
        // Peak hold with release, then rekordbox's level curve for this band.
        mEnvelope[band] = std::max(mCurrent.bandPeak[band], mEnvelope[band] * mReleasePerBin[band]);
        const BandCurve& curve = kBandCurves[band];
        const float level = std::min(1.0f, curve.gain * std::pow(mEnvelope[band], curve.exponent));
        mBins[base + 1 + band].store(level, std::memory_order_relaxed);
    }
    mCommitted.store(committed + 1, std::memory_order_release);
    mCurrent = {};
}

// ---------------------------------------------------------------------------
// Reader — called from UI polling thread.
// ---------------------------------------------------------------------------

void WaveformAnalyzer::getBins(float* outBins, uint32_t* sequence) const {
    for (int attempt = 0; attempt < 3; ++attempt) {
        const uint32_t end = mCommitted.load(std::memory_order_acquire);
        for (int i = 0; i < kBinCount; ++i) {
            const int sourceBase = ((end - kBinCount + i) % kRingCount) * 4;
            for (int band = 0; band < 4; ++band) {
                outBins[i * 4 + band] = mBins[sourceBase + band].load(std::memory_order_relaxed);
            }
        }
        if (mCommitted.load(std::memory_order_acquire) - end < kBinCount) {
            if (sequence) *sequence = end;
            return;
        }
    }
    // A severely stalled reader must not display torn history.
    std::fill(outBins, outBins + kBinCount * 4, 0.0f);
    if (sequence) *sequence = mCommitted.load(std::memory_order_acquire);
}

void WaveformAnalyzer::reset() {
    for (auto& filter : mLowFilter) filter.resetState();
    for (auto& filter : mMidFilter1) filter.resetState();
    for (auto& filter : mMidFilter2) filter.resetState();
    for (auto& filter : mHighFilter) filter.resetState();

    mCurrent = {};
    std::fill(std::begin(mEnvelope), std::end(mEnvelope), 0.0f);
    for (auto& value : mBins) value.store(0.0f, std::memory_order_relaxed);
    mCommitted.store(0, std::memory_order_release);
}

} // namespace djmrec
