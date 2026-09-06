package com.itantra.app.transport

import com.itantra.app.packet.ITantraPacket

/**
 * Direct port of src/core/transport/Transport.ts.
 *
 * The seam between the transmitter and whatever carries its packets. The
 * transmitter knows nothing beyond this interface, so a real transport can
 * be dropped in later by swapping the instance handed to the controller.
 */
interface Transport {
    val name: String

    /** @return true when the packet was handed off successfully. */
    suspend fun sendPacket(packet: ITantraPacket): Boolean

    /** Whether the link is currently usable. Drives the connection badge. */
    fun isConnected(): Boolean

    /** Subscribe to link state changes. @return an unsubscribe function. */
    fun onConnectionChange(listener: (connected: Boolean) -> Unit): () -> Unit

    /**
     * Subscribe to packets arriving from the far end. @return an unsubscribe
     * function. The real P2P transport (BLE/Nearby Connections) is a
     * separate, later workstream — this seam exists now so the receiver
     * pipeline has something concrete to consume in the meantime.
     */
    fun onPacketReceived(listener: (packet: ITantraPacket) -> Unit): () -> Unit
}
