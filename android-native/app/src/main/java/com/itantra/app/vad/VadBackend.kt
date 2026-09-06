package com.itantra.app.vad

/**
 * Direct port of src/core/vad/VadBackend.ts.
 *
 * A voice-activity detector: given one fixed-size frame of 16 kHz mono
 * audio, return the probability that it contains speech.
 *
 * Only EnergyVad is ported (see EnergyVad.kt) — the RN app's SileroVad.ts
 * is opt-in, requires a separately-installed ONNX model and the
 * onnxruntime-react-native peer dependency, and is never wired into any
 * controller (react-native-sherpa-onnx's own bundled VAD is a documented
 * placeholder that always throws, which is the entire reason
 * SileroVad.ts exists as an alternative — but nothing in the app actually
 * constructs and uses it). Per MIGRATION_AUDIT.md §F, EnergyVad is the
 * only VAD backend actually driving the segmenter in the shipped RN app,
 * so it is the only one migrated here.
 */
interface VadBackend {
    /** Load weights / allocate state. Must be safe to call twice. */
    fun initialize()

    /** @return speech probability in [0, 1]. */
    fun process(frame: FloatArray): Float

    /** Drop recurrent state between utterances. */
    fun reset()

    /** Free native resources. */
    fun dispose()
}
