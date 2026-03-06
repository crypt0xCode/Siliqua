#include "crypto/ecdsa.h"
#include "crypto/secp256k1context.h"

secp256k1_context* ctx = Secp256k1Context::instance().get();

namespace crypto {
    int generate_keypair(std::string input, std::vector<std::string>& keypair, unsigned char* seckey,
        secp256k1_pubkey& pubkey, unsigned char* compressed_pubkey) {
        const int STD_SIZE = 32;

        unsigned char randomize[STD_SIZE];

        size_t len;
        int return_val;

        // Generate secret key.
        Logger::instance().debug("Generate secret key.");
        if (!fill_random(seckey, STD_SIZE)) {
            Logger::instance().error("Failed to generate randomness.");
            return EXIT_FAILURE;
        }
        else {
            Logger::instance().info("Secret key generated successfully.\n");
        }

        // Verify secret key.
        Logger::instance().debug("Verify secret key.");
        if (!secp256k1_ec_seckey_verify(ctx, seckey)) {
            Logger::instance().error("Generated secret key is invalid. This indicates an issue with the random number generator.\n");
            return EXIT_FAILURE;
        }
        else {
            Logger::instance().info("Secret key verified successfully.\n");
        }

        /* public key creation using a valid context with a verified secret key should never fail */
        Logger::instance().debug("Create public key.");
        return_val = secp256k1_ec_pubkey_create(ctx, &pubkey, seckey);
        if (return_val == 0)
            return EXIT_FAILURE;
        else
            Logger::instance().info("Public key created successfully.\n");

        /* serialize the pubkey in a compressed form(33 bytes). should always return 1. */
        len = STD_SIZE + 1;
        return_val = secp256k1_ec_pubkey_serialize(ctx, compressed_pubkey, &len, &pubkey, SECP256K1_EC_COMPRESSED);
        if (return_val == 0) {
            Logger::instance().error("Failed to serialize public key.");
            return EXIT_FAILURE;
        }

        /* should be the same size as the size of the output, because we passed a 33 byte array. */
        if (len != STD_SIZE + 1) {
            Logger::instance().error("Len of public key not equal 33 bytes. Len: {}", len);
            return EXIT_FAILURE;
        }

        // Generate keypair to string.
        Logger::instance().debug("Getting keypair: seckey + pubkey.\n");
        std::string seckey_str = "0x" + bytes_to_hex(seckey, STD_SIZE);
        std::string pubkey_str = "0x" + bytes_to_hex(compressed_pubkey, STD_SIZE + 1);
        keypair = { seckey_str, pubkey_str };

        return EXIT_SUCCESS;
    }

    int create_address_from_pubkey(unsigned char* compressed_pubkey, unsigned char* ripemd160_hash,
        std::string& ripemd160_hash_string) {
        const int STD_SIZE = 32;
        const int RIPEMD160_SIZE = 20;

        std::vector<unsigned char> compressed_pubkey_hash_vector(STD_SIZE);

        Logger::instance().debug("Generate address for public key.");
        // SHA256 hash from compressed_pubkey.
        hash_sha256(compressed_pubkey, compressed_pubkey + STD_SIZE + 1, compressed_pubkey_hash_vector);

        hash_ripemd160(ripemd160_hash, compressed_pubkey_hash_vector.data(), STD_SIZE);
        if ((bytes_to_hex(ripemd160_hash, RIPEMD160_SIZE)).length() / 2 != RIPEMD160_SIZE) {
            Logger::instance().error("Failed to generate address for public key.");
            return EXIT_FAILURE;
        }

        // RIPEMD-160 hash from SHA260 compressed pubkey hash.
        ripemd160_hash_string = "0x" + bytes_to_hex(ripemd160_hash, RIPEMD160_SIZE);

        return EXIT_SUCCESS;
    }

    int generate_sign(std::string input, unsigned char* seckey, unsigned char* serialized_signature,
        std::string& str_signature, secp256k1_ecdsa_signature& sig, unsigned char* input_hash) {
        const int STD_SIZE = 32;

        std::vector<unsigned char> input_hash_vector(STD_SIZE);

        unsigned char randomize[STD_SIZE];
        int return_val;

        // Generate hash for input string.
        Logger::instance().debug("Generate hash for message.");
        hash_sha256(input.begin(), input.end(), input_hash_vector);

        /* Generate an ECDSA signature `noncefp` and `ndata` allows you to pass a
            * custom nonce function, passing `NULL` will use the RFC-6979 safe default.
            * Signing with a valid context, verified secret key
            * and the default nonce function should never fail. */
        std::memcpy(input_hash, input_hash_vector.data(), STD_SIZE);    // vector data to char ptr
        return_val = secp256k1_ecdsa_sign(ctx, &sig, input_hash, seckey, NULL, NULL);
        if (return_val == 0) {
            Logger::instance().error("Failed to generate signature.");
            return EXIT_FAILURE;
        }

        /* Serialize the signature in a compact form. Should always return 1
        * according to the documentation in secp256k1.h. */
        return_val = secp256k1_ecdsa_signature_serialize_compact(ctx, serialized_signature, &sig);
        if (return_val == 0) {
            Logger::instance().error("Failed to serialize signature.");
            return EXIT_FAILURE;
        }

        // Generate sign to string.
        Logger::instance().debug("Getting signature.\n");
        str_signature = "0x" + bytes_to_hex(serialized_signature, STD_SIZE * 2);

        return EXIT_SUCCESS;
    }

    int verify_sign(secp256k1_ecdsa_signature& sig, unsigned char* serialized_signature,
        secp256k1_pubkey& pubkey, unsigned char* compressed_pubkey, unsigned char* input_hash) {
        const int STD_SIZE = 32;

        int is_signature_valid;

        /* Deserialize the signature. This will return 0 if the signature can't be parsed correctly. */
        if (!secp256k1_ecdsa_signature_parse_compact(ctx, &sig, serialized_signature)) {
            Logger::instance().error("Failed parsing the signature.\n");
            return EXIT_FAILURE;
        }

        /* Deserialize the public key. This will return 0 if the public key can't be parsed correctly. */
        if (!secp256k1_ec_pubkey_parse(ctx, &pubkey, compressed_pubkey, STD_SIZE + 1)) {
            Logger::instance().error("Failed parsing the public key.n");
            return EXIT_FAILURE;
        }

        /* Verify a signature. This will return 1 if it's valid and 0 if it's not. */
        is_signature_valid = secp256k1_ecdsa_verify(ctx, &sig, input_hash, &pubkey);
        Logger::instance().info("Is the signature valid? {}!\n", is_signature_valid ? "true" : "false");

        return EXIT_SUCCESS;
    }
}