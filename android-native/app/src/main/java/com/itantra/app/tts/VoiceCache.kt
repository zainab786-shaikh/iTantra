package com.itantra.app.tts

/**
 * Which TTS voices stay resident on the receiver (Phase 14.1).
 *
 * Voices are per language (language-layer spec §4.5), and a Tier 1 message renders in
 * the receiver's own language while Tier 2 arrives in the sender's (§10.1). Holding one
 * voice made every change of output language reload a voice — 1.0–1.1 s on the S22 and
 * 2.1–2.5 s on the S7 (Phase 13). This keeps at most [capacity] voices:
 *
 *   - the **primary** voice — the receiver's own (mother-tongue) render language — which
 *     is never evicted while it is primary, and
 *   - the **most recently used** other voice(s).
 *
 * A miss evicts the least recently used non-primary voice before the new one is created,
 * so no more than [capacity] voices are ever resident, even during a load. A voice is only
 * inserted once [load] has returned it whole: a load that throws leaves the cache exactly
 * as it was, and nothing can ever be handed out partially loaded.
 *
 * Capacity 2 was chosen against the S7's measured memory (Phase 14.1 RAM gate): two
 * resident MMS voices beside an IndicConformer STT model peaked at 776 MB process RSS
 * with 1.5 GB still available and Android's low-memory flag clear.
 *
 * Not thread-safe by itself beyond its own state: [get] runs on the TTS queue's drain
 * path, the only place voices are used, so an evicted voice is never in use.
 */
class VoiceCache<V : Any>(
    private val capacity: Int = 2,
    private val load: (id: String) -> V,
    private val release: (V) -> Unit,
) {
    init {
        require(capacity >= 1) { "capacity must be at least 1" }
    }

    /** Access order: least recently used first. */
    private val voices = LinkedHashMap<String, V>(capacity + 1, 0.75f, true)

    /** The voice id that is never evicted (the receiver's render language); null: plain LRU. */
    @get:Synchronized @set:Synchronized
    var primary: String? = null

    /** Voices created so far — a cache miss is a load. */
    @get:Synchronized
    var loads: Int = 0
        private set

    /** Requests served by an already-resident voice. */
    @get:Synchronized
    var hits: Int = 0
        private set

    @Synchronized
    fun residentIds(): List<String> = voices.keys.toList()

    /** The resident voice for [id], loading it (and evicting if full) on a miss. */
    @Synchronized
    fun get(id: String): V {
        voices[id]?.let {
            hits++
            return it
        }
        if (voices.size >= capacity) evictOne()
        val voice = load(id)
        loads++
        voices[id] = voice
        return voice
    }

    /** Release every resident voice. */
    @Synchronized
    fun clear() {
        val all = voices.values.toList()
        voices.clear()
        all.forEach(release)
    }

    private fun evictOne() {
        val victim = voices.keys.firstOrNull { it != primary } ?: voices.keys.first()
        voices.remove(victim)?.let(release)
    }
}
