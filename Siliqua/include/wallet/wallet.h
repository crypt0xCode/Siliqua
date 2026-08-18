#ifndef WALLET_H
#define WALLET_H

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include "signing.h"
#include "script.h"
#include "base58.h"
#include "crypto/ecdsa.h"
#include "crypto/secp256k1context.h"
#include "core/transaction.h"
#include "logger.h"
#include "utils.h"

namespace wallet {
    // Below this, change is not worth turning into its own UTXO: it would cost a weak node more
    // to store and rescan that entry forever than the amount is worth. The leftover is simply
    // not paid out (same idea as Bitcoin wallets folding dust-sized change into the fee).
    constexpr int64_t DUST_THRESHOLD = 1000;

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

        /* @brief                       Build and sign a transaction spending enough UTXOs from
        *                               available_utxos (assumed to already be filtered to ones
        *                               this wallet owns) to cover amount + fee: amount to
        *                               recipient_address, the remainder back to this wallet as
        *                               change (omitted if it would be dust-sized). Selection is
        *                               a simple greedy "keep adding UTXOs until there's enough" -
        *                               not fee-optimal, but correct.
        *  @param   available_utxos     this wallet's own spendable UTXOs.
        *  @param   recipient_address   who gets paid.
        *  @param   amount               how much they get.
        *  @param   fee                  paid to whoever mines the block (not to any specific output).
        *  @return                      signed transaction, ready to relay or include in a block.
        */
        transaction::Transaction CreateTransaction(const transaction::UtxoSet& available_utxos,
            const std::array<uint8_t, ADDRESS_SIZE>& recipient_address, int64_t amount, int64_t fee) const {
            std::vector<transaction::COutPoint> selected;
            int64_t selected_total = 0;
            for (const auto& [outpoint, txout] : available_utxos) {
                selected.push_back(outpoint);
                selected_total += txout.nValue;
                if (selected_total >= amount + fee) {
                    break;
                }
            }
            if (selected_total < amount + fee) {
                throw std::invalid_argument("Wallet::CreateTransaction: insufficient funds");
            }

            std::vector<transaction::CTxIn> inputs;
            for (const auto& outpoint : selected) {
                inputs.emplace_back(outpoint, std::vector<uint8_t>{});
            }

            std::vector<transaction::CTxOut> outputs;
            outputs.emplace_back(amount, script::build_p2pkh_script_pubkey(recipient_address));
            int64_t change = selected_total - amount - fee;
            if (change >= DUST_THRESHOLD) {
                outputs.emplace_back(change, script::build_p2pkh_script_pubkey(address_));
            }

            transaction::Transaction tx(1, inputs, outputs, 0);

            // All inputs are spent with the same key here (a wallet only ever selects its own
            // UTXOs), so one signature over the whole transaction covers every input.
            std::vector<uint8_t> script_sig = Sign(tx);
            for (auto& in : tx.vin) {
                in.scriptSig = script_sig;
            }
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

        // Signs the tx hash computed with every scriptSig empty - the signature must not depend
        // on its own bytes (same idea as Bitcoin's SIGHASH_ALL pre-image) - then wraps it into a
        // P2PKH scriptSig (signature + our compressed pubkey).
        std::vector<uint8_t> Sign(const transaction::Transaction& tx) const {
            std::array<uint8_t, 32> sighash = tx.GetHash();
            std::vector<uint8_t> signature = sign_raw(seckey_, sighash);
            std::vector<uint8_t> pubkey(compressed_pubkey_, compressed_pubkey_ + PUBKEY_SIZE);
            return script::build_script_sig(signature, pubkey);
        }

        unsigned char seckey_[32];
        secp256k1_pubkey pubkey_;
        unsigned char compressed_pubkey_[PUBKEY_SIZE];
        std::array<uint8_t, ADDRESS_SIZE> address_{};
    };

    /* @brief                       Check that tx.vin[input_index]'s scriptSig is a valid spend
    *                               of expected_script_pub_key, by running the actual script
    *                               (script::evaluate) against tx's hash-with-every-scriptSig-empty.
    *  @param   tx                  transaction to check.
    *  @param   input_index         which input to check.
    *  @param   expected_script_pub_key  scriptPubKey of the UTXO that input claims to spend.
    *  @return                      true if the input is a valid spend of that UTXO.
    */
    inline bool verify_transaction_signature(const transaction::Transaction& tx, size_t input_index, const std::vector<uint8_t>& expected_script_pub_key) {
        if (input_index >= tx.vin.size()) {
            return false;
        }

        transaction::Transaction unsigned_tx = tx;
        for (auto& in : unsigned_tx.vin) {
            in.scriptSig.clear();
        }
        std::array<uint8_t, 32> sighash = unsigned_tx.GetHash();

        return script::evaluate(tx.vin.at(input_index).scriptSig, expected_script_pub_key, sighash);
    }
}

#endif
