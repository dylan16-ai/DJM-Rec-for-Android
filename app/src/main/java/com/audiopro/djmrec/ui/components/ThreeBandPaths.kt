package com.audiopro.djmrec.ui.components

import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import com.audiopro.djmrec.ui.theme.WaveformCdjAll
import com.audiopro.djmrec.ui.theme.WaveformCdjHi
import com.audiopro.djmrec.ui.theme.WaveformCdjLow
import com.audiopro.djmrec.ui.theme.WaveformCdjLowHi
import com.audiopro.djmrec.ui.theme.WaveformCdjLowMid
import com.audiopro.djmrec.ui.theme.WaveformCdjMid
import com.audiopro.djmrec.ui.theme.WaveformCdjMidHi
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min

/**
 * rekordbox's 3-band detail waveform (the CDJ-3000 "3Band" look). The analyzer's levels are
 * PWV7-style band values (byte / 128). Each band reaches its own distance from the axis -- low
 * and mid straight from their level, high through rekordbox's raised cosine -- and the regions
 * nest: every height takes the colour of the set of bands that reach it, so a column's base is
 * cream (all three) and its tip is the colour of the longest band alone.
 */
internal object ThreeBand {
    /** Extent of the high band: rekordbox's raised cosine C(h) = (1 - cos(pi h)) / 2. */
    fun highExtent(level: Float): Float =
        ((1.0 - cos(PI * level.coerceIn(0f, 1f))) / 2.0).toFloat()

    fun lowExtent(level: Float): Float = level.coerceIn(0f, 1f)
    fun midExtent(level: Float): Float = level.coerceIn(0f, 1f)

    /** Colours by the set of bands reaching a height: bit 0 low, bit 1 mid, bit 2 high. */
    val colours = arrayOf(
        WaveformCdjLow, WaveformCdjLow, WaveformCdjMid, WaveformCdjLowMid,
        WaveformCdjHi, WaveformCdjLowHi, WaveformCdjMidHi, WaveformCdjAll
    )

    /** Longest band first (ties go to low, then mid), as rekordbox's CDJ packing orders them. */
    fun longest(low: Float, mid: Float, high: Float): Int =
        if (low >= mid && low >= high) 0 else if (mid >= high) 1 else 2

    /** Shortest band, the tie order reversed so it never equals [longest]. */
    fun shortest(low: Float, mid: Float, high: Float): Int =
        if (high <= mid && high <= low) 2 else if (mid <= low) 1 else 0
}

/**
 * The nested regions as one path per band set. Each column is painted outside-in: the longest
 * band's span, then the two longest bands' span over it, then the all-three span, so the
 * regions share no anti-aliased seams.
 */
internal class ThreeBandPaths {
    private val paths = Array(8) { Path() }

    fun reset() = paths.forEach { it.reset() }

    /**
     * Adds the column from [x0] to [x1]. Extents are fractions of [height] above and below
     * [center]: at the left edge ([low], [mid], [high]) and at the right edge (the `next`
     * values), sorted per edge so a ramp between two columns stays continuous. The left edge
     * decides the colours.
     */
    fun addColumn(
        x0: Float, x1: Float, center: Float, height: Float,
        low: Float, mid: Float, high: Float,
        lowNext: Float = low, midNext: Float = mid, highNext: Float = high
    ) {
        val first = ThreeBand.longest(low, mid, high)
        val outer = 1 shl first
        val middle = 7 xor (1 shl ThreeBand.shortest(low, mid, high))
        val a1 = max(low, max(mid, high))
        val a3 = min(low, min(mid, high))
        val b1 = max(lowNext, max(midNext, highNext))
        val b3 = min(lowNext, min(midNext, highNext))
        span(paths[outer], x0, x1, center, a1 * height, b1 * height)
        span(paths[middle], x0, x1, center, (low + mid + high - a1 - a3) * height,
            (lowNext + midNext + highNext - b1 - b3) * height)
        span(paths[7], x0, x1, center, a3 * height, b3 * height)
    }

    fun draw(scope: DrawScope) {
        for (set in DRAW_ORDER) scope.drawPath(paths[set], ThreeBand.colours[set])
    }

    private fun span(path: Path, x0: Float, x1: Float, center: Float, left: Float, right: Float) {
        if (left < MIN_SPAN && right < MIN_SPAN) return
        path.moveTo(x0, center - left)
        path.lineTo(x1, center - right)
        path.lineTo(x1, center + right)
        path.lineTo(x0, center + left)
        path.close()
    }

    private companion object {
        /** Single bands, then pairs, then all three: each layer lies inside the one before. */
        val DRAW_ORDER = intArrayOf(1, 2, 4, 3, 5, 6, 7)
        const val MIN_SPAN = 0.25f
    }
}
