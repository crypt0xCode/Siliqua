#ifndef SERIALIZER_H
#define SERIALIZER_H

#include <vector>
#include <cstdint>
#include <cstring>

namespace serializer {
    /* @brief   Write 4 bytes integer to the little-endian.
    *  @return  little-endian 4 bytes.
    */
    inline void write_uint_32LE(std::vector<uint8_t>& out, uint32_t value) {
        out.push_back(static_cast<uint8_t>(value & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    }

    /* @brief   Write 8 bytes integer to the little-endian.
    *  @return  little-endian 4 bytes.
    */
    inline void write_uint_64LE(std::vector<uint8_t>& out, uint64_t value) {
        out.push_back(static_cast<uint8_t>(value & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 32) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 40) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 48) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 56) & 0xFF));
    }

    /* @brief   Write uint_32t to the varint.
    *  @return  varint value.
    */
    inline void write_var_int32(std::vector<uint8_t>& out, uint32_t value) {
        // For example, 4 bytes little-endian.
        write_uint_32LE(out, value);
    }

    /* @brief   Write any bytes to array (for example, 5 or 32 and less or more bytes).
    *  @return  bytes array.
    */
    inline void write_bytes(std::vector<uint8_t>& out, const uint8_t* data, size_t len) {
        out.insert(out.end(), data, data + len);
    }
}
#endif