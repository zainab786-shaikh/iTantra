#include "itantra-native.h"

// JNI adapter — implementation plan Phase 11. It holds no logic: every decision
// is made in api/engine.h (native/src), which the host tests exercise. This file
// only converts Java arrays, strings and objects to and from the engine's structs.
//
//   Kotlin (com.itantra.app.native)            here                       engine
//   NativeBridge.nativeVersion()               version string
//   NativeBridge.nativeCreate(names, bytes)    new Engine, load()         packs, tables
//   NativeEngine.nativeBeginLoopbackSession    begin_loopback_session()   keys, contexts
//   NativeEngine.nativeSendUtterance           send_utterance()           ONE call per utterance,
//                                              ← NativeClauseEncoding[]   one result per clause
//   NativeEngine.nativeReceive                 receive()                  ONE call per payload
//                                              ← NativeReceiveResult      receiver §7
//   NativeEngine.nativeDestroy                 delete
//
// Text crosses as UTF-8 byte arrays, never as Java strings in either direction:
// JNI string functions use modified UTF-8, which re-encodes supplementary-plane
// characters, and Tier 2 must hand back the sender's bytes exactly (tier §6.1,
// contract C-05). Language codes (ASCII) and diagnostic names are the only strings.
//
// The Phase 0 prototype stubs (getNativeVersion, processText, classifyPriority,
// compressPayload, decompressPayload) are removed: none implemented specified
// behaviour, classifyPriority used the retired four-band scale, and the boundary
// note (native/KOTLIN-NATIVE-BOUNDARY.md) scheduled their replacement for Phase 11.

#include <android/log.h>

#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "api/engine.h"
#include "coder/coder.h"
#include "crypto/aead.h"
#include "crypto/kdf.h"
#include "lang/languages.h"
#include "packet/metadata.h"
#include "receiver/output.h"

#define LOG_TAG "ITantraNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace itantra;

namespace {

constexpr const char* kIllegalState    = "java/lang/IllegalStateException";
constexpr const char* kIllegalArgument = "java/lang/IllegalArgumentException";
constexpr const char* kRuntime         = "java/lang/RuntimeException";

// Constructor signatures of the Kotlin result classes (NativeEngine.kt). A
// mismatch fails JNI_OnLoad, so the library refuses to load rather than crash later.
constexpr const char* kClauseClass     = "com/itantra/app/native/NativeClauseEncoding";
constexpr const char* kClauseInit      = "(Ljava/lang/String;ILjava/lang/String;IZZ[BIIIJII)V";
constexpr const char* kReceivedClass   = "com/itantra/app/native/NativeReceiveResult";
constexpr const char* kReceivedInit    = "(Ljava/lang/String;ZI[BLjava/lang/String;II[IZZZJZ)V";

struct JavaTypes {
    jclass    string        = nullptr;
    jclass    clause        = nullptr;
    jmethodID clause_init   = nullptr;
    jclass    received      = nullptr;
    jmethodID received_init = nullptr;
};

// Resolved once in JNI_OnLoad, never written afterwards.
JavaTypes java_types;

void throw_java(JNIEnv* env, const char* type, const std::string& message) {
    if (env->ExceptionCheck()) return;
    jclass c = env->FindClass(type);
    if (c != nullptr) {
        env->ThrowNew(c, message.c_str());
        env->DeleteLocalRef(c);
    }
}

Engine* engine_of(JNIEnv* env, jlong handle) {
    if (handle == 0) {
        throw_java(env, kIllegalState, "native engine is closed");
        return nullptr;
    }
    return reinterpret_cast<Engine*>(static_cast<std::intptr_t>(handle));
}

bool read_bytes(JNIEnv* env, jbyteArray array, std::vector<u8>& out) {
    out.clear();
    if (array == nullptr) {
        throw_java(env, kIllegalArgument, "null byte array");
        return false;
    }
    const jsize n = env->GetArrayLength(array);
    out.resize(static_cast<std::size_t>(n));
    if (n > 0) env->GetByteArrayRegion(array, 0, n, reinterpret_cast<jbyte*>(out.data()));
    return !env->ExceptionCheck();
}

bool read_string(JNIEnv* env, jstring value, std::string& out) {
    if (value == nullptr) {
        throw_java(env, kIllegalArgument, "null string");
        return false;
    }
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) return false;   // OutOfMemoryError already pending
    out.assign(chars);
    env->ReleaseStringUTFChars(value, chars);
    return true;
}

