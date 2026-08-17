#ifndef SERIALIZER_H
#define SERIALIZER_H

#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>
#include <cstdint>

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
    *  @return  little-endian 8 bytes.
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

    /* @brief   Write value as a varint (currently just a plain 4 bytes little-endian, not a
    *           true variable-length varint yet).
    *  @return  varint value.
    */
    inline void write_var_int32(std::vector<uint8_t>& out, uint32_t value) {
        write_uint_32LE(out, value);
    }

    /* @brief   Write len raw bytes.
    *  @return  bytes array.
    */
    inline void write_bytes(std::vector<uint8_t>& out, const uint8_t* data, size_t len) {
        out.insert(out.end(), data, data + len);
    }

    /* @brief           Read 4 bytes little-endian integer, advance offset by 4.
    *  @param   in      buffer to read from.
    *  @param   offset  in-out cursor position in the buffer.
    *  @return          decoded value.
    */
    inline uint32_t read_uint_32LE(const std::vector<uint8_t>& in, size_t& offset) {
        uint32_t value = static_cast<uint32_t>(in.at(offset))
            | (static_cast<uint32_t>(in.at(offset + 1)) << 8)
            | (static_cast<uint32_t>(in.at(offset + 2)) << 16)
            | (static_cast<uint32_t>(in.at(offset + 3)) << 24);
        offset += 4;
        return value;
    }

    /* @brief           Read 8 bytes little-endian integer, advance offset by 8.
    *  @param   in      buffer to read from.
    *  @param   offset  in-out cursor position in the buffer.
    *  @return          decoded value.
    */
    inline uint64_t read_uint_64LE(const std::vector<uint8_t>& in, size_t& offset) {
        uint64_t value = 0;
        for (int i = 0; i < 8; ++i) {
            value |= static_cast<uint64_t>(in.at(offset + i)) << (8 * i);
        }
        offset += 8;
        return value;
    }

    /* @brief   Read varint written by write_var_int32 (currently a plain 4 bytes little-endian).
    *  @return  decoded value.
    */
    inline uint32_t read_var_int32(const std::vector<uint8_t>& in, size_t& offset) {
        return read_uint_32LE(in, offset);
    }

    /* @brief           Read len raw bytes, advance offset by len.
    *  @param   in      buffer to read from.
    *  @param   offset  in-out cursor position in the buffer.
    *  @param   len     number of bytes to read.
    *  @return          bytes copied out.
    */
    inline std::vector<uint8_t> read_bytes(const std::vector<uint8_t>& in, size_t& offset, size_t len) {
        if (offset + len > in.size()) {
            throw std::out_of_range("read_bytes: not enough data in buffer");
        }
        std::vector<uint8_t> out(in.begin() + offset, in.begin() + offset + len);
        offset += len;
        return out;
    }

    /* @brief           Read a fixed 32 bytes (txid/hash), advance offset by 32.
    *  @param   in      buffer to read from.
    *  @param   offset  in-out cursor position in the buffer.
    *  @return          32 bytes copied out.
    */
    inline std::array<uint8_t, 32> read_bytes32(const std::vector<uint8_t>& in, size_t& offset) {
        if (offset + 32 > in.size()) {
            throw std::out_of_range("read_bytes32: not enough data in buffer");
        }
        std::array<uint8_t, 32> out{};
        std::copy(in.begin() + offset, in.begin() + offset + 32, out.begin());
        offset += 32;
        return out;
    }
}
#endif