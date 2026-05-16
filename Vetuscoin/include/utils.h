#ifndef UTILS_H
#define UTILS_H

#if defined(_WIN32)
/*
 * The defined WIN32_NO_STATUS macro disables return code definitions in
 * windows.h, which avoids "macro redefinition" MSVC warnings in ntstatus.h.
 */
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <bcrypt.h>
#elif defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/random.h>
#elif defined(__OpenBSD__)
#include <unistd.h>
#else
#error "Couldn't identify the OS"
#endif

#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"

/* Returns 1 on success and 0 on failure. */
inline int fill_random(unsigned char* data, size_t size) {
#if defined(_WIN32)
    NTSTATUS res = BCryptGenRandom(NULL, data, size, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (res != STATUS_SUCCESS || size > ULONG_MAX) {
        return 0;
    }
    else {
        return 1;
    }
#elif defined(__linux__) || defined(__FreeBSD__)
    /* If `getrandom(2)` is not available you should fallback to /dev/urandom */
    ssize_t res = getrandom(data, size, 0);
    if (res < 0 || (size_t)res != size) {
        return 0;
    }
    else {
        return 1;
    }
#elif defined(__APPLE__) || defined(__OpenBSD__)
    /* If `getentropy(2)` is not available you should fallback to either
     * `SecRandomCopyBytes` or /dev/urandom */
    int res = getentropy(data, size);
    if (res == 0) {
        return 1;
    }
    else {
        return 0;
    }
#endif
    return 0;
}

/*  @brief              Convert char byte array to hex.
 *  @param byte_data    unsigned char byte array.
 *  @param len          byte array length.
 *  @return             hex string.
 */
inline std::string bytes_to_hex(const unsigned char* byte_data, size_t len) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; i++) {
        stream << std::setw(2) << static_cast<int>(byte_data[i]);
    }
    return stream.str();
}


/* @brief   Write hex char symbol to byte.
 * @return  byte symbol.
 */
inline uint8_t hexCharToByte(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    else if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    else {
        throw std::invalid_argument("hexCharToByte: invalid hex symbol.");
    }
}

/* @brief   Convert hex to uint8_t array with 32 bytes.
 * @return  uint8_t array with 32 bytes.
 */
inline std::array<uint8_t, 32> hexToArray32(const std::string& hex) {
    // 32 bytes equals 64 hex symbols.
    if (hex.length() != 64) {
        throw std::length_error("hexToArray32: hex length must be only 64 symbols.");
    }

    std::array<uint8_t, 32> bytes;
    for (size_t i = 0; i < 32; ++i) {
        uint8_t high = hexCharToByte(hex[i * 2]);
        uint8_t low = hexCharToByte(hex[i * 2 + 1]);
        bytes[i] = (high << 4) | low;
    }
    return bytes;
}
#endif