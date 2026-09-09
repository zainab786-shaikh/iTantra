#include "itantra-native.h"
#include <android/log.h>
#include <string>
#include <vector>
#include <algorithm>

#define LOG_TAG "ITantraNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern "C" {

JNIEXPORT jstring JNICALL
Java_com_itantra_app_native_NativeBridge_getNativeVersion(JNIEnv *env, jobject thiz) {
    std::string version = "iTantra Native C++ Core v1.0.0 (C++17 / JNI)";
    return env->NewStringUTF(version.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_itantra_app_native_NativeBridge_processText(JNIEnv *env, jobject thiz, jstring text, jstring language) {
    if (!text || !language) return text;

    const char *textCStr = env->GetStringUTFChars(text, nullptr);
    const char *langCStr = env->GetStringUTFChars(language, nullptr);

    std::string processed = std::string(textCStr);
    LOGI("Processing text in C++ native engine [%s]: %s", langCStr, processed.c_str());

    env->ReleaseStringUTFChars(text, textCStr);
    env->ReleaseStringUTFChars(language, langCStr);

    return env->NewStringUTF(processed.c_str());
}

JNIEXPORT jint JNICALL
Java_com_itantra_app_native_NativeBridge_classifyPriority(JNIEnv *env, jobject thiz, jstring text, jstring language) {
    if (!text) return 0; // NORMAL

    const char *textCStr = env->GetStringUTFChars(text, nullptr);
    std::string str(textCStr);

    // Convert to lowercase for matching
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);

    int priority = 0; // 0: NORMAL, 1: MEDIUM, 2: HIGH, 3: CRITICAL

    if (str.find("mayday") != std::string::npos ||
        str.find("sos") != std::string::npos ||
        str.find("emergency") != std::string::npos ||
        str.find("attack") != std::string::npos ||
        str.find("casualty") != std::string::npos ||
        str.find("fire") != std::string::npos) {
        priority = 3; // CRITICAL
    } else if (str.find("help") != std::string::npos ||
               str.find("urgent") != std::string::npos ||
               str.find("doctor") != std::string::npos ||
               str.find("rescue") != std::string::npos) {
        priority = 2; // HIGH
    } else if (str.find("report") != std::string::npos ||
               str.find("status") != std::string::npos ||
               str.find("supplies") != std::string::npos) {
        priority = 1; // MEDIUM
    }

    env->ReleaseStringUTFChars(text, textCStr);
    return priority;
}

JNIEXPORT jbyteArray JNICALL
Java_com_itantra_app_native_NativeBridge_compressPayload(JNIEnv *env, jobject thiz, jstring text) {
    if (!text) return nullptr;

    const char *textCStr = env->GetStringUTFChars(text, nullptr);
    std::string str(textCStr);
    env->ReleaseStringUTFChars(text, textCStr);

    jbyteArray result = env->NewByteArray(static_cast<jsize>(str.size()));
    if (result) {
        env->SetByteArrayRegion(result, 0, static_cast<jsize>(str.size()),
                                reinterpret_cast<const jbyte*>(str.data()));
    }
    return result;
}

JNIEXPORT jstring JNICALL
Java_com_itantra_app_native_NativeBridge_decompressPayload(JNIEnv *env, jobject thiz, jbyteArray data) {
    if (!data) return nullptr;

    jsize len = env->GetArrayLength(data);
    std::vector<char> buffer(len);
    env->GetByteArrayRegion(data, 0, len, reinterpret_cast<jbyte*>(buffer.data()));

    std::string decompressed(buffer.begin(), buffer.end());
    return env->NewStringUTF(decompressed.c_str());
}

} // extern "C"
