package com.itantra.app.native

import android.util.Log

private const val TAG = "NativeBridge"

/**
 * JNI Bridge connecting Kotlin app code to the native C++ iTantra engine
 * (`libitantra-native.so`).
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

    external fun getNativeVersion(): String
    external fun processText(text: String, language: String): String
    external fun classifyPriority(text: String, language: String): Int
    external fun compressPayload(text: String): ByteArray
    external fun decompressPayload(data: ByteArray): String
}
