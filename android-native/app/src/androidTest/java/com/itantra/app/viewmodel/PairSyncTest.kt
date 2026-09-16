package com.itantra.app.viewmodel

import android.app.Application
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.itantra.app.native.NativeBridge
import com.itantra.app.native.NativeReceiveResult
import com.itantra.app.native.ReceiveStatus
import com.itantra.app.packet.ITantraPacket
import com.itantra.app.transport.UdpTransport
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.Random

private const val TAG = "PairSyncTest"

/** Messages per run (C-10: 200). */
private const val MESSAGES = 200

/** The sender's language; the receiver's comes from the `lang` argument. */
private const val SENDER_LANGUAGE = "en-IN"

/**
 * One cycle of the fixed script. Single clauses only, so message i carries counter i + 1.
 * Repeated locations make the sender inherit (and send the context hash); Tier 2 lines
 * exercise the text context rule (tier §11.3).
 */
private val SCRIPT = listOf(
    "Send an ambulance to the hospital" to 900L,
    "Send medicine to the hospital" to 900L,
    "Send water to the hospital" to 900L,
    "Fire at the north gate" to 900L,
    "Send an ambulance to the hospital" to 900L,
    "all is quiet here" to NativeBridge.CONFIDENCE_UNAVAILABLE,
    "Send medicine to the hospital" to 900L,
    "Police move away" to 900L,
    "The weather is good today" to NativeBridge.CONFIDENCE_UNAVAILABLE,
    "Send water to the hospital" to 900L,
)

/** C-11: 5% loss, deterministic so both phones know the schedule. The last message always arrives. */
private val LOST: Set<Int> = Random(1205).let { r -> (0 until MESSAGES - 1).filter { r.nextInt(100) < 5 }.toSet() }

/** C-12: message i (i % 20 == 3) is held and released after message i + 1. */
private fun heldForReorder(i: Int) = i % 20 == 3

/** C-13: message i (i % 10 == 7) is put on the air twice. */
private fun duplicated(i: Int) = i % 10 == 7

/** Refresh points (native `kContextRefreshInterval`, context §16.1): both phones reset the context there. */
private const val REFRESH_INTERVAL = 16L

/** C-42: previously accepted messages replayed after the run — one inside the window, one far behind it. */
private val REPLAYED = listOf(150, 2)

/**
 * [P] Phase 12 pair conformance over the real UDP link: C-10 … C-14, C-16, C-35, C-42.
 *
 * Faults are injected at the sender through [UdpTransport.outboundFaults] (the lossy
 * proxy); the receiver checks every native result. Run [receive] on one phone first,
 * then [send] on the other, both with the same `-e scenario clean|loss|reorder|dup`
 * and `-e lang <receiver language>`. `clean` must run first for a language: it records
 * the reference renderings and context hashes the other scenarios are judged against
 * (sender and receiver hashes are logged as `SYNC tx` / `SYNC rx` for C-10's comparison).
 */
@RunWith(AndroidJUnit4::class)
class PairSyncTest {

    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val scenario = InstrumentationRegistry.getArguments().getString("scenario") ?: "clean"
    private val receiverLanguage = InstrumentationRegistry.getArguments().getString("lang") ?: "en-IN"
    private val crossLanguage = receiverLanguage != SENDER_LANGUAGE

    private fun app(): AppViewModel {
        lateinit var app: AppViewModel
        instrumentation.runOnMainSync { app = AppViewModel(context.applicationContext as Application) }
        return app
    }

