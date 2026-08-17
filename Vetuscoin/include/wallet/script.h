#ifndef SCRIPT_H
#define SCRIPT_H

#include <array>
#include <cstdint>
#include <vector>
#include "signing.h"
#include "crypto/ecdsa.h"

// A tiny real Script engine - not the full Bitcoin Script language (~100 opcodes), just the
// handful needed to express and evaluate classic Pay-to-PubKey-Hash (P2PKH), the same way real
// Bitcoin does it: scriptSig and scriptPubKey are concatenated and run as one stack-based
// program, and the spend is valid iff a single truthy value is left on the stack at the end.
namespace script {
    constexpr uint8_t OP_DUP = 0x76;
    constexpr uint8_t OP_HASH160 = 0xa9;
    constexpr uint8_t OP_EQUALVERIFY = 0x88;
    constexpr uint8_t OP_CHECKSIG = 0xac;

    /* @brief   Build a P2PKH scriptPubKey: OP_DUP OP_HASH160 <push 20-byte address> OP_EQUALVERIFY OP_CHECKSIG.
    *  @return  script bytes.
    */
    inline std::vector<uint8_t> build_p2pkh_script_pubkey(const std::array<uint8_t, wallet::ADDRESS_SIZE>& address) {
        std::vector<uint8_t> s;
        s.push_back(OP_DUP);
        s.push_back(OP_HASH160);
        s.push_back(static_cast<uint8_t>(address.size()));
        s.insert(s.end(), address.begin(), address.end());
        s.push_back(OP_EQUALVERIFY);
        s.push_back(OP_CHECKSIG);
        return s;
    }

    /* @brief   Build a P2PKH scriptSig: <push signature><push pubkey>.
    *  @return  script bytes.
    */
    inline std::vector<uint8_t> build_script_sig(const std::vector<uint8_t>& signature, const std::vector<uint8_t>& pubkey) {
        std::vector<uint8_t> s;
        s.push_back(static_cast<uint8_t>(signature.size()));
        s.insert(s.end(), signature.begin(), signature.end());
        s.push_back(static_cast<uint8_t>(pubkey.size()));
        s.insert(s.end(), pubkey.begin(), pubkey.end());
        return s;
    }

    /* @brief                   Recognize a P2PKH scriptPubKey and pull the address out of it -
    *                           needed to scan a UTXO set for "what does this wallet own", since
    *                           scriptPubKey is now a program, not a bare address.
    *  @param   script_pubkey   candidate script.
    *  @param   out_address     filled in only if this really is a P2PKH script.
    *  @return                  true if script_pubkey matches the expected P2PKH pattern exactly.
    */
    inline bool extract_p2pkh_address(const std::vector<uint8_t>& script_pubkey, std::array<uint8_t, wallet::ADDRESS_SIZE>& out_address) {
        if (script_pubkey.size() != 3 + wallet::ADDRESS_SIZE + 2) {
            return false;
        }
        if (script_pubkey[0] != OP_DUP || script_pubkey[1] != OP_HASH160 || script_pubkey[2] != wallet::ADDRESS_SIZE) {
            return false;
        }
        if (script_pubkey[3 + wallet::ADDRESS_SIZE] != OP_EQUALVERIFY || script_pubkey[4 + wallet::ADDRESS_SIZE] != OP_CHECKSIG) {
            return false;
        }
        std::copy(script_pubkey.begin() + 3, script_pubkey.begin() + 3 + wallet::ADDRESS_SIZE, out_address.begin());
        return true;
    }

    /* @brief                   Run scriptSig then scriptPubKey as one program against sighash.
    *  @param   script_sig      spender-supplied script (signature + pubkey, for P2PKH).
    *  @param   script_pubkey   the UTXO's locking script.
    *  @param   sighash         hash the signature is expected to cover.
    *  @return                  true iff the program leaves a single truthy value on the stack.
    */
    inline bool evaluate(const std::vector<uint8_t>& script_sig, const std::vector<uint8_t>& script_pubkey, const std::array<uint8_t, 32>& sighash) {
        std::vector<std::vector<uint8_t>> stack;
        std::vector<uint8_t> program = script_sig;
        program.insert(program.end(), script_pubkey.begin(), script_pubkey.end());

        size_t i = 0;
        while (i < program.size()) {
            uint8_t op = program[i++];

            if (op >= 1 && op <= 75) { // push the next op bytes
                if (i + op > program.size()) {
                    return false;
                }
                stack.emplace_back(program.begin() + i, program.begin() + i + op);
                i += op;
            }
            else if (op == OP_DUP) {
                if (stack.empty()) {
                    return false;
                }
                stack.push_back(stack.back());
            }
            else if (op == OP_HASH160) {
                if (stack.empty()) {
                    return false;
                }
                std::vector<uint8_t> top = std::move(stack.back());
                stack.pop_back();
                std::array<uint8_t, 32> sha{};
                crypto::hash_sha256(top, sha);
                std::array<uint8_t, wallet::ADDRESS_SIZE> ripemd{};
                crypto::hash_ripemd160(ripemd.data(), sha.data(), sha.size());
                stack.emplace_back(ripemd.begin(), ripemd.end());
            }
            else if (op == OP_EQUALVERIFY) {
                if (stack.size() < 2) {
                    return false;
                }
                std::vector<uint8_t> a = std::move(stack.back()); stack.pop_back();
                std::vector<uint8_t> b = std::move(stack.back()); stack.pop_back();
                if (a != b) {
                    return false;
                }
            }
            else if (op == OP_CHECKSIG) {
                if (stack.size() < 2) {
                    return false;
                }
                std::vector<uint8_t> pubkey_bytes = std::move(stack.back()); stack.pop_back();
                std::vector<uint8_t> sig_bytes = std::move(stack.back()); stack.pop_back();
                bool ok = wallet::verify_raw_signature(sig_bytes, pubkey_bytes, sighash);
                stack.push_back(ok ? std::vector<uint8_t>{ 1 } : std::vector<uint8_t>{});
            }
            else {
                return false; // unknown opcode - the full Script language has ~100 of these, we only implement P2PKH's
            }
        }

        return !stack.empty() && !stack.back().empty() && stack.back().back() != 0;
    }
}

#endif
