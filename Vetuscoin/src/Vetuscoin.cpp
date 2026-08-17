// Vetuscoin.cpp: определяет точку входа для приложения.
//
#include "Vetuscoin.h"
using namespace crypto;
using namespace transaction;
using namespace block;
using namespace proof_of_work;
using namespace storage;
using namespace network;

int main(int argc, char* argv[])
{
    std::vector<std::string> args(argv + 1, argv + argc);

    // ./Vetuscoin --seed <path>                        - mine a fresh genesis-only chain and save it.
    // The genesis coinbase pays an all-zero placeholder address (nobody's wallet), so the seed
    // chain stays neutral - every node that copies it starts equally unable to spend it, and
    // real balances only start accruing from each node's own first --listen run onward.
    if (args.size() >= 2 && args[0] == "--seed") {
        save_chain(build_genesis_chain(std::array<uint8_t, wallet::ADDRESS_SIZE>{}), args[1]);
        return 0;
    }

    // ./Vetuscoin --listen <port> [chain_path]          - extend chain_path by one block, serve it to one peer.
    if (args.size() >= 2 && args[0] == "--listen") {
        uint16_t port = static_cast<uint16_t>(std::stoi(args[1]));
        std::string chain_path = args.size() >= 3 ? args[2] : "chain.dat";
        run_listener(port, chain_path);
        return 0;
    }

    // ./Vetuscoin --connect <host> <port> [chain_path]  - fetch a block and, if valid, append it to chain_path.
    if (args.size() >= 3 && args[0] == "--connect") {
        std::string host = args[1];
        uint16_t port = static_cast<uint16_t>(std::stoi(args[2]));
        std::string chain_path = args.size() >= 4 ? args[3] : "chain.dat";
        run_connector(host, port, chain_path);
        return 0;
    }

    // ./Vetuscoin --address <chain_path>                - print this node's wallet address (Base58Check).
    if (args.size() >= 2 && args[0] == "--address") {
        wallet::Wallet w = load_or_create_wallet(args[1] + ".wallet");
        std::println("{}", wallet::encode_address(w.Address()));
        return 0;
    }

    // ./Vetuscoin --balance <chain_path>                - print this node's spendable UTXOs and total.
    if (args.size() >= 2 && args[0] == "--balance") {
        wallet::Wallet w = load_or_create_wallet(args[1] + ".wallet");
        UtxoSet owned = filter_utxos_by_address(build_utxo_set(load_chain(args[1])), w.Address());
        int64_t total = 0;
        for (const auto& [outpoint, txout] : owned) {
            total += txout.nValue;
        }
        std::println("{} UTXO(s), {} satoshi total.", owned.size(), total);
        return 0;
    }

    // ./Vetuscoin --send <host> <port> <chain_path> <recipient_address> <amount>
    //                                                    - sign a spend from chain_path's wallet
    //                                                      (paying network::DEFAULT_FEE) and send
    //                                                      it to a peer's mempool.
    if (args.size() >= 6 && args[0] == "--send") {
        std::string host = args[1];
        uint16_t port = static_cast<uint16_t>(std::stoi(args[2]));
        std::string chain_path = args[3];
        auto recipient = wallet::decode_address(args[4]);
        int64_t amount = std::stoll(args[5]);
        run_send_tx(host, port, chain_path, recipient, amount);
        return 0;
    }

    // ./Vetuscoin --receive-tx <port> <chain_path>       - accept one TX from a peer into the mempool.
    if (args.size() >= 3 && args[0] == "--receive-tx") {
        uint16_t port = static_cast<uint16_t>(std::stoi(args[1]));
        std::string chain_path = args[2];
        run_receive_tx(port, chain_path);
        return 0;
    }

    // ./Vetuscoin --daemon <port> <chain_path> [host:port ...]
    //                                                    - run forever: accept any number of
    //                                                      peers, mine and sync with any listed
    //                                                      known peers every few seconds.
    if (args.size() >= 3 && args[0] == "--daemon") {
        uint16_t port = static_cast<uint16_t>(std::stoi(args[1]));
        std::string chain_path = args[2];
        std::vector<network::PeerAddress> peers;
        for (size_t i = 3; i < args.size(); ++i) {
            size_t colon = args[i].find(':');
            if (colon == std::string::npos) {
                Logger::instance().error("main: Ignoring malformed peer '{}', expected host:port.", args[i]);
                continue;
            }
            peers.push_back({ args[i].substr(0, colon), static_cast<uint16_t>(std::stoi(args[i].substr(colon + 1))) });
        }
        network::Node node(port, chain_path, peers);
        node.Run();
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

    // Build and persist a tiny UTXO set from coinbaseTx's output, then reload it.
    UtxoSet utxos;
    utxos.emplace(COutPoint(coinbaseTx.tx_hash, 0), coinbaseTx.vout.at(0));
    const std::string utxo_path = "utxo.dat";
    save_utxo_set(utxos, utxo_path);
    UtxoSet loadedUtxos = load_utxo_set(utxo_path);

    Logger::instance().info("{}UTXO round-trip: saved {} entrie(s), loaded {} entrie(s).\n",
        prefix, utxos.size(), loadedUtxos.size());

    return 0;
}
