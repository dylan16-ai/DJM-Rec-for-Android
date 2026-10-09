package com.audiopro.djmrec.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.audiopro.djmrec.ui.theme.BackgroundDark
import com.audiopro.djmrec.ui.theme.TextSecondary
import androidx.lifecycle.repeatOnLifecycle
import kotlinx.coroutines.isActive
import kotlinx.coroutines.flow.StateFlow

/** Collect here so waveform updates do not recompose the entire recorder workspace. */
@Composable
fun LiveRgbWaveform(source: StateFlow<FloatArray>, modifier: Modifier = Modifier,
                    smooth: Boolean = true, active: Boolean = true, onVisible: (Boolean) -> Unit = {}) {
    DisposableEffect(Unit) {
        onVisible(true)
        onDispose { onVisible(false) }
    }
    val bins by source.collectAsState()
    RgbWaveform(bins, modifier, smooth, active)
}

/** Fixed historical envelopes translate on the display frame clock, drawn as rekordbox's 3-band detail. */
@Composable
fun RgbWaveform(bins: FloatArray, modifier: Modifier = Modifier, smooth: Boolean = true, active: Boolean = true) {
    val timeline = remember { WaveformTimeline() }
    val bandHeights = remember { Array(3) { FloatArray(512) } }
    val cache = remember { LayeredPathCache() }
    val frame = remember { mutableLongStateOf(0L) }
    val owner = androidx.lifecycle.compose.LocalLifecycleOwner.current
    LaunchedEffect(bins) {
        timeline.accept(bins)
        for (i in 0 until 512) {
            val base = i * 4
            bandHeights[0][i] = ThreeBand.lowExtent(bins.level(base + 1))
            bandHeights[1][i] = ThreeBand.midExtent(bins.level(base + 2))
            bandHeights[2][i] = ThreeBand.highExtent(bins.level(base + 3))
        }
        cache.invalidate()
        if (!active || !smooth) frame.longValue = System.nanoTime()
    }
    LaunchedEffect(active, smooth, owner) {
        owner.lifecycle.repeatOnLifecycle(androidx.lifecycle.Lifecycle.State.STARTED) {
            val limiter = WaveformFrameLimiter()
            if (active && smooth) while (isActive) withFrameNanos {
                if (limiter.shouldRender(it)) frame.longValue = it
            }
        }
    }
    Canvas(modifier.fillMaxSize().clip(RoundedCornerShape(12.dp)).background(BackgroundDark)
        .semantics { contentDescription = "Live waveform in CDJ 3-band style. Blue = low, amber = mid, white = high, mixed where bands overlap. New audio enters at right." }) {
        val lag = timeline.lag(frame.longValue, smooth && active)
        val center = size.height / 2f
        for (line in 1..5) {
            val x = size.width * line / 6f
            drawLine(TextSecondary.copy(alpha = 0.09f), Offset(x, 0f), Offset(x, size.height))
        }
        for (line in 1..3) {
            val y = size.height * line / 4f
            drawLine(TextSecondary.copy(alpha = 0.09f), Offset(0f, y), Offset(size.width, y))
        }
        cache.ensureBuilt(bandHeights, size.width, center)
        if (timeline.bins.isNotEmpty()) clipRect {
            // rekordbox 3Band: each band reaches its own extent and the regions nest (see
            // ThreeBand). The paths are rebuilt only when analyzer data (or width) changes;
            // scrolling between updates is pure translation, so 60 fps stays smooth without
            // rebuilding every frame.
            translate(left = (lag - 24f) * cache.step) { cache.paths.draw(this) }
        }
    }
}

private fun FloatArray.level(index: Int): Float = getOrElse(index) { 0f }.let { if (it.isFinite()) it else 0f }

/**
 * Holds the band regions in bin space (x = binIndex * step). Rebuilt on data revisions and
 * resizes only -- per display frame the cached paths are just translated.
 */
private class LayeredPathCache {
    val paths = ThreeBandPaths()
    var step = 0f
        private set
    private var built = false
    private var builtWidth = -1f

    fun invalidate() {
        built = false
    }

    fun ensureBuilt(bands: Array<FloatArray>, width: Float, center: Float) {
        if (built && width == builtWidth) return
        step = width / 487f
        paths.reset()
        val (low, mid, high) = bands
        // 511 ramp segments; keep 24 bins of history so translation never resamples peaks.
        for (i in 0 until 511) {
            val x = i * step
            paths.addColumn(x, x + step + 0.25f, center, center * 0.91f,
                low[i], mid[i], high[i], low[i + 1], mid[i + 1], high[i + 1])
        }
        builtWidth = width
        built = true
    }
}
