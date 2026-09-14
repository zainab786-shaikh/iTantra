#include "crypto/kdf.h"

#include "third_party/monocypher/monocypher-ed25519.h"
#include "third_party/monocypher/monocypher.h"

namespace itantra {

void derive_session_keys(const u8* psk, const u8* initiator_nonce, const u8* responder_nonce,
                         SessionKeys& out) noexcept {
    u8 salt[2u * kHelloNonceBytes];
    for (u32 i = 0u; i < kHelloNonceBytes; ++i) {
        salt[i]                    = initiator_nonce[i];
        salt[kHelloNonceBytes + i] = responder_nonce[i];
    }

    u8 okm[kSessionKeyBytes + 4u];
    crypto_sha512_hkdf(okm, sizeof okm, psk, kPskBytes, salt, sizeof salt,
                       reinterpret_cast<const u8*>(kKdfInfo), kKdfInfoBytes);

    for (u32 i = 0u; i < kSessionKeyBytes; ++i) out.key[i] = okm[i];
    out.session_id = (u32{okm[32]} << 24) | (u32{okm[33]} << 16) | (u32{okm[34]} << 8) | u32{okm[35]};

    crypto_wipe(okm, sizeof okm);
}

void wipe_session_keys(SessionKeys& keys) noexcept {
    crypto_wipe(keys.key, sizeof keys.key);
    keys.session_id = 0u;
}

}  // namespace itantra
