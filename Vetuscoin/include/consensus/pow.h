#ifndef POW_H
#define POW_H

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include "core/block.h"
#include "logger.h"
#include "utils.h"

namespace proof_of_work {
    /* @brief   Convert compact difficulty (nBits) to a full 256-bit target.
    *  @param   bits    compact target: top byte is the exponent (byte length), low 3 bytes are the mantissa.
    *  @return  256-bit target as a big-endian byte array (index 0 is the most significant byte).
    */
    inline std::array<uint8_t, 32> bits_to_target(uint32_t bits) {
        std::string prefix = "bits_to_target: ";

        std::array<uint8_t, 32> target{};

        uint32_t exponent = bits >> 24;
        uint32_t mantissa = bits & 0x007FFFFF; // clear sign bit (bit 23), keep 23-bit magnitude

        if (bits & 0x00800000) {
            // Negative target is invalid, keep target zeroed (impossible to satisfy).
            Logger::instance().error("{}nBits {:#010x} has the sign bit set, returning zero target.", prefix, bits);
            return target;
        }

        if (exponent > 32) {
            // Target does not fit into 256 bits, keep target zeroed (impossible to satisfy).
            Logger::instance().error("{}nBits {:#010x} exponent {} overflows a 256-bit target, returning zero target.", prefix, bits, exponent);
            return target;
        }

        if (exponent <= 3) {
            // Mantissa itself is shifted right, no zero padding after it.
            mantissa >>= 8 * (3 - exponent);
            target[29] = static_cast<uint8_t>((mantissa >> 16) & 0xFF);
            target[30] = static_cast<uint8_t>((mantissa >> 8) & 0xFF);
            target[31] = static_cast<uint8_t>(mantissa & 0xFF);
        }
        else {
            // Mantissa occupies the 3 most significant bytes of the value,
            // the remaining (exponent - 3) low bytes are zero padding.
            size_t offset = 32 - exponent;
            target[offset] = static_cast<uint8_t>((mantissa >> 16) & 0xFF);
            target[offset + 1] = static_cast<uint8_t>((mantissa >> 8) & 0xFF);
            target[offset + 2] = static_cast<uint8_t>(mantissa & 0xFF);
        }

        Logger::instance().debug("{}nBits {:#010x} -> target {}.", prefix, bits, bytes_to_hex(target.data(), target.size()));
        return target;
    }

    /* @brief   Check that a block header's hash satisfies its own difficulty target.
    *  @param   header  block header providing nBits and GetHash().
    *  @return  true if the hash is less than or equal to the target, false otherwise.
    */
    inline bool check_proof_of_work(const block::CBlockHeader& header) {
        std::string prefix = "check_proof_of_work: ";

        // Both the hash and the target are compared as big-endian numbers (index 0 = most significant byte).
        std::array<uint8_t, 32> target = bits_to_target(header.nBits);
        std::array<uint8_t, 32> hash = header.GetHash();
        bool result = hash <= target;

        Logger::instance().debug("{}hash {} {} target {}.", prefix,
            bytes_to_hex(hash.data(), hash.size()), result ? "<=" : ">", bytes_to_hex(target.data(), target.size()));
        return result;
    }

    /* @brief               Search for nNonce that makes the header's hash satisfy nBitsTarget.
    *  @param   header      block header to mine (nBits, nNonce and nTime are mutated in place).
    *  @param   nBitsTarget compact difficulty to mine for.
    */
    inline void mine_block(block::CBlockHeader& header, uint32_t nBitsTarget) {
        std::string prefix = "mine_block: ";

        header.nBits = nBitsTarget;
        header.nNonce = 0;

        // Computed once: recomputing the target on every nNonce attempt would be wasteful.
        std::array<uint8_t, 32> target = bits_to_target(header.nBits);
        auto start = std::chrono::steady_clock::now();

        while (header.GetHash() > target) {
            if (header.nNonce == UINT32_MAX) {
                // nNonce space exhausted: roll nTime forward and start over
                // (for simplify, no extranonce/coinbase change like real Bitcoin miners use).
                header.nNonce = 0;
                header.nTime += 1;
                Logger::instance().debug("{}nNonce overflow, bumping nTime to {}.", prefix, header.nTime);
            }
            else {
                header.nNonce += 1;
            }
        }

        auto end = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        Logger::instance().info("{}Block mined. nNonce: {}, nTime: {}, elapsed: {} ms.", prefix, header.nNonce, header.nTime, elapsed_ms);
    }
}

#endif