jbyteArray new_bytes(JNIEnv* env, const u8* data, std::size_t length) {
    jbyteArray array = env->NewByteArray(static_cast<jsize>(length));
    if (array != nullptr && length != 0u) {
        env->SetByteArrayRegion(array, 0, static_cast<jsize>(length), reinterpret_cast<const jbyte*>(data));
    }
    return array;
}

// Ok → true. Anything else becomes a Java exception: a misuse of the engine is
// a programming error on the Kotlin side, never a message to decode.
bool engine_ok(JNIEnv* env, EngineStatus status) {
    if (status == EngineStatus::Ok) return true;
    const bool argument = status == EngineStatus::UnknownLanguage || status == EngineStatus::InvalidArgument;
    throw_java(env, argument ? kIllegalArgument : kIllegalState,
               std::string("native engine: ") + engine_status_name(status));
    return false;
}

jclass global_class(JNIEnv* env, const char* name) {
    jclass local = env->FindClass(name);
    if (local == nullptr) return nullptr;
    jclass global = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    return global;
}

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    java_types.string   = global_class(env, "java/lang/String");
    java_types.clause   = global_class(env, kClauseClass);
    java_types.received = global_class(env, kReceivedClass);
    if (java_types.string == nullptr || java_types.clause == nullptr || java_types.received == nullptr) {
        LOGE("JNI_OnLoad: result classes not found");
        return JNI_ERR;
    }
    java_types.clause_init   = env->GetMethodID(java_types.clause, "<init>", kClauseInit);
    java_types.received_init = env->GetMethodID(java_types.received, "<init>", kReceivedInit);
    if (java_types.clause_init == nullptr || java_types.received_init == nullptr) {
        LOGE("JNI_OnLoad: result class constructors do not match the native signatures");
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}

JNIEXPORT jstring JNICALL
Java_com_itantra_app_native_NativeBridge_nativeVersion(JNIEnv* env, jobject) {
    const std::string version = std::string("iTantra native core: packet format ") +
                                std::to_string(kPacketFormatVersion) + ", coder " + std::to_string(kCoderVersion) +
                                ", KDF " + std::to_string(kKdfVersion) + ", AEAD " +
                                (kAeadBypassed ? "BYPASSED (ITANTRA_DISABLE_AEAD debug diagnostic)" : "ChaCha20-Poly1305");
    return env->NewStringUTF(version.c_str());
}

JNIEXPORT jlong JNICALL
Java_com_itantra_app_native_NativeBridge_nativeCreate(JNIEnv* env, jobject, jobjectArray names,
                                                      jobjectArray contents) {
    try {
        if (names == nullptr || contents == nullptr || env->GetArrayLength(names) != env->GetArrayLength(contents)) {
            throw_java(env, kIllegalArgument, "pack names and contents must pair up");
            return 0;
        }
        PackFiles files;
        const jsize count = env->GetArrayLength(names);
        for (jsize i = 0; i < count; ++i) {
            auto name  = static_cast<jstring>(env->GetObjectArrayElement(names, i));
            auto bytes = static_cast<jbyteArray>(env->GetObjectArrayElement(contents, i));
            std::string     file_name;
            std::vector<u8> file_bytes;
            const bool ok = read_string(env, name, file_name) && read_bytes(env, bytes, file_bytes);
            env->DeleteLocalRef(name);
            env->DeleteLocalRef(bytes);
            if (!ok) return 0;
            files.emplace_back(std::move(file_name), std::move(file_bytes));
        }
        auto engine = std::make_unique<Engine>();
        std::string error;
        if (!engine->load(files, error)) {
            throw_java(env, kIllegalState, "native packs: " + error);
            return 0;
        }
        std::string languages;
        for (const std::string& code : engine->languages()) languages += (languages.empty() ? "" : ",") + code;
        LOGI("engine loaded: %zu pack files, languages %s", files.size(), languages.c_str());
        return static_cast<jlong>(reinterpret_cast<std::intptr_t>(engine.release()));
    } catch (const std::exception& e) {
        throw_java(env, kRuntime, std::string("native engine: ") + e.what());
        return 0;
    }
}

