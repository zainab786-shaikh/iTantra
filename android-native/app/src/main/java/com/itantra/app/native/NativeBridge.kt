package com.itantra.app.native

import android.content.res.AssetManager
import android.util.Log
import java.util.concurrent.atomic.AtomicLong

private const val TAG = "NativeBridge"

/**
 * The JNI boundary (implementation plan Phase 11): loads `libitantra-native.so`
 * and creates the phone's [NativeEngine].
 *
 * ```
 * Kotlin   audio · STT · TTS · transport · UI          owns
 *            │ one call per utterance   ▲ one result per clause
 *            ▼                          │
 * C++      extraction · tier selection · coder · AEAD  owns the context, the
 *          counter · receiver pipeline · commit         models, the keys
 * ```
 *
 * Coarse on purpose (handoff "JNI BOUNDARY"): a send crosses once per utterance
 * and a receive once per payload — never per token or per symbol. Kotlin never
 * parses a native payload (`packet §1.1`); what it learns about one comes back
 * from these calls.
 *
 * The Phase 0 stubs (`processText`, `classifyPriority`, `compressPayload`,
 * `decompressPayload`, `getNativeVersion`) are gone. None implemented specified
 * behaviour, and `classifyPriority` still used the retired four-band scale.
 */
object NativeBridge {

    private var isLoaded = false

    init {
        try {
            System.loadLibrary("itantra-native")
            isLoaded = true
            Log.i(TAG, "Successfully loaded native C++ library: libitantra-native.so")
        } catch (e: UnsatisfiedLinkError) {
            Log.e(TAG, "Failed to load native C++ library libitantra-native.so", e)
        }
    }

    fun isNativeLoaded(): Boolean = isLoaded

    /**
     * The STT confidence to pass when the recogniser reports none.
     *
     * Below every language pack's threshold, so such input can never select
     * Tier 1 (`tier §5.8` "low input confidence disables Tier 1 outright",
     * contract C-25). No confidence is invented. The confidence scale itself is
     * still an open item (language spec implementation resolutions).
     */
    const val CONFIDENCE_UNAVAILABLE: Long = Long.MIN_VALUE

    /** Where app/build.gradle.kts packages the packs inside the APK. */
    const val PACK_ASSET_ROOT = "itantra/packs"

    /**
     * JNI crossings on the send and receive paths. They exist for the boundary
     * test ("JNI boundary is one call per clause, not per token") and are not
     * read anywhere else.
     */
    val sendCrossings = AtomicLong()
    val receiveCrossings = AtomicLong()

    fun version(): String = nativeVersion()

    /**
     * Every file under [root], keyed by its path relative to [root] —
     * `common/concepts.bin`, `lang/en/lexicon.bin`, … — which is the layout
     * the native engine loads.
     */
    fun readPacks(assets: AssetManager, root: String = PACK_ASSET_ROOT): Map<String, ByteArray> {
        val out = sortedMapOf<String, ByteArray>()
        fun walk(relative: String) {
            val path = if (relative.isEmpty()) root else "$root/$relative"
            val children = assets.list(path).orEmpty()
            if (children.isEmpty()) {
                out[relative] = assets.open(path).use { it.readBytes() }
            } else {
                for (child in children) walk(if (relative.isEmpty()) child else "$relative/$child")
            }
        }
        walk("")
        return out
    }

    /** Load [packs] into a new engine. Throws with the loader's reason if they do not validate. */
    fun createEngine(packs: Map<String, ByteArray>): NativeEngine {
        check(isLoaded) { "libitantra-native.so is not loaded" }
        val names = packs.keys.sorted()
        val handle = nativeCreate(names.toTypedArray(), Array(names.size) { packs.getValue(names[it]) })
        return NativeEngine(handle)
    }

    private external fun nativeVersion(): String
    private external fun nativeCreate(names: Array<String>, contents: Array<ByteArray>): Long
}
