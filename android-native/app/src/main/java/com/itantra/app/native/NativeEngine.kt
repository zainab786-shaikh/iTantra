package com.itantra.app.native

import com.itantra.app.config.LANGUAGES
import java.security.SecureRandom

/**
 * One clause of one utterance, as the native sender handled it
 * (`native/src/api/engine.h` ClauseSend). Built by JNI; the constructor's
 * parameter list is a JNI signature, so it changes only together with
 * `itantra-native.cpp`.
 */
class NativeClauseEncoding(
    /** `Tier1` | `Tier2` | `ClauseTooLong` | `NoEncoding` | `InvalidArgument` (`select/select.h`). */
    val outcome: String,
    /** 1 or 2 when [sent]; 0 otherwise. */
    val tier: Int,
    /** Why Tier 1 was not safe (`tier §8.1`); `None` when it was. */
    val trigger: String,
    /** 0 NORMAL, 1 CRITICAL — the value written into the payload (`packet §3.1`). */
    val priority: Int,
    /** [payload] is ready for the transport. False: nothing to send for this clause. */
    val sent: Boolean,
    /** Selected, but `commit()` refused it, so nothing was released. */
    val commitRefused: Boolean,
    /** The sealed native payload — ciphertext ‖ 4-byte tag (`packet §6.2`, §6.4). Opaque. */
    val payload: ByteArray,
    /** Plaintext native payload bytes: the golden-vector boundary, M-02. */
    val plaintextBytes: Int,
    /** Complete native packets tier selection compared (C-19); 0 = that tier had none. */
    val tier1PacketBytes: Int,
    val tier2PacketBytes: Int,
    /** The wide counter behind this payload's `seq` and nonce (`packet §3.6`); 0 when not sent. */
    val counter: Long,
    /** This clause's bytes in the UTF-8 utterance, `[textStart, textEnd)`. */
    val textStart: Int,
    val textEnd: Int,
)

/**
 * The receiver's output interface for one payload (`receiver §7`), built by JNI.
 *
 * **[unresolved] carries a contract** (`receiver §7.1`, C-31): a slot listed
 * there must not be presented as a value — not spoken, not displayed, not
 * defaulted. The native layer supplies no value for it, and [text] is empty.
 */
class NativeReceiveResult(
    /** `receive_outcome_name` — Delivered, Tier1Unresolved, AuthenticationFailed, Replayed, … */
    val outcome: String,
    /** False: nothing is output (authentication failure, replay — `receiver §7`, §9). */
    val emit: Boolean,
    /** [ReceiveStatus]. */
    val status: Int,
    /** UTF-8. Tier 1: rendered in the receiver's language. Tier 2: the sender's exact text. */
    val text: ByteArray,
    /** ISO 639-1 of the output's language id (`receiver §7.2`); null when none. */
    val languageCode: String?,
    /** 1 TIER_1, 2 TIER_2, 3 TIER_3 (a stub, never produced). */
    val mode: Int,
    /** 0 NORMAL, 1 CRITICAL. */
    val priority: Int,
    /** SlotId values — see [SLOT_NAMES]. */
    val unresolved: IntArray,
    val requestRepeat: Boolean,
    val requestSync: Boolean,
    val requestUnboostedResend: Boolean,
    /** The recovered wide counter. */
    val counter: Long,
    val contextCommitted: Boolean,
)

/** `receiver/output.h` OutputStatus, in declaration order. */
object ReceiveStatus {
    const val OK = 0
    const val CONTEXT_MISMATCH = 1
    const val INTEGRITY_FAIL = 2
    const val RENDER_FAIL = 3
}

/** Slot names by SlotId (`context` Appendix B). Labels only — never values. */
val SLOT_NAMES: List<String> = listOf("ACTOR", "OBJECT", "LOCATION", "SEVERITY", "QUANTITY", "TIME", "STATE", "LAST_REF")

