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

        /*  @brief  Serialize the 80-byte header (same layout GetHash() hashes, reused for storage).
         *  @return serialized bytes.
         */
        std::vector<uint8_t> Serialize() const;

        /*  @brief          Deserialize a header from bytes written by Serialize().
         *  @param  data    buffer to read from.
         *  @param  offset  in-out cursor position in the buffer.
         *  @return         decoded CBlockHeader.
         */
        static CBlockHeader Deserialize(const std::vector<uint8_t>& data, size_t& offset);
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

        /*  @brief  Check block is valid.
         *  @return true or false.
         */
        bool IsValid() const;

        /*  @brief  Serialize the full block (header + vtx count (varint) + each tx's Serialize()).
         *  @return serialized bytes.
         */
        std::vector<uint8_t> Serialize() const;

        /*  @brief          Deserialize a full block from bytes written by Serialize().
         *  @param  data    buffer to read from.
         *  @param  offset  in-out cursor position in the buffer.
         *  @return         decoded CBlock.
         */
        static CBlock Deserialize(const std::vector<uint8_t>& data, size_t& offset);
    };
}

#endif