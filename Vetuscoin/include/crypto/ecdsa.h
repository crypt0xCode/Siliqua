#ifndef ECDSA_H
#define ECDSA_H

#include <secp256k1.h>
#include <picosha2.h>
#include <cryptopp/ripemd.h>
#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include <logger.h>
#include "utils.h"

#include <string>
#include <vector>
#include <print>

namespace crypto {
    /* @brief           Hash string by SHA-256.
     * @param input     out-input string.
     */
    inline void hash_sha256(std::string& input) {
        Logger::instance().debug("hash_sha256: Prepare hash256 for string {}.", input);
        picosha2::hash256_hex_string(input, input);
    }

    /* @brief               Hash string by SHA-256 to char array.
     * @param str_begin     iterator of string's begin.
     * @param str_end       iterator of string's end.
     * @param hash_array    out container for char array.
     */
    template <typename StringIter, typename VectorContainer>
    void hash_sha256(StringIter str_begin, StringIter str_end, VectorContainer& hash_array) {
        Logger::instance().debug("hash_sha256: Prepare hash256 for hash array.");
        picosha2::hash256(str_begin, str_end, hash_array);
    }

    /* @brief   Compute SHA-256 of a fixed-size array (any size, but output is always 32 bytes).
     * @param   input   array of bytes (size known at compile time)
     * @param   output  array of exactly 32 bytes where hash will be written
     */
    template <size_t N>
    inline void hash_sha256(const std::array<uint8_t, N>& input, std::array<uint8_t, 32>& output) {
        Logger::instance().debug("hash_sha256: Prepare hash256 for fixed-size array of size {}.", N);
        picosha2::hash256(input.begin(), input.end(), output.begin(), output.end());
    }

    /* @brief   Compute SHA-256 of a byte vector, store result in a fixed-size array.
     * @param   input   vector of bytes to hash.
     * @param   output  array of exactly 32 bytes where hash will be written.
     */
    inline void hash_sha256(const std::vector<uint8_t>& input, std::array<uint8_t, 32>& output) {
        Logger::instance().debug("hash_sha256: Prepare hash256 for byte vector of size {}.", input.size());
        picosha2::hash256(input.begin(), input.end(), output.begin(), output.end());
    }

    /* @brief   Compute SHA-256 of raw bytes.
     * @param   data    pointer to bytes.
     * @param   len     number of bytes.
     * @param   output  array of exactly 32 bytes where hash will be written.
     */
    inline void hash_sha256(const uint8_t* data, size_t len, std::array<uint8_t, 32>& output) {
        Logger::instance().debug("hash_sha256: Prepare hash256 for raw bytes of size {}.", len);
        picosha2::hash256(data, data + len, output.begin(), output.end());
    }

    /* @brief              Encrypt string by double SHA256.
     * @param  input       out-input string.
     */
    inline void hash_double_sha256(std::string& input) {
        hash_sha256(input);
        hash_sha256(input);
    }

    /* @brief   Compute double SHA-256 of a byte vector.
     * @param   input   vector of bytes
     * @param   output  array of exactly 32 bytes where final hash will be written
     */
    inline void hash_double_sha256(const std::vector<uint8_t>& input, std::array<uint8_t, 32>& output) {
        std::array<uint8_t, 32> first;
        hash_sha256(input, first);
        hash_sha256(first, output);
    }

    /* @brief   Compute double SHA-256 of raw bytes.
     * @param   data    pointer to bytes
     * @param   len     number of bytes
     * @param   output  array of exactly 32 bytes where final hash will be written
     */
    inline void hash_double_sha256(const uint8_t* data, size_t len, std::array<uint8_t, 32>& output) {
        std::array<uint8_t, 32> first;
        hash_sha256(data, len, first);
        hash_sha256(first, output);
    }

    /* @brief                   Hash string by RIPEMD-160.
     * @param ripemd160_hash    out for ripemd-160 hash.
     * @param input             input in bytes.
     * @param len               input size.
     */
    inline void hash_ripemd160(unsigned char* ripemd160_hash, unsigned char* input, size_t len) {
        std::string input_string = bytes_to_hex(input, len);
        Logger::instance().debug("hash_ripemd160: Prepare ripemd160 for {}.", input_string);
        CryptoPP::RIPEMD160 hasher;
        hasher.CalculateDigest(ripemd160_hash, input, len);
    }

    /*  @brief						Generate new keypair.
     *  @param  input				string message.
     *  @param  keypair				out container for keypair generation in string.
     *	@param	seckey				out for secret key in bytes.
     *	@param	pubkey				out for pubkey in secp256k1.
     *	@param	compressed_pubkey	out for pubkey in bytes.
     *  @return						exit code.
     */
    int generate_keypair(std::string input, std::vector<std::string>& keypair, unsigned char* seckey,
        secp256k1_pubkey& pubkey, unsigned char* compressed_pubkey);

    /*  @brief							Generate address from public key.
     *  @param  compressed_pubkey		pubkey in bytes.
     *  @param  ripemd160_hash			out for ripemd-160 hash bytes array.
     *	@param	ripemd160_hash_string	out for ripemd-160 hash string.
     *  @return							exit code.
     */
    int create_address_from_pubkey(unsigned char* compressed_pubkey, unsigned char* ripemd160_hash,
        std::string& ripemd160_hash_string);

    /* @brief						Generate sign for secret key.
     * @param input					string message.
     * @param seckey				secret key.
     * @param serialized_signature	out for signature in bytes.
     * @param str_signature			out for signature in string.
     * @param sig                   out for signature in secp256k1.
     * @param input_hash			out for message hash in bytes.
     * @return						exit code.
     */
    int generate_sign(std::string input, unsigned char* seckey, unsigned char* serialized_signature,
        std::string& str_signature, secp256k1_ecdsa_signature& sig, unsigned char* input_hash);

    /* @brief                       Sign verification.
     * @param sig                   signature for verification in secp256k1.
     * @param serialized_signature  signature for verification in bytes.
     * @param pubkey                public key.
     * @param compressed_pubkey     public key in bytes.
     * @param input_hash            hash of input message.
     * @return                      exit code.
     */
    int verify_sign(secp256k1_ecdsa_signature& sig, unsigned char* serialized_signature,
        secp256k1_pubkey& pubkey, unsigned char* compressed_pubkey, unsigned char* input_hash);
}
#endif