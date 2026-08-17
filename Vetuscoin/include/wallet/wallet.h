#ifndef WALLET_H
#define WALLET_H

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include "crypto/ecdsa.h"
#include "crypto/secp256k1context.h"
#include "core/transaction.h"
#include "logger.h"
#include "utils.h"

namespace wallet {
    constexpr size_t ADDRESS_SIZE = 20;   // RIPEMD160(SHA256(compressed pubkey))
    constexpr size_t SIGNATURE_SIZE = 64; // secp256k1 compact signature
    constexpr size_t PUBKEY_SIZE = 33;    // compressed pubkey

    /* @brief   Parse a 40-char hex string (as printed by --address) back into a raw address.
    *  @param   hex     hex string, exactly ADDRESS_SIZE * 2 characters.
    *  @return          decoded address.
    */
    inline std::array<uint8_t, ADDRESS_SIZE> address_from_hex(const std::string& hex) {
        if (hex.length() != ADDRESS_SIZE * 2) {
            throw std::length_error("address_from_hex: hex length must be " + std::to_string(ADDRESS_SIZE * 2) + " symbols");
        }
        std::array<uint8_t, ADDRESS_SIZE> bytes{};
        for (size_t i = 0; i < ADDRESS_SIZE; ++i) {
            bytes[i] = static_cast<uint8_t>((hexCharToByte(hex[i * 2]) << 4) | hexCharToByte(hex[i * 2 + 1]));
        }
        return bytes;
    }

    // Holds a keypair and address, and can spend UTXOs paid to that address.
    class Wallet {
    public:
        Wallet() {
            std::vector<std::string> keypair_strings;
            if (crypto::generate_keypair("", keypair_strings, seckey_, pubkey_, compressed_pubkey_) != 0) {
                throw std::runtime_error("Wallet: failed to generate keypair");
            }
            std::string address_str;
            if (crypto::create_address_from_pubkey(compressed_pubkey_, address_.data(), address_str) != 0) {
                throw std::runtime_error("Wallet: failed to derive address");
            }
        }

        // Restore a wallet from a previously saved 32-byte secret key.
        static Wallet FromSeckey(const unsigned char seckey[32]) {
            return Wallet(seckey);
        }

        const std::array<uint8_t, ADDRESS_SIZE>& Address() const { return address_; }
        const unsigned char* Seckey() const { return seckey_; }

        /* @brief                       Build and sign a transaction spending utxo_value from
        *                               utxo_outpoint: amount to recipient_address, the remainder
        *                               back to this wallet as change.
        *  @param   utxo_outpoint       outpoint of the UTXO to spend (must belong to this wallet).
        *  @param   utxo_value          value of that UTXO.
        *  @param   recipient_address   who gets paid.
        *  @param   amount              how much they get (must be <= utxo_value).
        *  @return                      signed transaction, ready to include in a block.
        */
        transaction::Transaction CreateTransaction(const transaction::COutPoint& utxo_outpoint, int64_t utxo_value,
            const std::array<uint8_t, ADDRESS_SIZE>& recipient_address, int64_t amount) const {
            if (amount > utxo_value) {
                throw std::invalid_argument("Wallet::CreateTransaction: amount exceeds UTXO value");
            }

            transaction::CTxIn input(utxo_outpoint, {});
            std::vector<transaction::CTxOut> outputs;
            outputs.emplace_back(amount, std::vector<uint8_t>(recipient_address.begin(), recipient_address.end()));
            int64_t change = utxo_value - amount;
            if (change > 0) {
                outputs.emplace_back(change, std::vector<uint8_t>(address_.begin(), address_.end()));
            }

            transaction::Transaction tx(1, { input }, outputs, 0);
            tx.vin.at(0).scriptSig = Sign(tx);
            tx.tx_hash = tx.GetHash();
            return tx;
        }

    private:
        explicit Wallet(const unsigned char seckey[32]) {
            secp256k1_context* ctx = Secp256k1Context::instance().get();
            std::memcpy(seckey_, seckey, 32);
            if (!secp256k1_ec_seckey_verify(ctx, seckey_)) {
                throw std::runtime_error("Wallet: invalid seckey");
            }
            if (!secp256k1_ec_pubkey_create(ctx, &pubkey_, seckey_)) {
                throw std::runtime_error("Wallet: failed to derive pubkey");
            }
            size_t len = PUBKEY_SIZE;
            secp256k1_ec_pubkey_serialize(ctx, compressed_pubkey_, &len, &pubkey_, SECP256K1_EC_COMPRESSED);

            std::string address_str;
            if (crypto::create_address_from_pubkey(compressed_pubkey_, address_.data(), address_str) != 0) {
                throw std::runtime_error("Wallet: failed to derive address");
            }
        }

