#ifndef ITANTRA_NATIVE_H
#define ITANTRA_NATIVE_H

#include <jni.h>
#include <string>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT jstring JNICALL
Java_com_itantra_app_native_NativeBridge_getNativeVersion(JNIEnv *env, jobject thiz);

JNIEXPORT jstring JNICALL
Java_com_itantra_app_native_NativeBridge_processText(JNIEnv *env, jobject thiz, jstring text, jstring language);

JNIEXPORT jint JNICALL
Java_com_itantra_app_native_NativeBridge_classifyPriority(JNIEnv *env, jobject thiz, jstring text, jstring language);

JNIEXPORT jbyteArray JNICALL
Java_com_itantra_app_native_NativeBridge_compressPayload(JNIEnv *env, jobject thiz, jstring text);

JNIEXPORT jstring JNICALL
Java_com_itantra_app_native_NativeBridge_decompressPayload(JNIEnv *env, jobject thiz, jbyteArray data);

#ifdef __cplusplus
}
#endif

#endif // ITANTRA_NATIVE_H