    private fun await(what: String, timeoutMs: Long, condition: () -> Boolean) {
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            if (condition()) return
            Thread.sleep(100)
        }
        fail("timed out waiting for $what")
    }

    private class Row(val r: NativeReceiveResult, val hashBefore: Int, val hashAfter: Int) {
        val text get() = String(r.text, Charsets.UTF_8)
    }

    private fun referenceFile() = File(context.filesDir, "pair-reference-$receiverLanguage.tsv")

    @Test
    fun receive() {
        val app = app()
        val rows = mutableListOf<Row>()
        var hash = app.contextHashes()!![1]
        app.receiver.onNativeResult = { r ->
            val after = app.contextHashes()!![1]
            synchronized(rows) { rows.add(Row(r, hash, after)) }
            Log.i(TAG, "SYNC rx counter=${r.counter} outcome=${r.outcome} status=${r.status} tier=${r.mode} " +
                "emit=${r.emit} sync=${r.requestSync} committed=${r.contextCommitted} " +
                "unresolved=${r.unresolved.joinToString(",")} hash=${"%04x".format(after)} \"${String(r.text, Charsets.UTF_8)}\"")
            hash = after
        }
        instrumentation.runOnMainSync { app.receiver.setLanguage(receiverLanguage) }
        await("session", 120_000) { app.sessionReady }

        val expected = when (scenario) {
            "loss" -> MESSAGES - LOST.size
            "dup" -> MESSAGES + (0 until MESSAGES).count(::duplicated) + REPLAYED.size
            else -> MESSAGES
        }
        await("$expected results", 600_000) { synchronized(rows) { rows.size >= expected } }
        Thread.sleep(5_000)
        val all = synchronized(rows) { rows.toList() }
        assertEquals("results", expected, all.size)

        if (scenario == "clean") {
            // C-10: no loss → every message delivered, zero sync requests.
            assertTrue("every message delivered",
                all.all { it.r.emit && (it.r.outcome == "Delivered" || (crossLanguage && it.r.outcome == "RenderFailed")) })
            assertTrue("zero sync requests", all.none { it.r.requestSync })
            assertEquals("counters 1 … $MESSAGES in order", (1L..MESSAGES).toList(), all.map { it.r.counter })
            // Tier 2 context rule (tier §11.3, C-35 receiver half): committed iff same language.
            all.filter { it.r.mode == 2 }.forEach {
                assertEquals("Tier 2 commit iff same language (counter ${it.r.counter})", !crossLanguage, it.r.contextCommitted)
                if (crossLanguage) assertEquals("C-35: receiver context unchanged", it.hashBefore, it.hashAfter)
                assertEquals("C-09: Tier 2 in the sender's language", "en", it.r.languageCode)
            }
            if (crossLanguage) {
                assertTrue("C-08: Tier 1 in the receiver's language",
                    all.filter { it.r.mode == 1 && it.r.status == ReceiveStatus.OK }.all { it.r.languageCode == receiverLanguage.substringBefore('-') })
            }
            referenceFile().writeText(all.joinToString("\n") { "${it.r.counter}\t${"%04x".format(it.hashAfter)}\t${it.text}" })
            Log.i(TAG, "SYNC clean: reference written, final hash ${"%04x".format(all.last().hashAfter)}")
            return
        }

        val reference = referenceFile().readLines().associate { line ->
            val (counter, h, text) = line.split('\t', limit = 3)
            counter.toLong() to (h to text)
        }
        assertEquals("reference from the clean run", MESSAGES, reference.size)

        val decoded = all.filter { it.r.emit }
        // C-11 / C-12 pass criterion: nothing is ever delivered with a wrong value — every
        // delivered text is exactly what the loss-free run delivered for that message.
        val wrong = decoded.filter { it.r.status == ReceiveStatus.OK && it.text != reference.getValue(it.r.counter).second }
        wrong.forEach { Log.e(TAG, "SYNC WRONG counter=${it.r.counter} \"${it.text}\" expected \"${reference.getValue(it.r.counter).second}\"") }
        assertTrue("no message delivered with a wrong value", wrong.isEmpty())
        // C-14: a hash mismatch decodes, leaves only slots unresolved, renders nothing, commits nothing.
        decoded.filter { it.r.status == ReceiveStatus.CONTEXT_MISMATCH }.forEach {
            assertTrue("mismatch: no text", it.text.isEmpty())
            assertTrue("mismatch: slots named", it.r.unresolved.isNotEmpty())
            assertTrue("mismatch: not committed", !it.r.contextCommitted)
            assertEquals("mismatch: context unchanged", it.hashBefore, it.hashAfter)
        }
        val mismatches = decoded.count { it.r.status == ReceiveStatus.CONTEXT_MISMATCH }
        val diverged = decoded.count { "%04x".format(it.hashAfter) != reference.getValue(it.r.counter).first }
        Log.i(TAG, "SYNC $scenario: ${all.size} results, ${decoded.count { it.r.status == ReceiveStatus.OK }} delivered, " +
            "$mismatches context mismatches, ${all.count { it.r.requestSync }} sync requests, " +
            "$diverged messages after which the context differed from the loss-free run, " +
            "final hash ${"%04x".format(all.last().hashAfter)} (loss-free ${reference.getValue(MESSAGES.toLong()).first})")

        /**
         * C-11 / C-12 recovery and C-16: after every refresh that arrived in order the context
         * equals the loss-free run's; from the first refresh after the last disruption on,
         * every message is delivered, inheritance included, with identical contexts.
         */
        fun assertRecovered(lastDisrupted: Long, outOfOrder: Set<Long>) {
            assertTrue("divergence detected", mismatches > 0 && all.any { it.r.requestSync })
            val refreshes = all.filter { it.r.counter % REFRESH_INTERVAL == 0L && it.r.counter !in outOfOrder }
            assertTrue("refreshes received", refreshes.isNotEmpty())
            refreshes.forEach {
                assertEquals("context after refresh ${it.r.counter} equals the loss-free run",
                    reference.getValue(it.r.counter).first, "%04x".format(it.hashAfter))
            }
            val recoveredAt = (lastDisrupted / REFRESH_INTERVAL + 1) * REFRESH_INTERVAL
            val after = all.filter { it.r.counter >= recoveredAt }
            assertTrue("messages after recovery point $recoveredAt", after.isNotEmpty())
            after.forEach {
                assertEquals("C-16: delivered after recovery (counter ${it.r.counter})", "Delivered", it.r.outcome)
                assertEquals("C-16: context identical after recovery (counter ${it.r.counter})",
                    reference.getValue(it.r.counter).first, "%04x".format(it.hashAfter))
            }
            assertEquals("final context equals the loss-free run", reference.getValue(MESSAGES.toLong()).first, "%04x".format(all.last().hashAfter))
            Log.i(TAG, "SYNC $scenario: recovered at refresh $recoveredAt; ${after.size} messages after it all delivered with identical contexts")
        }

        when (scenario) {
            "loss" -> {
                assertEquals("counters", (1L..MESSAGES).filter { (it - 1).toInt() !in LOST }, all.map { it.r.counter })
                assertRecovered(LOST.max() + 1L, emptySet())
            }
            "reorder" -> {
                assertTrue("C-12: every message handled", all.all { it.r.emit })
                val held = (0 until MESSAGES - 1).filter(::heldForReorder).map { it + 1L }
                assertRecovered(held.max() + 1L, held.toSet())
            }
            "dup" -> {
                val rejected = all.filter { !it.r.emit }
                assertEquals("C-13 / C-42: duplicates and replays rejected", (0 until MESSAGES).count(::duplicated) + REPLAYED.size, rejected.size)
                rejected.forEach {
                    assertTrue("rejected silently (${it.r.outcome})", it.r.outcome == "Replayed" || it.r.outcome == "AuthenticationFailed")
                    assertTrue("no repair request", !it.r.requestSync)
                    assertEquals("no context change", it.hashBefore, it.hashAfter)
                }
                assertTrue("C-13: duplicates caught by the replay window", rejected.count { it.r.outcome == "Replayed" } >= (0 until MESSAGES).count(::duplicated) + 1)
                assertTrue("every original delivered", decoded.all { it.r.outcome == "Delivered" })
                assertEquals("context identical to the loss-free run throughout", 0, diverged)
            }
        }
    }

    @Test
    fun send() {
        val app = app()
        val udp = app.link as UdpTransport
        await("session with a ${receiverLanguage} receiver", 120_000) { app.sessionReady && app.peerLanguage == receiverLanguage }
        Thread.sleep(3_000)
        val throttleWasOn = app.throttle.enabled.value
        instrumentation.runOnMainSync { app.throttle.setEnabled(false) }

        var index = 0
        var held: ITantraPacket? = null
        udp.outboundFaults = { packet ->
            val i = index
            when (scenario) {
                "loss" -> if (i in LOST) emptyList() else listOf(packet)
                "reorder" -> when {
                    heldForReorder(i) -> { held = packet; emptyList() }
                    held != null -> listOf(packet, held!!).also { held = null }
                    else -> listOf(packet)
                }
                "dup" -> if (duplicated(i)) listOf(packet, packet) else listOf(packet)
                else -> listOf(packet)
            }
        }
        val sent = mutableListOf<ITantraPacket>()
        try {
            runBlocking {
                repeat(MESSAGES) { i ->
                    index = i
                    val (text, confidence) = SCRIPT[i % SCRIPT.size]
                    val before = app.contextHashes()!![0]
                    val built = app.transmitter.transmit(text, SENDER_LANGUAGE, confidence, 0, false).packets.single()
                    val after = app.contextHashes()!![0]
                    val tier = built.native!!.tier
                    assertEquals("message $i carries counter ${i + 1}", (i + 1).toLong(), built.native!!.counter)
                    if (tier == 2 && crossLanguage) assertEquals("C-35: sender context unchanged by cross-language Tier 2", before, after)
                    Log.i(TAG, "SYNC tx counter=${i + 1} tier=$tier hash=${"%04x".format(after)} \"$text\"")
                    sent.add(built.packet)
                    Thread.sleep(150)
                }
                udp.outboundFaults = null
                if (scenario == "dup") {
                    for (i in REPLAYED) {
                        Thread.sleep(500)
                        Log.i(TAG, "SYNC tx replay of counter ${i + 1}")
                        udp.sendPacket(sent[i])
                    }
                }
            }
        } finally {
            udp.outboundFaults = null
            instrumentation.runOnMainSync { app.throttle.setEnabled(throttleWasOn) }
        }
        Thread.sleep(3_000)
    }
}