/** The pack language (ISO 639-1) of an app language code: `hi-IN` → `hi`. */
fun packLanguageOf(appLanguage: String): String = appLanguage.substringBefore('-')

/** The app language code for a pack language, or null if the app lists none. */
fun appLanguageOf(packLanguage: String?): String? =
    packLanguage?.let { code -> LANGUAGES.firstOrNull { packLanguageOf(it.code) == code }?.code }

/**
 * The phone's native engine: packs, the sender's context and counter, the
 * receiver's session, and the keys. Created by [NativeBridge.createEngine].
 *
 * Calls are serialised on this object, so [close] can never free the engine
 * under a call in flight.
 */
class NativeEngine internal constructor(private var handle: Long) : AutoCloseable {

    /** Pack languages loaded (ISO 639-1). */
    val languages: List<String> = nativeLanguages(handle).toList()

    /** Whether a pack for [appLanguage] is loaded — needed to send in it, or to receive in it. */
    fun supports(appLanguage: String): Boolean = packLanguageOf(appLanguage) in languages

    /**
     * Start the single-device loopback session: session keys from a fresh random
     * PSK and HELLO nonces (`packet §6.7`), both contexts at their initial state,
     * the counter at 0. Local randomness means no two launches share a key, so a
     * counter restarting at 1 never repeats a nonce (C-33).
     *
     * A second phone cannot derive this key: PSK provisioning and HELLO are
     * Phase 12 (`context §18.1`).
     */
    @Synchronized
    fun beginLoopbackSession(random: SecureRandom = SecureRandom()) {
        val psk = ByteArray(32).also(random::nextBytes)
        val initiatorNonce = ByteArray(32).also(random::nextBytes)
        val responderNonce = ByteArray(32).also(random::nextBytes)
        try {
            nativeBeginLoopbackSession(open(), psk, initiatorNonce, responderNonce)
        } finally {
            psk.fill(0)
            initiatorNonce.fill(0)
            responderNonce.fill(0)
        }
    }

    /**
     * Encode one utterance: ONE JNI crossing, one result per clause, in order.
     * Languages are app codes (`en-IN`) or pack codes (`en`).
     */
    @Synchronized
    fun sendUtterance(
        utf8: ByteArray,
        senderLanguage: String,
        listenerLanguage: String,
        sttConfidence: Long,
        manualCritical: Boolean,
    ): List<NativeClauseEncoding> {
        val handle = open()
        NativeBridge.sendCrossings.incrementAndGet()
        return nativeSendUtterance(
            handle, utf8, packLanguageOf(senderLanguage), packLanguageOf(listenerLanguage), sttConfidence, manualCritical,
        ).toList()
    }

    /** Decode one native payload (`receiver §1.1`): ONE JNI crossing. */
    @Synchronized
    fun receive(receiverLanguage: String, payload: ByteArray): NativeReceiveResult {
        val handle = open()
        NativeBridge.receiveCrossings.incrementAndGet()
        return nativeReceive(handle, packLanguageOf(receiverLanguage), payload)
    }

    @Synchronized
    override fun close() {
        if (handle != 0L) {
            nativeDestroy(handle)
            handle = 0L
        }
    }

    private fun open(): Long {
        check(handle != 0L) { "native engine is closed" }
        return handle
    }

    private external fun nativeDestroy(handle: Long)
    private external fun nativeLanguages(handle: Long): Array<String>
    private external fun nativeBeginLoopbackSession(
        handle: Long,
        psk: ByteArray,
        initiatorNonce: ByteArray,
        responderNonce: ByteArray,
    )
    private external fun nativeSendUtterance(
        handle: Long,
        utf8: ByteArray,
        senderLanguage: String,
        listenerLanguage: String,
        sttConfidence: Long,
        manualCritical: Boolean,
    ): Array<NativeClauseEncoding>
    private external fun nativeReceive(handle: Long, receiverLanguage: String, payload: ByteArray): NativeReceiveResult
}
