#include "Siliqua.h"
using namespace crypto;
using namespace transaction;
using namespace block;
using namespace proof_of_work;
using namespace storage;
using namespace network;

namespace {
    constexpr int EXIT_RUNTIME_ERROR = 1;
    constexpr int EXIT_USAGE_ERROR = 2;
    constexpr size_t UNLIMITED = static_cast<size_t>(-1);

    struct CommandSpec {
        const char* flag;
        size_t min_args;    // arguments required after the flag
        size_t max_args;    // UNLIMITED for a variable-length tail
        const char* usage;
        const char* summary;
    };

    constexpr CommandSpec kCommands[] = {
        { "--seed",       1, 1,         "--seed <chain_path>",                                                 "mine a fresh genesis-only chain and save it" },
        { "--listen",     1, 2,         "--listen <port> [chain_path]",                                        "mine one block, serve it to one peer" },
        { "--connect",    2, 3,         "--connect <host> <port> [chain_path]",                                "fetch a block from a peer and append it if valid" },
        { "--address",    1, 1,         "--address <chain_path>",                                              "print this node's wallet address (Base58Check)" },
        { "--balance",    1, 1,         "--balance <chain_path>",                                              "print this node's spendable UTXOs and total" },
        { "--send",       5, 5,         "--send <host> <port> <chain_path> <recipient_address> <amount>",      "sign a payment and send it to a peer's mempool" },
        { "--receive-tx", 2, 2,         "--receive-tx <port> <chain_path>",                                    "accept one transaction from a peer into the mempool" },
        { "--daemon",     2, UNLIMITED, "--daemon <port> <chain_path> [host:port ...] [--no-mine]",            "run a persistent node (mines + syncs; --no-mine = follower)" },
    };

    const CommandSpec* find_command(const std::string& flag) {
        for (const auto& command : kCommands) {
            if (flag == command.flag) {
                return &command;
            }
        }
        return nullptr;
    }

    void print_usage(FILE* out) {
        std::println(out, "Siliqua - Bitcoin-like blockchain node.\n");
        std::println(out, "Usage: Siliqua <command> [arguments]\n");
        for (const auto& command : kCommands) {
            std::println(out, "  {:<66} {}", command.usage, command.summary);
        }
        std::println(out, "\n  --help, -h                                                         show this help");
        std::println(out, "\nWithout any arguments the program runs a demonstration mode (hashing, keypair,");
        std::println(out, "signature, address, sample transactions; writes utxo.dat to the current folder).");
        std::println(out, "Exit codes: 0 success, 1 runtime error, 2 invalid command line.");
    }

    // Whole-string parse: "9000x" or "" are rejected, unlike a bare std::stoi.
    bool parse_port(const std::string& text, uint16_t& port) {
        try {
            size_t used = 0;
            long long value = std::stoll(text, &used);
            if (used != text.size() || value < 1 || value > 65535) {
                return false;
            }
            port = static_cast<uint16_t>(value);
            return true;
        }
        catch (const std::exception&) {
            return false;
        }
    }

    bool parse_amount(const std::string& text, int64_t& amount) {
        try {
            size_t used = 0;
            long long value = std::stoll(text, &used);
            if (used != text.size() || value <= 0) {
                return false;
            }
            amount = value;
            return true;
        }
        catch (const std::exception&) {
            return false;
        }
    }

    /* @brief           Validate the command line before anything is executed.
    *  @param   args    arguments without the program name (must not be empty).
    *  @return          -1 if the command line is valid and should be run, otherwise the exit code
    *                   to return (0 for --help, EXIT_USAGE_ERROR after a printed explanation).
    */
    int check_arguments(const std::vector<std::string>& args) {
        const std::string& flag = args[0];
        if (flag == "--help" || flag == "-h") {
            print_usage(stdout);
            return 0;
        }

        const CommandSpec* spec = find_command(flag);
        if (spec == nullptr) {
            std::println(stderr, "Unknown argument '{}'.\n", flag);
            print_usage(stderr);
            return EXIT_USAGE_ERROR;
        }

        size_t given = args.size() - 1;
        if (given < spec->min_args || given > spec->max_args) {
            std::println(stderr, "Wrong number of arguments for {}.\nUsage: Siliqua {}", spec->flag, spec->usage);
            return EXIT_USAGE_ERROR;
        }

        uint16_t port = 0;
        auto invalid = [&](const char* what, const std::string& value, const std::string& why = "") {
            std::println(stderr, "Invalid {} '{}'{}.\nUsage: Siliqua {}", what, value, why.empty() ? "" : ": " + why, spec->usage);
            return EXIT_USAGE_ERROR;
        };

        if ((flag == "--listen" || flag == "--receive-tx" || flag == "--daemon") && !parse_port(args[1], port)) {
            return invalid("port", args[1], "expected a number from 1 to 65535");
        }
        if ((flag == "--connect" || flag == "--send") && !parse_port(args[2], port)) {
            return invalid("port", args[2], "expected a number from 1 to 65535");
        }
        if (flag == "--send") {
            try {
                wallet::decode_address(args[4]);
            }
            catch (const std::exception& e) {
                return invalid("recipient address", args[4], e.what());
            }
            int64_t amount = 0;
            if (!parse_amount(args[5], amount)) {
                return invalid("amount", args[5], "expected a positive integer number of satoshi");
            }
        }
        return -1;
    }
}

