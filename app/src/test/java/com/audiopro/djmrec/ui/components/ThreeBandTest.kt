package com.audiopro.djmrec.ui.components

import com.audiopro.djmrec.ui.theme.WaveformCdjAll
import com.audiopro.djmrec.ui.theme.WaveformCdjLow
import com.audiopro.djmrec.ui.theme.WaveformCdjLowMid
import com.audiopro.djmrec.ui.theme.WaveformCdjMidHi
import org.junit.Assert.*
import org.junit.Test

class ThreeBandTest {
    @Test fun highBandUsesRekordboxRaisedCosine() {
        assertEquals(0f, ThreeBand.highExtent(0f), 0f)
        assertEquals(0.5f, ThreeBand.highExtent(0.5f), 1e-6f)
        assertEquals(1f, ThreeBand.highExtent(1f), 1e-6f)
        // The worked example of the CDJ 3-band maths: C(50) = 0.3315.
        assertEquals(0.3315f, ThreeBand.highExtent(50f / 128f), 1e-4f)
        assertEquals(0f, ThreeBand.highExtent(-1f), 0f)
        assertEquals(1f, ThreeBand.highExtent(2f), 1e-6f)
    }

    @Test fun lowAndMidAreLinear() {
        assertEquals(100f / 128f, ThreeBand.lowExtent(100f / 128f), 0f)
        assertEquals(60f / 128f, ThreeBand.midExtent(60f / 128f), 0f)
        assertEquals(1f, ThreeBand.lowExtent(1.5f), 0f)
    }

    @Test fun regionsTakeTheColourOfTheBandsReachingThem() {
        // Example 1 of the maths (PWV7 100, 60, 40): low longest, high shortest.
        val low = 100f / 128f
        val mid = 60f / 128f
        val high = ThreeBand.highExtent(40f / 128f)
        val longest = ThreeBand.longest(low, mid, high)
        val shortest = ThreeBand.shortest(low, mid, high)
        assertEquals(0, longest)
        assertEquals(2, shortest)
        assertEquals(WaveformCdjLow, ThreeBand.colours[1 shl longest])
        assertEquals(WaveformCdjLowMid, ThreeBand.colours[7 xor (1 shl shortest)])
        assertEquals(WaveformCdjAll, ThreeBand.colours[7])
        // Example 2 (40, 110, 90): mid tip, then mid + high, low only at the base.
        val lowB = 40f / 128f
        val midB = 110f / 128f
        val highB = ThreeBand.highExtent(90f / 128f)
        assertEquals(1, ThreeBand.longest(lowB, midB, highB))
        assertEquals(WaveformCdjMidHi, ThreeBand.colours[7 xor (1 shl ThreeBand.shortest(lowB, midB, highB))])
    }

    @Test fun tiesNeverPickTheSameBandAsLongestAndShortest() {
        val values = floatArrayOf(0f, 0.3f, 0.6f)
        for (l in values) for (m in values) for (h in values) {
            assertNotEquals("l=$l m=$m h=$h", ThreeBand.longest(l, m, h), ThreeBand.shortest(l, m, h))
        }
        // Ties go to low, then mid, as rekordbox's CDJ packing does.
        assertEquals(0, ThreeBand.longest(0.5f, 0.5f, 0.5f))
        assertEquals(1, ThreeBand.longest(0.2f, 0.5f, 0.5f))
    }
}
