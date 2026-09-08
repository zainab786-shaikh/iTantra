package com.itantra.app.transport

import com.itantra.app.packet.ITantraPacket
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.random.Random

/**
 * Direct port of src/core/transport/MockTransport.ts.
 *
 * Logging stand-in for the real transport. Models the two behaviours the UI
 * has to cope with — non-zero send latency and occasional failure — so that
 * retry and error states are exercised before the radio layer exists.
 */
class MockTransport(
    /** Fraction of sends that fail, to exercise the UI's error path. */
    private val failureRate: Double = 0.0,
) : Transport {
    override val name: String = "mock://itantra-loopback"

    private var connected = true
    private val listeners = mutableSetOf<(Boolean) -> Unit>()
    private val receiveListeners = mutableSetOf<(ITantraPacket) -> Unit>()

    /** Every packet handed to this transport, newest last. */
    val sent: MutableList<ITantraPacket> = mutableListOf()

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    override suspend fun sendPacket(packet: ITantraPacket): Boolean {
        // Stand in for radio round-trip time.
        delay((60 + Random.nextDouble() * 90).toLong())

        if (!connected || Random.nextDouble() < failureRate) {
            println("[MockTransport] send failed ${packet.id}")
            return false
        }

        sent.add(packet)
        println(
            "[MockTransport] -> ${packet.priority.value} ${packet.language} " +
                "${packet.mode.name} ${packet.originalBytes} B -> ${packet.payload.size} B (${packet.id})"
        )

        // No real P2P transport exists yet (a separate, later workstream), so
        // this loops a successfully "sent" packet back to this same device's
        // receive listeners after a short delay — standing in for a peer
        // receiving it, so the receiver pipeline (TTS, receiver UI) has
        // something real to exercise end-to-end on one device.
        scope.launch {
            delay((120 + Random.nextDouble() * 180).toLong())
            for (listener in receiveListeners.toList()) listener(packet)
        }

        return true
    }

    override fun onPacketReceived(listener: (ITantraPacket) -> Unit): () -> Unit {
        receiveListeners.add(listener)
        return { receiveListeners.remove(listener) }
    }

    /** Test/demo affordance: inject a packet as if it arrived from a peer, without a real send. */
    fun simulateReceive(packet: ITantraPacket) {
        for (listener in receiveListeners.toList()) listener(packet)
    }

    override fun isConnected(): Boolean = connected

    /** Test/demo affordance: flip the link and notify subscribers. */
    fun setConnected(connected: Boolean) {
        if (this.connected == connected) return
        this.connected = connected
        for (listener in listeners.toList()) listener(connected)
    }

    override fun onConnectionChange(listener: (Boolean) -> Unit): () -> Unit {
        listeners.add(listener)
        return { listeners.remove(listener) }
    }
}
