#include "UsbSampleRate.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace djmrec;
    assert(nominalUsbSampleRate(99271) == 0); // Must never become a PCM format.
    assert(usbPacketSampleRate(48000, 4000, 8000) == 96000);
    assert(usbPacketSampleRate(47952, 4000, 8000) == 96000); // Four startup empties.
    assert(usbPacketSampleRate(24000, 4000, 8000) == 48000);
    assert(usbPacketSampleRate(22050, 500, 1000) == 44100);
    assert(usbPacketSampleRate(48000, 2000, 4000) == 96000); // bInterval=2.
    assert(usbPacketSampleRate(0, 4000, 8000) == 0);
    assert(usbPacketSampleRate(48000, 0, 8000) == 0);
    // DJM-750MK2 (issue #4): 216-byte packets of 12ch x 3 bytes = 6 frames per microframe.
    assert(usbPacketSampleRate(4000 * 216 / 36, 4000, 8000) == 48000);
    // The measured cadence beats the expected rate: the mixer took a 96 kHz request and sent 48 kHz.
    assert(resolveUsbSampleRate(96000, 48000, 8000, 47990) == 48000);
    assert(resolveUsbSampleRate(48000, 48000, 8000, 0) == 48000);
    // Without a nominal cadence the expected rate stands.
    assert(resolveUsbSampleRate(96000, 0, 8000, 0) == 96000);
    // Timed packets with no nominal cadence never fall back to a wall-clock guess.
    assert(resolveUsbSampleRate(0, 0, 8000, 48000) == 0);
    assert(resolveUsbSampleRate(0, 0, 0, 44110) == 44100);
    assert(resolveUsbSampleRate(0, 0, 0, 99271) == 0);
    std::cout << "USB packet-rate checks passed, including 99271 Hz rejection and measured-rate priority\n";
}
