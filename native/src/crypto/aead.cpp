#include "crypto/aead.h"

#include "third_party/monocypher/monocypher.h"

namespace itantra {

namespace {

constexpr u32 kFullTagBytes = 16u;

bool pointers_ok(const u8* key, const u8* nonce, const u8* ad, u32 ad_length, const u8* in,
                 u32 in_length, const u8* out, u32 capacity) noexcept {
    return key != nullptr && nonce != nullptr && (ad != nullptr || ad_length == 0u) &&
           (in != nullptr || in_length == 0u) && (out != nullptr || capacity == 0u);
}

}  // namespace

AeadStatus aead_seal(const u8* key, const u8* nonce, const u8* ad, u32 ad_length,
                     const u8* plaintext, u32 length, u8* out, u32 capacity,
                     u32& out_length) noexcept {
    out_length = 0u;
    if (!pointers_ok(key, nonce, ad, ad_length, plaintext, length, out, capacity)) {
        return AeadStatus::InvalidArgument;
    }
    if (length > kAeadMaxMessageBytes || capacity < length + kAeadOverheadBytes) {
        return AeadStatus::InvalidArgument;
    }

#ifdef ITANTRA_DISABLE_AEAD
    if (out != plaintext) {
        for (u32 i = 0u; i < length; ++i) out[i] = plaintext[i];
    }
    out_length = length;
    return AeadStatus::Ok;
#else
    crypto_aead_ctx ctx;
    u8 tag[kFullTagBytes];
    crypto_aead_init_ietf(&ctx, key, nonce);
    crypto_aead_write(&ctx, out, tag, ad, ad_length, plaintext, length);
    for (u32 i = 0u; i < kAeadTagBytes; ++i) out[length + i] = tag[i];
    crypto_wipe(&ctx, sizeof ctx);
    crypto_wipe(tag, sizeof tag);
    out_length = length + kAeadTagBytes;
    return AeadStatus::Ok;
#endif
}

AeadStatus aead_open(const u8* key, const u8* nonce, const u8* ad, u32 ad_length,
                     const u8* sealed, u32 sealed_length, u8* plaintext, u32 capacity,
                     u32& plaintext_length) noexcept {
    plaintext_length = 0u;
    if (!pointers_ok(key, nonce, ad, ad_length, sealed, sealed_length, plaintext, capacity)) {
        return AeadStatus::InvalidArgument;
    }

#ifdef ITANTRA_DISABLE_AEAD
    if (sealed_length > kAeadMaxMessageBytes || capacity < sealed_length) {
        return AeadStatus::InvalidArgument;
    }
    if (plaintext != sealed) {
        for (u32 i = 0u; i < sealed_length; ++i) plaintext[i] = sealed[i];
    }
    plaintext_length = sealed_length;
    return AeadStatus::Ok;
#else
    if (sealed_length < kAeadTagBytes) return AeadStatus::AuthenticationFailed;
    const u32 n = sealed_length - kAeadTagBytes;
    if (n > kAeadMaxMessageBytes || capacity < n) return AeadStatus::InvalidArgument;

    // The received tag is read before step 1 in case plaintext aliases sealed.
    u8 received[kAeadTagBytes];
    for (u32 i = 0u; i < kAeadTagBytes; ++i) received[i] = sealed[n + i];

    crypto_aead_ctx ctx;
    u8 tag[kFullTagBytes];
    u8 recomputed[kAeadMaxMessageBytes];

    // 1  ciphertext XOR keystream → candidate plaintext
    crypto_aead_init_ietf(&ctx, key, nonce);
    crypto_aead_write(&ctx, plaintext, tag, ad, ad_length, sealed, n);

    // 2  re-seal the candidate: reproduces the ciphertext and its tag
    crypto_aead_init_ietf(&ctx, key, nonce);
    crypto_aead_write(&ctx, recomputed, tag, ad, ad_length, plaintext, n);

    // 3  constant time over the 4 transmitted bytes
    u32 difference = 0u;
    for (u32 i = 0u; i < kAeadTagBytes; ++i) difference |= u32{tag[i]} ^ u32{received[i]};

    crypto_wipe(&ctx, sizeof ctx);
    crypto_wipe(tag, sizeof tag);
    crypto_wipe(recomputed, n);

    if (difference != 0u) {
        crypto_wipe(plaintext, n);
        return AeadStatus::AuthenticationFailed;
    }
    plaintext_length = n;
    return AeadStatus::Ok;
#endif
}

void secure_wipe(u8* data, u32 length) noexcept {
    if (data != nullptr && length != 0u) crypto_wipe(data, length);
}

}  // namespace itantra
