#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <array>
#include <vector>
#include "serializer.h"

namespace transaction {
    // Previous tx out pointer.
	struct COutPoint {
        std::array<uint8_t, 32> txid;  // tx hash (32 bytes)
        uint32_t n;                    // out index

        COutPoint() : n(0xFFFFFFFF) {
            txid.fill(0);
        }
        COutPoint(const std::array<uint8_t, 32>& hash, uint32_t index) : txid(hash), n(index) {}

        /*  @brief  True if all txid's elements equals zero and n == 0xFFFFFFFF.
         *  @return true or false.
         */
        bool IsNull() const;
	};

    // Transaction enter.
    struct CTxIn {
        COutPoint prevout;              // UTXO pointer
        std::vector<uint8_t> scriptSig; // sign (bytes for simplify)
        uint32_t nSequence;             // 0xFFFFFFFF for simplify

        CTxIn() : scriptSig{}, nSequence(0xFFFFFFFF) { prevout = COutPoint(); }
        CTxIn(const COutPoint& out, const std::vector<uint8_t>& sig, uint32_t seq = 0xFFFFFFFF) : prevout(out), scriptSig(sig), nSequence(seq) {}
    };

    // Transaction out.
    struct CTxOut {
        int64_t nValue;                     // sum in satoshi
        std::vector<uint8_t> scriptPubKey;  // receiver (address/key)

        CTxOut() : nValue((int64_t)0), scriptPubKey{} {}
        CTxOut(int64_t value, const std::vector<uint8_t>& script) : nValue(value), scriptPubKey(script) {}
    };

    // Transaction class.
    class Transaction {
    public:
        int32_t nVersion;
        std::vector<CTxIn> vin;
        std::vector<CTxOut> vout;
        uint32_t nLockTime;

        Transaction() : nVersion((int32_t) 0), nLockTime((uint32_t) 0) { vin = {}; vout = {}; }
        Transaction(int32_t version, const std::vector<CTxIn>& inputs,
            const std::vector<CTxOut>& outputs, uint32_t lockTime = 0) : nVersion(version), vin(inputs), vout(outputs), nLockTime(lockTime) {}

        /*  @brief  Get transaction hash (all params writes in the main buffer as little-endians).
         *  @return tx hash (double SHA-256).
         */
        std::array<uint8_t, 32> GetHash() const;

        // Check tx on coinbased (first in block).
        bool IsCoinbase() const;
    };
}

#endif