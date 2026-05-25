// Bitcent.cpp: определяет точку входа для приложения.
//
#include "Vetuscoin.h"
using namespace crypto;
using namespace transaction;
using namespace block;

int main()
{
    std::string prefix = "main: ";
    const int STD_SIZE = 32;
    const int RIPEMD160_SIZE = 20;

    Logger::instance().info("Starting program");

    // TODO: in Bitcoin input is serialize tx, not string.
    std::string input{ "" };
    std::print("Enter new string: ");
    getline(std::cin, input);
    std::string str_hash = input;

    hash_sha256(str_hash);
    std::println("\nMessage: {}.\nMessage hash: {}.\n", input, str_hash);
    str_hash = input;

    hash_double_sha256(str_hash);
    std::println("Message: {}.\nMessage double hash: {}.\n", input, str_hash);

    // Generate keypair.
    std::vector<std::string> keypair;
    unsigned char seckey[STD_SIZE];                  // 32 bytes
    secp256k1_pubkey pubkey_secp256k1;
    unsigned char compressed_pubkey[STD_SIZE + 1];   // 33 bytes
    int keypair_result = generate_keypair(input, keypair, seckey, pubkey_secp256k1, compressed_pubkey);
    if (keypair_result != 0) {
        return EXIT_FAILURE;
    }
    else {
        Logger::instance().info("{}Keypair generation completed successfully.\nSecret key: {}.\nPublic key: {}.\n", prefix, keypair.at(0), keypair.at(1));
    }

    // Generate signature.
    unsigned char serialized_signature[STD_SIZE * 2]; // 64 bytes
    std::string string_signature;
    secp256k1_ecdsa_signature sig_secp256k1;
    unsigned char input_hash[STD_SIZE];               // 32 bytes
    int signature_result = generate_sign(input, seckey, serialized_signature, string_signature, sig_secp256k1, input_hash);
    if (signature_result != 0) {
        return EXIT_FAILURE;
    }
    else {
        Logger::instance().info("{}Signature generation completed successfully.\nSignature: {}.\n", prefix, string_signature);
    }

    // Verify pubkey with signature.
    int verifying_result = verify_sign(sig_secp256k1, serialized_signature, pubkey_secp256k1, compressed_pubkey, input_hash);
    if (verifying_result != 0) {
        return EXIT_FAILURE;
    }
    else {
        Logger::instance().info("{}Signature verification completed successfully.\n", prefix);
    }

    // Generate address from public key.
    unsigned char serialized_address[RIPEMD160_SIZE];
    std::string string_serialized_address;
    int pubkey_to_address_result = create_address_from_pubkey(compressed_pubkey, serialized_address, string_serialized_address);
    if (pubkey_to_address_result != 0) {
        return EXIT_FAILURE;
    }
    else {
        Logger::instance().info("{}Address generation completed successfully.\nAddress: {}.\n\n\n\n", prefix, string_serialized_address);
    }


    // Create new tx.
    std::vector<CTxIn> enterings;
    std::vector<CTxOut> outs;

    // Entering example.
    COutPoint prevout;
    prevout.txid.fill(1);
    prevout.n = 1;
    CTxIn txin(prevout, std::vector<uint8_t>{0x01, 0x02}, 0xFFFFFFFF);
    enterings.push_back(txin);

    // Out example.
    CTxOut txout(100000, std::vector<uint8_t>{0xAA, 0xBB});
    outs.push_back(txout);

    Transaction tx(1, enterings, outs, 0);
    tx.tx_hash = tx.GetHash();
    Logger::instance().info("{}New transaction generated successfully.\nHash: {}.\n", prefix, bytes_to_hex(tx.tx_hash.data(), tx.tx_hash.size()));
    Logger::instance().info("{}Is coinbase? {}.\n", prefix, tx.IsCoinbase());

    // Coinbase example.
    CTxIn coinbaseIn;
    coinbaseIn.scriptSig = std::vector<uint8_t>{ 0x00 };
    Transaction coinbaseTx(1, { coinbaseIn }, outs, 0);
    coinbaseTx.tx_hash = coinbaseTx.GetHash();
    Logger::instance().info("{}New transaction generated successfully.\nHash: {}.\n", prefix, bytes_to_hex(coinbaseTx.tx_hash.data(), coinbaseTx.tx_hash.size()));
    Logger::instance().info("{}Is coinbase? {}.\n", prefix, coinbaseTx.IsCoinbase());

    // Block example.
    CBlock firstBlock;
    firstBlock.hashPrevBlock = std::array<uint8_t, 32>{};
    firstBlock.nBits = 0x1d00ffff;
    firstBlock.nNonce = 0;
    firstBlock.vtx.push_back(coinbaseTx);
    firstBlock.hashMerkleRoot = firstBlock.BuildMerkleRoot();

    Logger::instance().info("{}New block generated successfully."
        "\nHash previous block: {}\nMerkle Root Hash: {}" 
        "\nnBit: {}\nnNonce: {}\nIs Merkle Root valid: {}\nIs valid: {}\n",
        prefix,
        bytes_to_hex(firstBlock.hashPrevBlock.data(), firstBlock.hashPrevBlock.size()),
        bytes_to_hex(firstBlock.hashMerkleRoot.data(), firstBlock.hashMerkleRoot.size()),
        firstBlock.nBits, firstBlock.nNonce, firstBlock.IsMerkleRootValid(), firstBlock.IsValid());
    return 0;
}
