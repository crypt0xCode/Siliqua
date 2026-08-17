#ifndef SIGNING_H
#define SIGNING_H

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "crypto/ecdsa.h"
#include "utils.h"

// Raw signing/verification primitives shared by wallet.h (which knows about Wallet identities
// and transactions) and script.h (which knows about opcodes, not wallets) - kept in their own
// file specifically so those two can depend on this without depending on each other.
namespace wallet {
    constexpr size_t ADDRESS_SIZE = 20;   // RIPEMD160(SHA256(compressed pubkey))
    constexpr size_t SIGNATURE_SIZE = 64; // secp256k1 compact signature
    constexpr size_t PUBKEY_SIZE = 33;    // compressed pubkey

    // generate_sign()/verify_sign() hash a std::string internally rather than taking a raw
    // 32-byte hash directly, so a sighash is hex-encoded first before going through them - an
    // extra layer, but a deterministic 1:1 encoding, so it does not weaken anything.
    inline std::array<uint8_t, 32> sighash_to_message_hash(const std::array<uint8_t, 32>& sighash) {
        std::string sighash_hex = bytes_to_hex(sighash.data(), sighash.size());
        std::array<uint8_t, 32> message_hash{};
        crypto::hash_sha256(sighash_hex.begin(), sighash_hex.end(), message_hash);
        return message_hash;
    }

    /* @brief   Sign a sighash with a raw 32-byte secret key.
    *  @return  64-byte compact signature.
    */
    inline std::vector<uint8_t> sign_raw(const unsigned char seckey[32], const std::array<uint8_t, 32>& sighash) {
        std::string message_hex = bytes_to_hex(sighash.data(), sighash.size());

        unsigned char serialized_sig[SIGNATURE_SIZE];
        std::string sig_str;
        secp256k1_ecdsa_signature sig;
        unsigned char signed_hash[32];
        if (crypto::generate_sign(message_hex, const_cast<unsigned char*>(seckey), serialized_sig, sig_str, sig, signed_hash) != 0) {
            throw std::runtime_error("sign_raw: signing failed");
        }
        return std::vector<uint8_t>(serialized_sig, serialized_sig + SIGNATURE_SIZE);
    }

    /* @brief   Verify a raw (signature, pubkey) pair over a sighash.
    *  @return  true if the signature is valid for that pubkey and sighash.
    */
    inline bool verify_raw_signature(const std::vector<uint8_t>& sig_bytes, const std::vector<uint8_t>& pubkey_bytes, const std::array<uint8_t, 32>& sighash) {
        if (sig_bytes.size() != SIGNATURE_SIZE || pubkey_bytes.size() != PUBKEY_SIZE) {
            return false;
        }
        unsigned char serialized_sig[SIGNATURE_SIZE];
        std::memcpy(serialized_sig, sig_bytes.data(), SIGNATURE_SIZE);
        unsigned char compressed_pubkey[PUBKEY_SIZE];
        std::memcpy(compressed_pubkey, pubkey_bytes.data(), PUBKEY_SIZE);

        std::array<uint8_t, 32> message_hash = sighash_to_message_hash(sighash);

        secp256k1_ecdsa_signature sig;
        secp256k1_pubkey pubkey;
        return crypto::verify_sign(sig, serialized_sig, pubkey, compressed_pubkey, message_hash.data()) == 0;
    }
}

#endif
