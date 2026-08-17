#ifndef POW_H
#define POW_H

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
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

    /* @brief                           Compute the next block's difficulty from a header history,
    *                                   matching Bitcoin's retarget rule: every retarget_interval
    *                                   blocks, scale the target by actual_timespan / target_timespan,
    *                                   clamped to [0.25x, 4x] so a burst of fast/slow blocks can't
    *                                   swing the difficulty by more than 4x in one retarget.
    *  @param   headers                 chain headers so far, oldest first (headers.back() is tip).
    *  @param   retarget_interval       how many blocks between difficulty adjustments.
    *  @param   target_timespan_seconds expected total time for retarget_interval blocks.
    *  @return                          nBits to use for the next block.
    */
    inline uint32_t get_next_work_required(const std::vector<block::CBlockHeader>& headers,
        uint32_t retarget_interval, uint32_t target_timespan_seconds) {
        std::string prefix = "get_next_work_required: ";

        if (headers.empty()) {
            Logger::instance().error("{}Empty header history, cannot compute next difficulty.", prefix);
            throw std::invalid_argument("get_next_work_required: empty header history");
        }

        uint32_t current_bits = headers.back().nBits;

        // Not a retarget boundary yet - keep mining at the same difficulty as the last block.
        if (headers.size() % retarget_interval != 0) {
            return current_bits;
        }

        const block::CBlockHeader& period_start = headers.at(headers.size() - retarget_interval);
        const block::CBlockHeader& period_end = headers.back();
        int64_t actual_timespan = static_cast<int64_t>(period_end.nTime) - static_cast<int64_t>(period_start.nTime);

        int64_t min_timespan = static_cast<int64_t>(target_timespan_seconds) / 4;
        int64_t max_timespan = static_cast<int64_t>(target_timespan_seconds) * 4;
        if (actual_timespan < min_timespan) actual_timespan = min_timespan;
        if (actual_timespan > max_timespan) actual_timespan = max_timespan;

        // NOTE: scales the (exponent, mantissa) pair directly using double, instead of exact
        // 256-bit integer arithmetic (there is no big-integer type in this codebase, and
        // arith_uint256-style exactness is not needed for local same-machine testing).
        uint32_t exponent = current_bits >> 24;
        uint32_t mantissa = current_bits & 0x007FFFFF;
        double value = static_cast<double>(mantissa) * std::pow(2.0, 8.0 * (static_cast<double>(exponent) - 3.0));
        value *= static_cast<double>(actual_timespan) / static_cast<double>(target_timespan_seconds);

        uint32_t new_exponent = 3;
        double m = value;
        while (m >= 16777216.0) { // 2^24: mantissa must fit in 23 bits after the sign bit is masked off
            m /= 256.0;
            new_exponent += 1;
        }
        uint32_t new_mantissa = static_cast<uint32_t>(m) & 0x007FFFFF;
        uint32_t new_bits = (new_exponent << 24) | new_mantissa;

        Logger::instance().info("{}Retarget at height {}: actual timespan {}s (clamped to [{}, {}]), nBits {:#010x} -> {:#010x}.",
            prefix, headers.size(), actual_timespan, min_timespan, max_timespan, current_bits, new_bits);
        return new_bits;
    }
}

#endif
