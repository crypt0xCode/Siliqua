// Bitcent.cpp: определяет точку входа для приложения.
//
#include "Vetuscoin.h"
using namespace crypto;
using namespace transaction;
using namespace block;
using namespace proof_of_work;
using namespace storage;
using namespace network;

// Quick coinbase-only block for the network demo (--listen/--connect), mined at an easy
// target so it does not depend on the interactive crypto/tx demo below.
static CBlock BuildDemoBlock() {
    CTxIn coinbaseIn;
    coinbaseIn.scriptSig = std::vector<uint8_t>{ 0x00 };
    Transaction coinbaseTx(1, { coinbaseIn }, { CTxOut(5000000000, std::vector<uint8_t>{0xAA, 0xBB}) }, 0);
    coinbaseTx.tx_hash = coinbaseTx.GetHash();

    CBlock blk;
    blk.nVersion = 1;
    blk.hashPrevBlock = std::array<uint8_t, 32>{};
    blk.nTime = static_cast<uint32_t>(std::time(nullptr));
    blk.vtx.push_back(coinbaseTx);
    blk.hashMerkleRoot = blk.BuildMerkleRoot();
    mine_block(blk, 0x207fffff);
    return blk;
}

int main(int argc, char* argv[])
{
    std::vector<std::string> args(argv + 1, argv + argc);

    // ./Vetuscoin --listen <port>            - mine a demo block and serve it to one peer.
    if (args.size() >= 2 && args[0] == "--listen") {
        uint16_t port = static_cast<uint16_t>(std::stoi(args[1]));
        CBlock blockToAnnounce = BuildDemoBlock();
        run_listener(port, blockToAnnounce);
        return 0;
    }

    // ./Vetuscoin --connect <host> <port>    - fetch and validate whatever block the peer announces.
    if (args.size() >= 3 && args[0] == "--connect") {
        std::string host = args[1];
        uint16_t port = static_cast<uint16_t>(std::stoi(args[2]));
        run_connector(host, port);
        return 0;
    }

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
    firstBlock.nVersion = 1;
    firstBlock.hashPrevBlock = std::array<uint8_t, 32>{};
    firstBlock.nTime = static_cast<uint32_t>(std::time(nullptr));
    firstBlock.vtx.push_back(coinbaseTx);
    firstBlock.hashMerkleRoot = firstBlock.BuildMerkleRoot();

    // Mine the block. NOTE: the real Bitcoin genesis nBits (0x1d00ffff) requires ~2^32
    // hash attempts on average (its target has 4 leading zero bytes), which is too slow
    // for a synchronous demo run (GetHash() itself logs on every field write) - use a much
    // easier target instead, just to demonstrate the mining loop terminates and produces
    // a hash satisfying its target.
    mine_block(firstBlock, 0x207fffff);

    Logger::instance().info("{}New block generated successfully."
        "\nHash previous block: {}\nMerkle Root Hash: {}"
        "\nnBit: {}\nnNonce: {}\nIs Merkle Root valid: {}\nIs Proof of Work valid: {}\nIs valid: {}\n",
        prefix,
        bytes_to_hex(firstBlock.hashPrevBlock.data(), firstBlock.hashPrevBlock.size()),
        bytes_to_hex(firstBlock.hashMerkleRoot.data(), firstBlock.hashMerkleRoot.size()),
        firstBlock.nBits, firstBlock.nNonce, firstBlock.IsMerkleRootValid(), check_proof_of_work(firstBlock), firstBlock.IsValid());

    // Persist the chain (single block, for now) and reload it to verify the round-trip.
    std::vector<CBlock> chain{ firstBlock };
    const std::string chain_path = "chain.dat";
    save_chain(chain, chain_path);
    std::vector<CBlock> loadedChain = load_chain(chain_path);

    Logger::instance().info("{}Chain round-trip: saved {} block(s), loaded {} block(s), hash matches: {}.\n",
        prefix, chain.size(), loadedChain.size(),
        !loadedChain.empty() && loadedChain.at(0).GetHash() == firstBlock.GetHash());

    // Build and persist a tiny UTXO set from firstBlock's coinbase output, then reload it.
    std::map<COutPoint, CTxOut> utxos;
    utxos.emplace(COutPoint(coinbaseTx.tx_hash, 0), coinbaseTx.vout.at(0));
    const std::string utxo_path = "utxo.dat";
    save_utxo_set(utxos, utxo_path);
    std::map<COutPoint, CTxOut> loadedUtxos = load_utxo_set(utxo_path);

    Logger::instance().info("{}UTXO round-trip: saved {} entrie(s), loaded {} entrie(s).\n",
        prefix, utxos.size(), loadedUtxos.size());

    // Difficulty retarget demo (Этап 3, п.3.4): 5 synthetic headers, 5 seconds apart (20s total),
    // retargeting every 5 blocks against a 60s target - blocks arrived 3x faster than wanted,
    // so the next nBits should come out harder (smaller target).
    std::vector<CBlockHeader> headers;
    uint32_t demo_bits = 0x207fffff;
    uint32_t demo_time = static_cast<uint32_t>(std::time(nullptr)) - 20;
    for (int i = 0; i < 5; ++i) {
        CBlockHeader h{};
        h.nBits = demo_bits;
        h.nTime = demo_time + static_cast<uint32_t>(i) * 5;
        headers.push_back(h);
    }
    uint32_t next_bits = get_next_work_required(headers, 5, 60);
    Logger::instance().info("{}Retarget demo: 5 blocks in {}s (target 60s), nBits {:#010x} -> {:#010x}.\n",
        prefix, headers.back().nTime - headers.front().nTime, demo_bits, next_bits);

    return 0;
}
