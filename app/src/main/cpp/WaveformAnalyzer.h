#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace djmrec {

/**
 * CDJ-3000-style 3-band waveform analyzer.
 *
 * Rather than an expensive full FFT, this uses 2nd-order IIR filters (low-pass, band-pass and
 * high-pass) — a lightweight visualization filter bank — to split the left and right signals
 * independently into three frequency bands. Each display bin (~6 ms at 48 kHz) keeps every
 * band's peak; a per-band release envelope and level curve turn those into rekordbox's 3-band
 * detail values (PWV7: low, mid, high, byte / 128). The curves were fitted against rekordbox's
 * own PWV7 tags (see kBandCurves). The result is a rolling 512-bin waveform where each bin
 * carries [amplitude, low, mid, high].
 *
 * Thread safety: pushFrames() is called from the realtime audio callback
 * (must not allocate/block); getBins() is called from the UI polling thread.
 * Individual bin values are atomic, so UI reads never block audio writes.
 * The reader snapshots a published cursor; an extra 512 slots protect its history
 * from concurrent writes. A bounded retry rejects a reader stalled for a full history.
 */
class WaveformAnalyzer {
public:
    static constexpr int kBinCount = 512;
    /** Samples accumulated per display bin at 48 kHz (~6.1 ms per bin). */
    explicit WaveformAnalyzer(int sampleRate = 48000);
    ~WaveformAnalyzer();

    WaveformAnalyzer(const WaveformAnalyzer&) = delete;
    WaveformAnalyzer& operator=(const WaveformAnalyzer&) = delete;

    /**
     * Called from the realtime audio callback (or libusb event thread) for
     * every captured frame batch. Analyzes stereo → mono, runs the three IIR
     * filters per-sample, and accumulates the peak and band peaks into the current
     * display bin. When kFramesPerBin samples have been accumulated the bin
     * is committed and the next bin begins.
     *
     * Must be allocation-free and lock-free — only atomic stores and trivial
     * float math inside.
     */
    void pushFrames(const int32_t* interleavedStereo, size_t frameCount);

    /**
     * Same as above for one track inside a wider interleaved frame: reads @p channels (1 or 2)
     * samples starting at @p offset of every @p stride-sample frame. A mono track is analyzed
     * as identical left and right so its bins match a stereo track's scale.
     */
    void pushFrames(const int32_t* interleaved, size_t frameCount, int stride, int offset, int channels);

    /**
     * Copies the most recently committed waveform snapshot into @p outBins,
     * which must be at least kBinCount * 4 floats. Layout:
     *   [amp0, low0, mid0, high0, amp1, low1, mid1, high1, ...]
     *
     * Amplitude is the bin's peak sample; low, mid and high are rekordbox 3-band levels
     * (PWV7 byte / 128). Each value is normalized to [0, 1]. Safe to call from any thread.
     */
    void getBins(float* outBins, uint32_t* sequence = nullptr) const;

    /** Resets all filter state and clears the bin buffer (e.g. on new session). */
    // Call only while the audio producer is stopped.
    void reset();
    float binDurationMillis() const { return mBinDurationMillis; }

    /**
     * rekordbox level of one band: byte / 128 = min(1, gain * envelope^exponent), where the
     * envelope is the band's peak with a release of releasePerEntry per PWV7 entry (1/150 s).
     */
    struct BandCurve {
        float gain;
        float exponent;
        float releasePerEntry;
    };
    static const BandCurve kBandCurves[3]; // low, mid, high

private:
    /** 2nd-order Butterworth IIR filter (Direct Form I) for a single band. */
    struct BandFilter {
        // Coefficients (set once at construction).
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f; // a0 is always 1.0 (normalized)
        // Delay line state.
        float x1 = 0.0f, x2 = 0.0f; // input history
        float y1 = 0.0f, y2 = 0.0f; // output history

        /** Process one sample, returning the filtered output. */
        float process(float x) {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }

        void resetState() { x1 = x2 = y1 = y2 = 0.0f; }
    };

    /**
     * Designs a 2nd-order Butterworth low-pass filter at @p cutoffHz for
     * @p sampleRate. Coefficients stored in @p f.
     */
    static void designLowPass(BandFilter& f, float cutoffHz, float sampleRate);

    /**
     * Designs a 2nd-order Butterworth high-pass filter at @p cutoffHz for
     * @p sampleRate. Coefficients stored in @p f.
     */
    static void designHighPass(BandFilter& f, float cutoffHz, float sampleRate);

    /** Accumulates a stereo frame through all three bands into the current bin. */
    void accumulateSample(float left, float right);

    /** Commits the current bin and advances to the next. */
    void commitBin();

    // Three IIR filters, designed at construction time for the actual sample rate.
    BandFilter mLowFilter[2];   // low-pass  → low band
    BandFilter mMidFilter1[2];  // high-pass  → mid = midFilter2(midFilter1(x))
    BandFilter mMidFilter2[2];  // low-pass
    BandFilter mHighFilter[2];  // high-pass → high band

    // Per-bin accumulators (written by realtime thread, swapped atomically).
    struct BinAccum {
        float peakAbs = 0.0f;    // max absolute amplitude this bin
        float bandPeak[3] = {};  // max |low|, |mid|, |high| over both channels
        int sampleCount = 0;
    };
    float mEnvelope[3] = {};     // realtime thread: band peak envelopes
    float mReleasePerBin[3] = {};

    // Atomic rolling history, oldest bin first when read.
    BinAccum mCurrent;
    static constexpr int kRingCount = kBinCount * 2;
    std::atomic<float> mBins[kRingCount * 4];
    std::atomic<uint32_t> mCommitted{0};
    int mFramesPerBin = 294;
    float mBinDurationMillis = 1000.0f / 163.0f;

    static constexpr float kMaxAmplitude = 2147483648.0f; // 2^31 for int32 → float norm
};

} // namespace djmrec
