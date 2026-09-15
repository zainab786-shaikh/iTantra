#ifndef ITANTRA_NATIVE_H
#define ITANTRA_NATIVE_H

// JNI entry points of libitantra-native.so — implementation plan Phase 11.
// Coarse by design (handoff "JNI BOUNDARY"): one call per utterance on send,
// one per native payload on receive. See itantra-native.cpp.

#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved);

// com.itantra.app.native.NativeBridge
JNIEXPORT jstring JNICALL
Java_com_itantra_app_native_NativeBridge_nativeVersion(JNIEnv* env, jobject thiz);

JNIEXPORT jlong JNICALL
Java_com_itantra_app_native_NativeBridge_nativeCreate(JNIEnv* env, jobject thiz, jobjectArray names,
                                                      jobjectArray contents);

// com.itantra.app.native.NativeEngine
JNIEXPORT void JNICALL
Java_com_itantra_app_native_NativeEngine_nativeDestroy(JNIEnv* env, jobject thiz, jlong handle);

JNIEXPORT jobjectArray JNICALL
Java_com_itantra_app_native_NativeEngine_nativeLanguages(JNIEnv* env, jobject thiz, jlong handle);

JNIEXPORT void JNICALL
Java_com_itantra_app_native_NativeEngine_nativeBeginLoopbackSession(JNIEnv* env, jobject thiz, jlong handle,
                                                                    jbyteArray psk, jbyteArray initiator_nonce,
                                                                    jbyteArray responder_nonce);

JNIEXPORT jobjectArray JNICALL
Java_com_itantra_app_native_NativeEngine_nativeSendUtterance(JNIEnv* env, jobject thiz, jlong handle,
                                                             jbyteArray utf8, jstring sender_language,
                                                             jstring listener_language, jlong stt_confidence,
                                                             jboolean manual_critical);

JNIEXPORT jobject JNICALL
Java_com_itantra_app_native_NativeEngine_nativeReceive(JNIEnv* env, jobject thiz, jlong handle,
                                                       jstring receiver_language, jbyteArray payload);

#ifdef __cplusplus
}
#endif

#endif // ITANTRA_NATIVE_H
