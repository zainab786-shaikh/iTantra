package com.itantra.app.transport

import kotlinx.coroutines.flow.StateFlow

/**
 * The operator-facing controls of a transport that has to be pointed at a
 * peer.
 *
 * Kept separate from [Transport] on purpose. [Transport] is the seam the
 * pipeline talks through, and it must stay ignorant of addressing — a
 * constrained-radio transport has no IP address to configure, and
 * [MockTransport] has no peer at all. Only the link screen consumes this,
 * and it treats a transport that does not implement it as "nothing to
 * configure".
 */
interface LinkControl {
    /** This device's identity on the link, e.g. `NODE-3F1A`. */
    val nodeLabel: String

    /** This device's address on the local network, or a short reason it is unknown. */
    val localAddress: StateFlow<String>

    /** Where packets are currently being sent, or null when no peer is known. */
    val peerAddress: StateFlow<String?>

    /** How the current peer became known — typed in, or heard from. */
    val peerSource: StateFlow<PeerSource>

    /** Milliseconds since the last frame arrived from the peer, or null if never. */
    val lastHeardMs: StateFlow<Long?>

    /**
     * Point this device at [host]. Blank clears it, returning to broadcast.
     *
     * Setting a host also discards any previously learned address, so
     * re-typing is always an override rather than a suggestion.
     */
    fun setPeerHost(host: String)

    /** The host currently typed in (not necessarily the address in use — see [peerSource]). */
    val configuredHost: StateFlow<String>
}

/** How the transport came to know where the peer is. */
enum class PeerSource {
    /** No peer yet; frames go to the broadcast address. */
    NONE,

    /** An address the operator typed in. */
    CONFIGURED,

    /**
     * The exact source address of a frame we received.
     *
     * This outranks [CONFIGURED] whenever both exist. A peer whose traffic
     * is translated on the way to us (a NAT of any kind) is reachable at the
     * address its frames *arrive from*, which is not the address it believes
     * it has and cannot be typed in ahead of time.
     */
    LEARNED,
}
