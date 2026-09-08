package com.itantra.app.codec

import com.itantra.app.transport.PacketCodec
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Prints the real payload sizes the codec produces, so the numbers quoted in
 * the demo are measured rather than predicted.
 *
 * `prototype-for-demo.md` §6 carries a table of *target* figures computed
 * from the encoding formulas, with an explicit instruction not to quote them
 * as results until they have been measured. This is the measurement.
 *
 * Run: ./gradlew :app:testDebugUnitTest --tests '*MeasurementTest'
 */
class MeasurementTest {

    private val codec = ITantraCodec()

    /** Airtime at a given link rate is arithmetic: bytes x 8 / bits per second. */
    private fun seconds(bytes: Int, bitsPerSecond: Int): Double =
        bytes * 8.0 / bitsPerSecond

    @Test
    fun `measured payload sizes and airtime`() {
        val cases = listOf(
            // Free speech - whatever the operator happens to say.
            Triple("Hindi", "hi-IN", "उत्तर द्वार पर आग लग गई है तुरंत मदद भेजो"),
            Triple("Tamil", "ta-IN", "வடக்கு வாசலில் தீ ஆபத்து உடனடி உதவி"),
            Triple("Telugu", "te-IN", "ఉత్తర ద్వారం వద్ద మంటలు వెంటనే సహాయం పంపండి"),
            Triple("English", "en-IN", "requesting backup at checkpoint three"),
            // Known phrases - the 2-byte path.
            Triple("Hindi", "hi-IN", PhraseDictionary.surfaceFor(1, "hi-IN")!!),
            Triple("Marathi", "mr-IN", PhraseDictionary.surfaceFor(1, "mr-IN")!!),
            Triple("Bengali", "bn-IN", PhraseDictionary.surfaceFor(1, "bn-IN")!!),
            Triple("English", "en-IN", PhraseDictionary.surfaceFor(1, "en-IN")!!),
        )

        val bps = 250
        println()
        println("| Language | Sentence | UTF-8 | Payload | Mode | Ratio | Frame | Raw @${bps}bps | Sent @${bps}bps |")
        println("|---|---|---:|---:|---|---:|---:|---:|---:|")

        for ((label, language, text) in cases) {
            val encoded = codec.encode(text, language)
            val raw = encoded.originalBytes
            val payload = encoded.bytes.size
            val frame = PacketCodec.HEADER_BYTES + payload
            val ratio = raw.toDouble() / payload

            println(
                "| %s | %s | %d B | %d B | %s | %.2fx | %d B | %.2f s | %.2f s |".format(
                    label, text, raw, payload, encoded.mode.name, ratio, frame,
                    seconds(raw, bps), seconds(frame, bps),
                )
            )

            assertTrue("$language never grows", payload <= raw)
        }
        println()
        println("Frame = payload + ${PacketCodec.HEADER_BYTES} B header.")
        println("'Raw @${bps}bps' is the UTF-8 text alone; 'Sent' is the full transmitted frame.")
        println()
    }
}
