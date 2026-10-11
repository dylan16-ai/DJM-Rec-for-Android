#pragma once
#include <cmath>
#include <cstdint>
#include <climits>

namespace djmrec {
// Measurements are evidence for a nominal clock, never a new PCM format.
inline int nominalUsbSampleRate(int measured) {
    constexpr int rates[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000,
                             44100, 48000, 88200, 96000, 176400, 192000};
    int closest = 0, distance = INT_MAX;
    for (int rate : rates) {
        const int delta = std::abs(measured - rate);
        if (delta < distance) { closest = rate; distance = delta; }
    }
    return static_cast<int64_t>(distance) * 100 <= closest * 3 ? closest : 0;
}

inline int usbPacketSampleRate(uint64_t frames, uint64_t packets, int packetsPerSecond) {
    if (!packets || packetsPerSecond <= 0) return 0;
    return nominalUsbSampleRate(static_cast<int>(std::llround(
        static_cast<double>(frames) * packetsPerSecond / packets)));
}

// The packet cadence is the rate the samples actually arrive at, so a nominal cadence wins over
// the rate a mixer profile or clock query expected: a mixer can accept a rate request and keep
// streaming its own clock (the DJM-750MK2 took 96 kHz and sent 48 kHz). Without a usable
// cadence, fall back to the expected rate, and to the wall clock only when packets are untimed.
inline int resolveUsbSampleRate(int expectedRate, int packetRate, int packetsPerSecond, int wallRate) {
    if (packetRate > 0) return packetRate;
    if (expectedRate > 0) return expectedRate;
    return packetsPerSecond > 0 ? 0 : nominalUsbSampleRate(wallRate);
}
}
