#ifndef BLOCK_H
#define BLOCK_H

#include <array>
#include <vector>
#include <cstdint>
#include "serializer.h"
#include "transaction.h"

namespace block {

    struct CBlockHeader {
        int32_t nVersion;
        std::array<uint8_t, 32> hashPrevBlock;
        std::array<uint8_t, 32> hashMerkleRoot;
        uint32_t nTime;
        uint32_t nBits;      // compact target
        uint32_t nNonce;

        /*  @brief  Calculate double SHA-256 for block.
         *  @return double SHA-256 hash for current block.
         */
        std::array<uint8_t, 32> GetHash() const;
    };

    class CBlock : public CBlockHeader {
    public:
        std::vector<transaction::Transaction> vtx;

        /*  @brief  Calculate Merkle Root for transaction chain.
         *  @return Merkle Root hash.
         */
        std::array<uint8_t, 32> BuildMerkleRoot() const;

        /*  @brief  Check Merkle Root hash is valid.
         *  @return true or false.
         */
        bool IsMerkleRootValid() const;

        /*  @brief  Check block is valid    .
         *  @return true or false.
         */
        bool IsValid() const;
    };
}

#endif