// Runs an already validated command line. An empty command line falls through to the demonstration mode below.
static int run(const std::vector<std::string>& args)
{
    // ./Siliqua --seed <path>                        - mine a fresh genesis-only chain and save it.
    // The genesis coinbase pays an all-zero placeholder address (nobody's wallet), so the seed
    // chain stays neutral - every node that copies it starts equally unable to spend it, and
    // real balances only start accruing from each node's own first --listen run onward.
    if (args.size() >= 2 && args[0] == "--seed") {
        save_chain(build_genesis_chain(std::array<uint8_t, wallet::ADDRESS_SIZE>{}), args[1]);
        return 0;
    }

    // ./Siliqua --listen <port> [chain_path]          - extend chain_path by one block, serve it to one peer.
    if (args.size() >= 2 && args[0] == "--listen") {
        uint16_t port = static_cast<uint16_t>(std::stoi(args[1]));
        std::string chain_path = args.size() >= 3 ? args[2] : "chain.dat";
        run_listener(port, chain_path);
        return 0;
    }

    // ./Siliqua --connect <host> <port> [chain_path]  - fetch a block and, if valid, append it to chain_path.
    if (args.size() >= 3 && args[0] == "--connect") {
        std::string host = args[1];
        uint16_t port = static_cast<uint16_t>(std::stoi(args[2]));
        std::string chain_path = args.size() >= 4 ? args[3] : "chain.dat";
        run_connector(host, port, chain_path);
        return 0;
    }

    // ./Siliqua --address <chain_path>                - print this node's wallet address (Base58Check).
    if (args.size() >= 2 && args[0] == "--address") {
        wallet::Wallet w = load_or_create_wallet(args[1] + ".wallet");
        std::println("{}", wallet::encode_address(w.Address()));
        return 0;
    }

    // ./Siliqua --balance <chain_path>                - print this node's spendable UTXOs and total.
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

    // ./Siliqua --send <host> <port> <chain_path> <recipient_address> <amount>
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

    // ./Siliqua --receive-tx <port> <chain_path>       - accept one TX from a peer into the mempool.
    if (args.size() >= 3 && args[0] == "--receive-tx") {
        uint16_t port = static_cast<uint16_t>(std::stoi(args[1]));
        std::string chain_path = args[2];
        run_receive_tx(port, chain_path);
        return 0;
    }

    // ./Siliqua --daemon <port> <chain_path> [host:port ...] [--no-mine]
    //                                                    - run forever: accept any number of
    //                                                      peers, mine and sync with any listed
    //                                                      known peers every few seconds.
    //                                                      --no-mine: follower, never mines.
    if (args.size() >= 3 && args[0] == "--daemon") {
        uint16_t port = static_cast<uint16_t>(std::stoi(args[1]));
        std::string chain_path = args[2];
        std::vector<network::PeerAddress> peers;
        bool mine = true;
        for (size_t i = 3; i < args.size(); ++i) {
            if (args[i] == "--no-mine") {
                mine = false;
                continue;
            }
            size_t colon = args[i].find(':');
            uint16_t peer_port = 0;
            if (colon == std::string::npos || colon == 0 || !parse_port(args[i].substr(colon + 1), peer_port)) {
                Logger::instance().error("main: Ignoring malformed peer '{}', expected host:port.", args[i]);
                continue;
            }
            peers.push_back({ args[i].substr(0, colon), peer_port });
        }
        network::Node node(port, chain_path, peers, mine);
        node.Run();
    }

    std::string prefix = "main: ";
    const int STD_SIZE = 32;
    const int RIPEMD160_SIZE = 20;

    std::println("No command given: running the demonstration mode (see --help for the node commands).");
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

int main(int argc, char* argv[])
{
    std::vector<std::string> args(argv + 1, argv + argc);

    if (!args.empty()) {
        int status = check_arguments(args);
        if (status >= 0) {
            return status;
        }
    }

    // Anything thrown by the node code (for example connect_to() failing) used to reach the C++
    // runtime and abort the process with a core dump; report it and exit with a normal error code.
    try {
        return run(args);
    }
    catch (const std::exception& e) {
        Logger::instance().error("main: {}", e.what());
        std::println(stderr, "Error: {}", e.what());
        return EXIT_RUNTIME_ERROR;
    }
}