        // scriptSig = signature (64 bytes) + our compressed pubkey (33 bytes), P2PKH-style: lets a
        // verifier recompute our address from the embedded pubkey, then check the signature.
        std::vector<uint8_t> Sign(const transaction::Transaction& tx) const {
            // Sign the tx hash computed with an empty scriptSig - the signature must not depend
            // on its own bytes (same idea as Bitcoin's SIGHASH_ALL pre-image).
            std::array<uint8_t, 32> sighash = tx.GetHash();

            // generate_sign() hashes a std::string internally rather than taking a raw 32-byte
            // hash directly, so the hash is hex-encoded first - an extra layer, but still a
            // deterministic 1:1 encoding of the sighash, so it does not weaken the signature.
            std::string sighash_hex = bytes_to_hex(sighash.data(), sighash.size());

            unsigned char serialized_sig[SIGNATURE_SIZE];
            std::string sig_str;
            secp256k1_ecdsa_signature sig;
            unsigned char signed_hash[32];
            if (crypto::generate_sign(sighash_hex, const_cast<unsigned char*>(seckey_), serialized_sig, sig_str, sig, signed_hash) != 0) {
                throw std::runtime_error("Wallet::Sign: signing failed");
            }

            std::vector<uint8_t> script_sig(serialized_sig, serialized_sig + SIGNATURE_SIZE);
            script_sig.insert(script_sig.end(), compressed_pubkey_, compressed_pubkey_ + PUBKEY_SIZE);
            return script_sig;
        }

        unsigned char seckey_[32];
        secp256k1_pubkey pubkey_;
        unsigned char compressed_pubkey_[PUBKEY_SIZE];
        std::array<uint8_t, ADDRESS_SIZE> address_{};
    };

    /* @brief                       Verify tx.vin[0]'s scriptSig: the embedded pubkey must hash to
    *                               expected_script_pub_key (the UTXO's claimed owner), and the
    *                               signature must be valid over tx's own hash-with-empty-scriptSig.
    *  @param   tx                  transaction to check (must have exactly the scriptSig layout
    *                               Wallet::Sign() produces: 64-byte signature + 33-byte pubkey).
    *  @param   expected_script_pub_key  scriptPubKey of the UTXO tx.vin[0] claims to spend.
    *  @return                      true if the input is a valid spend of that UTXO.
    */
    inline bool verify_transaction_signature(const transaction::Transaction& tx, const std::vector<uint8_t>& expected_script_pub_key) {
        if (tx.vin.empty() || tx.vin.at(0).scriptSig.size() != SIGNATURE_SIZE + PUBKEY_SIZE) {
            return false;
        }
        const std::vector<uint8_t>& script_sig = tx.vin.at(0).scriptSig;

        unsigned char serialized_sig[SIGNATURE_SIZE];
        std::memcpy(serialized_sig, script_sig.data(), SIGNATURE_SIZE);
        unsigned char compressed_pubkey[PUBKEY_SIZE];
        std::memcpy(compressed_pubkey, script_sig.data() + SIGNATURE_SIZE, PUBKEY_SIZE);

        std::array<uint8_t, ADDRESS_SIZE> derived_address{};
        std::string address_str;
        crypto::create_address_from_pubkey(compressed_pubkey, derived_address.data(), address_str);
        if (expected_script_pub_key.size() != ADDRESS_SIZE ||
            !std::equal(derived_address.begin(), derived_address.end(), expected_script_pub_key.begin())) {
            return false;
        }

        // Recompute the same sighash Wallet::Sign() signed: this tx's hash with an empty scriptSig.
        transaction::Transaction unsigned_tx = tx;
        unsigned_tx.vin.at(0).scriptSig.clear();
        std::array<uint8_t, 32> sighash = unsigned_tx.GetHash();
        std::string sighash_hex = bytes_to_hex(sighash.data(), sighash.size());

        std::vector<unsigned char> message_hash(32);
        crypto::hash_sha256(sighash_hex.begin(), sighash_hex.end(), message_hash);

        secp256k1_ecdsa_signature sig;
        secp256k1_pubkey pubkey;
        return crypto::verify_sign(sig, serialized_sig, pubkey, compressed_pubkey, message_hash.data()) == 0;
    }
}

#endif