JNIEXPORT void JNICALL
Java_com_itantra_app_native_NativeEngine_nativeDestroy(JNIEnv*, jobject, jlong handle) {
    delete reinterpret_cast<Engine*>(static_cast<std::intptr_t>(handle));
}

JNIEXPORT jobjectArray JNICALL
Java_com_itantra_app_native_NativeEngine_nativeLanguages(JNIEnv* env, jobject, jlong handle) {
    Engine* engine = engine_of(env, handle);
    if (engine == nullptr) return nullptr;
    const std::vector<std::string> languages = engine->languages();
    jobjectArray out = env->NewObjectArray(static_cast<jsize>(languages.size()), java_types.string, nullptr);
    if (out == nullptr) return nullptr;
    for (std::size_t i = 0u; i < languages.size(); ++i) {
        jstring code = env->NewStringUTF(languages[i].c_str());
        env->SetObjectArrayElement(out, static_cast<jsize>(i), code);
        env->DeleteLocalRef(code);
    }
    return out;
}

JNIEXPORT void JNICALL
Java_com_itantra_app_native_NativeEngine_nativeBeginLoopbackSession(JNIEnv* env, jobject, jlong handle,
                                                                    jbyteArray psk, jbyteArray initiator_nonce,
                                                                    jbyteArray responder_nonce) {
    Engine* engine = engine_of(env, handle);
    if (engine == nullptr) return;
    std::vector<u8> k, a, b;
    if (!read_bytes(env, psk, k) || !read_bytes(env, initiator_nonce, a) || !read_bytes(env, responder_nonce, b)) return;
    if (k.size() == kPskBytes && a.size() == kHelloNonceBytes && b.size() == kHelloNonceBytes) {
        engine->begin_loopback_session(k.data(), a.data(), b.data());
        LOGI("loopback session started");
    } else {
        throw_java(env, kIllegalArgument, "PSK and HELLO nonces must be 32 bytes each (packet 6.7)");
    }
    secure_wipe(k.data(), static_cast<u32>(k.size()));
    secure_wipe(a.data(), static_cast<u32>(a.size()));
    secure_wipe(b.data(), static_cast<u32>(b.size()));
}

JNIEXPORT jobjectArray JNICALL
Java_com_itantra_app_native_NativeEngine_nativeSendUtterance(JNIEnv* env, jobject, jlong handle,
                                                             jbyteArray utf8, jstring sender_language,
                                                             jstring listener_language, jlong stt_confidence,
                                                             jboolean manual_critical) {
    try {
        Engine* engine = engine_of(env, handle);
        if (engine == nullptr) return nullptr;
        std::vector<u8> text;
        SendRequest     request;
        if (!read_bytes(env, utf8, text) || !read_string(env, sender_language, request.sender_language) ||
            !read_string(env, listener_language, request.listener_language)) {
            return nullptr;
        }
        request.text            = text.empty() ? nullptr : text.data();
        request.length          = text.size();
        request.stt_confidence  = static_cast<i64>(stt_confidence);
        request.manual_critical = manual_critical == JNI_TRUE;

        const UtteranceSend sent = engine->send_utterance(request);
        if (!engine_ok(env, sent.status)) return nullptr;

        jobjectArray out = env->NewObjectArray(static_cast<jsize>(sent.clauses.size()), java_types.clause, nullptr);
        if (out == nullptr) return nullptr;
        for (std::size_t i = 0u; i < sent.clauses.size(); ++i) {
            const ClauseSend& c = sent.clauses[i];
            const jint tier = !c.sent ? 0 : (c.outcome == SelectOutcome::Tier1 ? 1 : 2);
            jstring    outcome = env->NewStringUTF(select_outcome_name(c.outcome));
            jstring    trigger = env->NewStringUTF(safety_trigger_name(c.trigger));
            jbyteArray payload = new_bytes(env, c.sealed.data(), c.sealed.size());
            if (outcome == nullptr || trigger == nullptr || payload == nullptr) return nullptr;
            jobject clause = env->NewObject(
                java_types.clause, java_types.clause_init, outcome, tier, trigger,
                static_cast<jint>(c.priority), static_cast<jboolean>(c.sent ? JNI_TRUE : JNI_FALSE),
                static_cast<jboolean>(c.commit_refused ? JNI_TRUE : JNI_FALSE), payload,
                static_cast<jint>(c.plaintext_bytes), static_cast<jint>(c.tier1_packet_bytes),
                static_cast<jint>(c.tier2_packet_bytes), static_cast<jlong>(c.counter), static_cast<jint>(c.text.begin),
                static_cast<jint>(c.text.end));
            LOGI("send clause %zu/%zu: %s, tier %d, priority %u, trigger %s, %zu B sealed (%u B plaintext), counter %llu",
                 i + 1u, sent.clauses.size(), select_outcome_name(c.outcome), tier, static_cast<unsigned>(c.priority),
                 safety_trigger_name(c.trigger), c.sealed.size(), c.plaintext_bytes,
                 static_cast<unsigned long long>(c.counter));
            env->DeleteLocalRef(outcome);
            env->DeleteLocalRef(trigger);
            env->DeleteLocalRef(payload);
            if (clause == nullptr) return nullptr;
            env->SetObjectArrayElement(out, static_cast<jsize>(i), clause);
            env->DeleteLocalRef(clause);
        }
        return out;
    } catch (const std::exception& e) {
        throw_java(env, kRuntime, std::string("native send: ") + e.what());
        return nullptr;
    }
}

