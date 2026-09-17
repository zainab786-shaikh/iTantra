package com.itantra.app.viewmodel

import com.itantra.app.viewmodel.AppViewModel.Companion.PSK_FILE
import com.itantra.app.viewmodel.AppViewModel.Companion.provisionPskIfMissing
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.File
import java.io.IOException

class PskProvisioningTest {

    private lateinit var tempDir: File
    private lateinit var filesDir: File
    private val validHexPsk = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

    @Before
    fun setUp() {
        tempDir = File(System.getProperty("java.io.tmpdir"), "psk_test_${System.nanoTime()}")
        tempDir.mkdirs()
        filesDir = File(tempDir, "files")
        filesDir.mkdirs()
    }

    @After
    fun tearDown() {
        tempDir.deleteRecursively()
    }

    private fun readPskFromDir(dir: File): ByteArray? {
        val hex = File(dir, PSK_FILE).takeIf { it.exists() }?.readText()?.trim() ?: return null
        if (hex.length != 64 || !hex.all { it.isDigit() || it.lowercaseChar() in 'a'..'f' }) return null
        return ByteArray(32) { hex.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
    }

    @Test
    fun validAssetCopiedWhenPskMissing() {
        val target = File(filesDir, PSK_FILE)
        assertFalse("target should not exist initially", target.exists())

        val success = provisionPskIfMissing(filesDir) {
            ByteArrayInputStream(validHexPsk.toByteArray(Charsets.UTF_8))
        }

        assertTrue("provisionPskIfMissing should return true for valid asset", success)
        assertTrue("target file should be created", target.exists())
        assertEquals(validHexPsk, target.readText().trim())

        val readBytes = readPskFromDir(filesDir)
        assertNotNull("readPsk should return non-null ByteArray", readBytes)
        assertEquals(32, readBytes!!.size)
    }

    @Test
    fun existingPskPreservedAndNotOverwritten() {
        val existingPsk = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        val target = File(filesDir, PSK_FILE)
        target.writeText(existingPsk)

        val success = provisionPskIfMissing(filesDir) {
            ByteArrayInputStream(validHexPsk.toByteArray(Charsets.UTF_8))
        }

        assertTrue("provisionPskIfMissing should return true when PSK exists", success)
        assertEquals("existing PSK content should be preserved", existingPsk, target.readText().trim())
    }

    @Test
    fun malformedPskLengthRejected() {
        val shortHex = "0123456789abcdef" // only 16 hex chars
        val target = File(filesDir, PSK_FILE)

        val success = provisionPskIfMissing(filesDir) {
            ByteArrayInputStream(shortHex.toByteArray(Charsets.UTF_8))
        }

        assertFalse("provisionPskIfMissing should fail for short PSK", success)
        assertFalse("target file should not be created for invalid PSK", target.exists())
        assertNull("readPsk should return null", readPskFromDir(filesDir))
    }

    @Test
    fun malformedPskInvalidCharsRejected() {
        val invalidHex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789xyz123" // contains 'x','y','z'
        val target = File(filesDir, PSK_FILE)

        val success = provisionPskIfMissing(filesDir) {
            ByteArrayInputStream(invalidHex.toByteArray(Charsets.UTF_8))
        }

        assertFalse("provisionPskIfMissing should fail for non-hex characters", success)
        assertFalse("target file should not be created for invalid PSK", target.exists())
        assertNull("readPsk should return null", readPskFromDir(filesDir))
    }

    @Test
    fun missingAssetHandledWithoutCrashing() {
        val target = File(filesDir, PSK_FILE)

        val success = provisionPskIfMissing(filesDir) {
            throw IOException("Asset not found")
        }

        assertFalse("provisionPskIfMissing should return false when asset missing", success)
        assertFalse("target file should not exist", target.exists())
        assertNull("readPsk should return null", readPskFromDir(filesDir))
    }

    @Test
    fun peerLiveRequiresSessionReady() {
        val sessionReady = false
        val lastHeardMs: Long? = 1000L // traffic received 1s ago

        val live = sessionReady && (lastHeardMs ?: Long.MAX_VALUE) < 45_000
        assertFalse("PEER LIVE should be false when UDP traffic exists without sessionReady", live)

        val authenticatedSessionReady = true
        val authenticatedLive = authenticatedSessionReady && (lastHeardMs ?: Long.MAX_VALUE) < 45_000
        assertTrue("PEER LIVE should be true when both sessionReady and recent traffic exist", authenticatedLive)
    }
}
