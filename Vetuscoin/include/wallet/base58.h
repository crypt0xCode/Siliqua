#ifndef BASE58_H
#define BASE58_H

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "signing.h"
#include "crypto/ecdsa.h"

// Base58Check address encoding, the same algorithm real Bitcoin uses for its addresses (base58
// avoids characters that look alike in most fonts - 0/O, I/l - and are easy to mistype, plus a
// checksum catches typos before they turn into an unrecoverable payment to nowhere).
namespace wallet {
    inline constexpr char BASE58_ALPHABET[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

    // Own version byte, distinct from real Bitcoin's (0x00 for mainnet P2PKH) - so a Vetuscoin
    // address is never visually mistaken for a real Bitcoin one.
    constexpr uint8_t ADDRESS_VERSION_BYTE = 0x1E;

    /* @brief   Encode bytes as base58 (no checksum - see encode_address() for that).
    *  @return  base58 string.
    */
    inline std::string base58_encode(const std::vector<uint8_t>& input) {
        size_t leading_zeros = 0;
        while (leading_zeros < input.size() && input[leading_zeros] == 0) {
            leading_zeros++;
        }

        // log(256)/log(58) ~= 1.365 bytes of base58 digits per input byte, rounded up.
        std::vector<uint8_t> digits((input.size() - leading_zeros) * 138 / 100 + 1, 0);
        size_t length = 0;
        for (size_t i = leading_zeros; i < input.size(); i++) {
            int carry = input[i];
            size_t j = 0;
            for (auto it = digits.rbegin(); (carry != 0 || j < length) && it != digits.rend(); ++it, ++j) {
                carry += 256 * (*it);
                *it = static_cast<uint8_t>(carry % 58);
                carry /= 58;
            }
            length = j;
        }

        auto it = digits.begin() + (digits.size() - length);
        while (it != digits.end() && *it == 0) {
            ++it;
        }

        std::string result(leading_zeros, '1');
        while (it != digits.end()) {
            result += BASE58_ALPHABET[*it++];
        }
        return result;
    }

    /* @brief   Decode a base58 string back to bytes (inverse of base58_encode()).
    *  @return  decoded bytes.
    */
    inline std::vector<uint8_t> base58_decode(const std::string& str) {
        size_t leading_ones = 0;
        while (leading_ones < str.size() && str[leading_ones] == '1') {
            leading_ones++;
        }

        // log(58)/log(256) ~= 0.733 bytes per base58 digit, rounded up.
        std::vector<uint8_t> bytes((str.size() - leading_ones) * 733 / 1000 + 1, 0);
        size_t length = 0;
        for (size_t i = leading_ones; i < str.size(); i++) {
            const char* pos = std::strchr(BASE58_ALPHABET, str[i]);
            if (!pos || str[i] == '\0') {
                throw std::invalid_argument("base58_decode: invalid character in input");
            }
            int carry = static_cast<int>(pos - BASE58_ALPHABET);
            size_t j = 0;
            for (auto it = bytes.rbegin(); (carry != 0 || j < length) && it != bytes.rend(); ++it, ++j) {
                carry += 58 * (*it);
                *it = static_cast<uint8_t>(carry % 256);
                carry /= 256;
            }
            length = j;
        }

        auto it = bytes.begin() + (bytes.size() - length);
        while (it != bytes.end() && *it == 0) {
            ++it;
        }

        std::vector<uint8_t> result(leading_ones, 0x00);
        result.insert(result.end(), it, bytes.end());
        return result;
    }

    /* @brief           Encode an address as Base58Check: version byte + address + 4-byte
    *                   checksum (first 4 bytes of double-sha256(version + address)).
    *  @param   address raw 20-byte address.
    *  @return          human-readable, typo-checked address string.
    */
    inline std::string encode_address(const std::array<uint8_t, ADDRESS_SIZE>& address) {
        std::vector<uint8_t> payload;
        payload.push_back(ADDRESS_VERSION_BYTE);
        payload.insert(payload.end(), address.begin(), address.end());

        std::array<uint8_t, 32> checksum{};
        crypto::hash_double_sha256(payload, checksum);
        payload.insert(payload.end(), checksum.begin(), checksum.begin() + 4);

        return base58_encode(payload);
    }

    /* @brief           Decode and verify a Base58Check address string.
    *  @param   text     address as printed by encode_address().
    *  @return           raw 20-byte address.
    */
    inline std::array<uint8_t, ADDRESS_SIZE> decode_address(const std::string& text) {
        std::vector<uint8_t> decoded = base58_decode(text);
        if (decoded.size() != 1 + ADDRESS_SIZE + 4) {
            throw std::invalid_argument("decode_address: wrong length for a Vetuscoin address");
        }
        if (decoded[0] != ADDRESS_VERSION_BYTE) {
            throw std::invalid_argument("decode_address: not a Vetuscoin address (wrong version byte)");
        }

        std::vector<uint8_t> payload(decoded.begin(), decoded.end() - 4);
        std::array<uint8_t, 32> checksum{};
        crypto::hash_double_sha256(payload, checksum);
        if (!std::equal(decoded.end() - 4, decoded.end(), checksum.begin())) {
            throw std::invalid_argument("decode_address: checksum mismatch (typo?)");
        }

        std::array<uint8_t, ADDRESS_SIZE> address{};
        std::copy(decoded.begin() + 1, decoded.begin() + 1 + ADDRESS_SIZE, address.begin());
        return address;
    }
}

#endif