JNIEXPORT jobject JNICALL
Java_com_itantra_app_native_NativeEngine_nativeReceive(JNIEnv* env, jobject, jlong handle,
                                                       jstring receiver_language, jbyteArray payload) {
    try {
        Engine* engine = engine_of(env, handle);
        if (engine == nullptr) return nullptr;
        std::string     language;
        std::vector<u8> bytes;
        if (!read_string(env, receiver_language, language) || !read_bytes(env, payload, bytes)) return nullptr;

        ReceiveResult r;
        if (!engine_ok(env, engine->receive(language, bytes.empty() ? nullptr : bytes.data(),
                                            static_cast<u32>(bytes.size()), r))) {
            return nullptr;
        }

        const ReceiverOutput&    o    = r.output;
        const SupportedLanguage* lang = language_of_id(o.language);
        jstring    outcome = env->NewStringUTF(receive_outcome_name(r.outcome));
        jbyteArray text    = new_bytes(env, reinterpret_cast<const u8*>(o.text.data()), o.text.size());
        jstring    code    = lang != nullptr ? env->NewStringUTF(lang->code) : nullptr;
        jintArray  slots   = env->NewIntArray(static_cast<jsize>(o.unresolved.size()));
        if (outcome == nullptr || text == nullptr || slots == nullptr) return nullptr;
        for (std::size_t i = 0u; i < o.unresolved.size(); ++i) {
            const jint slot = static_cast<jint>(o.unresolved[i]);
            env->SetIntArrayRegion(slots, static_cast<jsize>(i), 1, &slot);
        }
        jobject result = env->NewObject(
            java_types.received, java_types.received_init, outcome, static_cast<jboolean>(r.emit ? JNI_TRUE : JNI_FALSE),
            static_cast<jint>(o.status), text, code, static_cast<jint>(o.mode), static_cast<jint>(o.priority), slots,
            static_cast<jboolean>(r.request_repeat ? JNI_TRUE : JNI_FALSE),
            static_cast<jboolean>(r.request_sync ? JNI_TRUE : JNI_FALSE),
            static_cast<jboolean>(r.request_unboosted_resend ? JNI_TRUE : JNI_FALSE), static_cast<jlong>(r.counter),
            static_cast<jboolean>(r.context_committed ? JNI_TRUE : JNI_FALSE));
        LOGI("receive %zu B: %s, emit %d, status %u, mode %u, priority %u, counter %llu after %u candidates, committed %d",
             bytes.size(), receive_outcome_name(r.outcome), r.emit ? 1 : 0, static_cast<unsigned>(o.status),
             static_cast<unsigned>(o.mode), static_cast<unsigned>(o.priority),
             static_cast<unsigned long long>(r.counter), r.candidates_tried, r.context_committed ? 1 : 0);
        env->DeleteLocalRef(outcome);
        env->DeleteLocalRef(text);
        if (code != nullptr) env->DeleteLocalRef(code);
        env->DeleteLocalRef(slots);
        return result;
    } catch (const std::exception& e) {
        throw_java(env, kRuntime, std::string("native receive: ") + e.what());
        return nullptr;
    }
}

}  // extern "C"
