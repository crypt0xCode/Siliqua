#ifndef NODE_H
#define NODE_H

#include <ctime>
#include "socket.h"
#include "message.h"
#include "peer.h"
#include "core/block.h"
#include "consensus/pow.h"
#include "storage/storage.h"
#include "wallet/wallet.h"
#include "logger.h"
#include "utils.h"

namespace network {
    constexpr uint32_t RETARGET_INTERVAL = 5;
    constexpr uint32_t TARGET_TIMESPAN_SECONDS = 60;
    constexpr uint32_t INITIAL_BITS = 0x207fffff;
    constexpr int64_t COINBASE_REWARD = 5000000000;

    inline transaction::Transaction build_coinbase_tx(const std::array<uint8_t, wallet::ADDRESS_SIZE>& reward_address) {
        transaction::CTxIn coinbase_in;
        coinbase_in.scriptSig = std::vector<uint8_t>{ 0x00 };
        transaction::Transaction tx(1, { coinbase_in },
            { transaction::CTxOut(COINBASE_REWARD, std::vector<uint8_t>(reward_address.begin(), reward_address.end())) }, 0);
        tx.tx_hash = tx.GetHash();
        return tx;
    }

    inline std::vector<block::CBlockHeader> extract_headers(const std::vector<block::CBlock>& chain) {
        std::vector<block::CBlockHeader> headers;
        headers.reserve(chain.size());
        for (const auto& blk : chain) {
            headers.push_back(static_cast<const block::CBlockHeader&>(blk));
        }
        return headers;
    }

    // Every output ever paid, minus every output ever spent - the chain's UTXO set (any owner).
    inline std::map<transaction::COutPoint, transaction::CTxOut> build_utxo_set(const std::vector<block::CBlock>& chain) {
        std::map<transaction::COutPoint, transaction::CTxOut> utxos;
        for (const auto& blk : chain) {
            for (const auto& tx : blk.vtx) {
                for (uint32_t i = 0; i < tx.vout.size(); ++i) {
                    utxos.emplace(transaction::COutPoint(tx.tx_hash, i), tx.vout.at(i));
                }
            }
        }
        for (const auto& blk : chain) {
            for (const auto& tx : blk.vtx) {
                for (const auto& in : tx.vin) {
                    utxos.erase(in.prevout);
                }
            }
        }
        return utxos;
    }

    inline std::map<transaction::COutPoint, transaction::CTxOut> filter_utxos_by_address(
        const std::map<transaction::COutPoint, transaction::CTxOut>& utxos, const std::array<uint8_t, wallet::ADDRESS_SIZE>& address) {
        std::map<transaction::COutPoint, transaction::CTxOut> owned;
        for (const auto& [outpoint, txout] : utxos) {
            if (txout.scriptPubKey.size() == wallet::ADDRESS_SIZE && std::equal(txout.scriptPubKey.begin(), txout.scriptPubKey.end(), address.begin())) {
                owned.emplace(outpoint, txout);
            }
        }
        return owned;
    }

    /* @brief                       Mine a new block extending prev_hash: a fresh coinbase paying
    *                               reward_address, plus any extra_txs, at the difficulty
    *                               get_next_work_required() computes from headers so far.
    *  @param   headers             this node's chain headers so far (empty for the very first block).
    *  @param   prev_hash           hash of the block being extended (all-zero for the genesis block).
    *  @param   reward_address      who the coinbase reward pays.
    *  @param   extra_txs           already-built and signed transactions to include, if any.
    *  @return                      mined block, ready to append to the chain.
    */
    inline block::CBlock build_next_block(const std::vector<block::CBlockHeader>& headers, const std::array<uint8_t, 32>& prev_hash,
        const std::array<uint8_t, wallet::ADDRESS_SIZE>& reward_address, const std::vector<transaction::Transaction>& extra_txs = {}) {
        uint32_t next_bits = headers.empty()
            ? INITIAL_BITS
            : proof_of_work::get_next_work_required(headers, RETARGET_INTERVAL, TARGET_TIMESPAN_SECONDS);

        block::CBlock blk;
        blk.nVersion = 1;
        blk.hashPrevBlock = prev_hash;
        blk.nTime = static_cast<uint32_t>(std::time(nullptr));
        blk.vtx.push_back(build_coinbase_tx(reward_address));
        for (const auto& tx : extra_txs) {
            blk.vtx.push_back(tx);
        }
        blk.hashMerkleRoot = blk.BuildMerkleRoot();
        proof_of_work::mine_block(blk, next_bits);
        return blk;
    }

    inline std::vector<block::CBlock> build_genesis_chain(const std::array<uint8_t, wallet::ADDRESS_SIZE>& reward_address) {
        return { build_next_block({}, std::array<uint8_t, 32>{}, reward_address) };
    }

    // Loads chain_path, or mines a fresh single-block genesis chain (paying reward_address) if missing.
    inline std::vector<block::CBlock> load_or_create_chain(const std::string& chain_path, const std::array<uint8_t, wallet::ADDRESS_SIZE>& reward_address) {
        try {
            return storage::load_chain(chain_path);
        }
        catch (const std::exception&) {
            Logger::instance().info("load_or_create_chain: '{}' not found, mining a fresh genesis block.", chain_path);
            return build_genesis_chain(reward_address);
        }
    }

    // Loads a wallet's seckey from wallet_path, or generates and saves a fresh one if missing -
    // so the same node keeps the same address (and can spend its own past coinbase rewards)
    // across runs, the same way load_or_create_chain keeps the same chain.
    inline wallet::Wallet load_or_create_wallet(const std::string& wallet_path) {
        try {
            std::vector<uint8_t> buffer = storage::read_file(wallet_path);
            if (buffer.size() != 32) {
                throw std::runtime_error("load_or_create_wallet: unexpected wallet file size");
            }
            return wallet::Wallet::FromSeckey(buffer.data());
        }
        catch (const std::exception&) {
            Logger::instance().info("load_or_create_wallet: '{}' not found, generating a fresh wallet.", wallet_path);
            wallet::Wallet w;
            std::vector<uint8_t> seckey_bytes(w.Seckey(), w.Seckey() + 32);
            storage::write_file(wallet_path, seckey_bytes);
            return w;
        }
    }

    // Loads mempool_path, or an empty mempool if it does not exist yet (nothing pending).
    inline std::vector<transaction::Transaction> load_mempool_or_empty(const std::string& mempool_path) {
        try {
            return storage::load_mempool(mempool_path);
        }
        catch (const std::exception&) {
            return {};
        }
    }

    /* @brief                       Extend the chain at chain_path by one mined block, then listen
    *                               on port, accept exactly one peer, complete the VERSION/VERACK
    *                               handshake and announce/serve that new tip block.
    *
    *                               Before mining, pulls this node's own persisted mempool
    *                               (transactions received earlier via run_receive_tx) and
    *                               includes every entry that still checks out against the
    *                               current UTXO set, then clears the mempool.
    *  @param   port                TCP port to listen on.
    *  @param   chain_path          path to this node's persisted chain (created fresh if missing).
    */
    inline void run_listener(uint16_t port, const std::string& chain_path) {
        std::string prefix = "run_listener: ";

        wallet::Wallet my_wallet = load_or_create_wallet(chain_path + ".wallet");
        std::vector<block::CBlock> chain = load_or_create_chain(chain_path, my_wallet.Address());

        std::map<transaction::COutPoint, transaction::CTxOut> utxo_set = build_utxo_set(chain);
        std::vector<transaction::Transaction> mempool = load_mempool_or_empty(chain_path + ".mempool");
        std::vector<transaction::Transaction> extra_txs;
        for (const auto& tx : mempool) {
            auto it = utxo_set.find(tx.vin.at(0).prevout);
            if (it != utxo_set.end() && wallet::verify_transaction_signature(tx, it->second.scriptPubKey)) {
                extra_txs.push_back(tx);
            }
            else {
                Logger::instance().error("{}Dropping stale/invalid mempool transaction {}.", prefix,
                    bytes_to_hex(tx.tx_hash.data(), tx.tx_hash.size()));
            }
        }
        if (!extra_txs.empty()) {
            Logger::instance().info("{}Including {} mempool transaction(s) in the new block.", prefix, extra_txs.size());
        }

        block::CBlock new_block = build_next_block(extract_headers(chain), chain.back().GetHash(), my_wallet.Address(), extra_txs);
        chain.push_back(new_block);
        storage::save_chain(chain, chain_path);
        storage::save_mempool({}, chain_path + ".mempool"); // everything pending just got confirmed or dropped

        std::array<uint8_t, 32> block_hash = new_block.GetHash();
        Logger::instance().info("{}Chain height {}, new tip {}.", prefix, chain.size(), bytes_to_hex(block_hash.data(), block_hash.size()));

        socket_t listener = create_listener(port);
        socket_t client_sock = accept_connection(listener);
        Peer peer(client_sock);

        Message msg;
        if (!peer.Receive(msg) || msg.command != command::VERSION) {
            Logger::instance().error("{}Expected VERSION, got '{}'. Dropping connection.", prefix, msg.command);
            close_socket(listener);
            return;
        }
        peer.Send(command::VERSION, build_version_payload());

        if (!peer.Receive(msg) || msg.command != command::VERACK) {
            Logger::instance().error("{}Expected VERACK, got '{}'. Dropping connection.", prefix, msg.command);
            close_socket(listener);
            return;
        }
        peer.Send(command::VERACK, {});
        Logger::instance().info("{}Handshake complete.", prefix);

        peer.Send(command::INV, std::vector<uint8_t>(block_hash.begin(), block_hash.end()));

        if (!peer.Receive(msg) || msg.command != command::GETDATA) {
            Logger::instance().error("{}Expected GETDATA, got '{}'. Dropping connection.", prefix, msg.command);
            close_socket(listener);
            return;
        }

        peer.Send(command::BLOCK, new_block.Serialize());
        Logger::instance().info("{}Served block {} to peer.", prefix, bytes_to_hex(block_hash.data(), block_hash.size()));

        close_socket(listener);
    }

    /* @brief               Connect to a peer, fetch the block it announces, and - only if it
    *                       correctly extends this node's own chain at chain_path (right previous
    *                       hash, right nBits, valid Merkle root and PoW, and every non-coinbase
    *                       transaction's signature checks out against this node's own view of
    *                       the UTXO set) - append and persist it.
    *  @param   host        peer IPv4 address.
    *  @param   port        peer TCP port.
    *  @param   chain_path  path to this node's persisted chain (must already exist - a fresh,
    *                       independently-mined genesis would not match the peer's chain).
    */
    inline void run_connector(const std::string& host, uint16_t port, const std::string& chain_path) {
        std::string prefix = "run_connector: ";

        std::vector<block::CBlock> chain;
        try {
            chain = storage::load_chain(chain_path);
        }
        catch (const std::exception&) {
            Logger::instance().error("{}No chain at '{}' - seed it first, this node needs a base chain to validate the peer's block against.", prefix, chain_path);
            return;
        }

        socket_t sock = connect_to(host, port);
        Peer peer(sock);

        peer.Send(command::VERSION, build_version_payload());

        Message msg;
        if (!peer.Receive(msg) || msg.command != command::VERSION) {
            Logger::instance().error("{}Expected VERSION, got '{}'. Dropping connection.", prefix, msg.command);
            return;
        }
        peer.Send(command::VERACK, {});

        if (!peer.Receive(msg) || msg.command != command::VERACK) {
            Logger::instance().error("{}Expected VERACK, got '{}'. Dropping connection.", prefix, msg.command);
            return;
        }
        Logger::instance().info("{}Handshake complete.", prefix);

        if (!peer.Receive(msg) || msg.command != command::INV) {
            Logger::instance().error("{}Expected INV, got '{}'. Dropping connection.", prefix, msg.command);
            return;
        }

        peer.Send(command::GETDATA, msg.payload); // echo the announced hash back as the request

        if (!peer.Receive(msg) || msg.command != command::BLOCK) {
            Logger::instance().error("{}Expected BLOCK, got '{}'. Dropping connection.", prefix, msg.command);
            return;
        }

        size_t offset = 0;
        block::CBlock received_block = block::CBlock::Deserialize(msg.payload, offset);
        std::array<uint8_t, 32> received_hash = received_block.GetHash();

        std::array<uint8_t, 32> my_tip_hash = chain.back().GetHash();
        uint32_t expected_bits = proof_of_work::get_next_work_required(extract_headers(chain), RETARGET_INTERVAL, TARGET_TIMESPAN_SECONDS);
        std::map<transaction::COutPoint, transaction::CTxOut> utxo_set = build_utxo_set(chain);

        bool merkle_ok = received_block.IsMerkleRootValid();
        bool pow_ok = proof_of_work::check_proof_of_work(received_block);
        bool links_to_tip = received_block.hashPrevBlock == my_tip_hash;
        bool bits_ok = received_block.nBits == expected_bits;

        bool txs_ok = true;
        for (const auto& tx : received_block.vtx) {
            if (tx.IsCoinbase()) {
                continue;
            }
            auto it = utxo_set.find(tx.vin.at(0).prevout);
            if (it == utxo_set.end() || !wallet::verify_transaction_signature(tx, it->second.scriptPubKey)) {
                txs_ok = false;
                break;
            }
        }

        if (merkle_ok && pow_ok && links_to_tip && bits_ok && txs_ok) {
            chain.push_back(received_block);
            storage::save_chain(chain, chain_path);
            Logger::instance().info("{}Accepted block {}. Chain height now {}.", prefix,
                bytes_to_hex(received_hash.data(), received_hash.size()), chain.size());
        }
        else {
            Logger::instance().error("{}Rejected block {}. Merkle: {}, PoW: {}, links to tip: {}, bits match: {}, txs valid: {}.",
                prefix, bytes_to_hex(received_hash.data(), received_hash.size()), merkle_ok, pow_ok, links_to_tip, bits_ok, txs_ok);
        }
    }

    /* @brief                       Connect to a peer, build and sign a transaction spending one
    *                               of this node's own UTXOs, and send it as a TX message for the
    *                               peer to pick up into its mempool (see run_receive_tx).
    *  @param   host                peer IPv4 address.
    *  @param   port                peer TCP port.
    *  @param   chain_path          this node's chain - source of its own wallet and UTXOs.
    *  @param   recipient_address   who gets paid.
    *  @param   amount              how much (must not exceed the spent UTXO's value).
    */
    inline void run_send_tx(const std::string& host, uint16_t port, const std::string& chain_path,
        const std::array<uint8_t, wallet::ADDRESS_SIZE>& recipient_address, int64_t amount) {
        std::string prefix = "run_send_tx: ";

        wallet::Wallet my_wallet = load_or_create_wallet(chain_path + ".wallet");
        std::vector<block::CBlock> chain;
        try {
            chain = storage::load_chain(chain_path);
        }
        catch (const std::exception&) {
            Logger::instance().error("{}No chain at '{}' - nothing to spend from.", prefix, chain_path);
            return;
        }

        auto my_utxos = filter_utxos_by_address(build_utxo_set(chain), my_wallet.Address());
        if (my_utxos.empty()) {
            Logger::instance().error("{}Wallet at '{}' owns no spendable UTXO.", prefix, chain_path);
            return;
        }

        const auto& [spend_outpoint, spend_utxo] = *my_utxos.begin();
        if (amount > spend_utxo.nValue) {
            Logger::instance().error("{}Amount {} exceeds the selected UTXO's value {}.", prefix, amount, spend_utxo.nValue);
            return;
        }

        transaction::Transaction tx = my_wallet.CreateTransaction(spend_outpoint, spend_utxo.nValue, recipient_address, amount);
        std::array<uint8_t, 32> tx_hash = tx.GetHash();

        socket_t sock = connect_to(host, port);
        Peer peer(sock);

        peer.Send(command::VERSION, build_version_payload());

        Message msg;
        if (!peer.Receive(msg) || msg.command != command::VERSION) {
            Logger::instance().error("{}Expected VERSION, got '{}'. Dropping connection.", prefix, msg.command);
            return;
        }
        peer.Send(command::VERACK, {});

        if (!peer.Receive(msg) || msg.command != command::VERACK) {
            Logger::instance().error("{}Expected VERACK, got '{}'. Dropping connection.", prefix, msg.command);
            return;
        }
        Logger::instance().info("{}Handshake complete.", prefix);

        peer.Send(command::TX, tx.Serialize());
        Logger::instance().info("{}Sent transaction {} ({} satoshi to recipient, {} change back to self).", prefix,
            bytes_to_hex(tx_hash.data(), tx_hash.size()), amount, spend_utxo.nValue - amount);
    }

    /* @brief                       Listen on a port, accept exactly one peer, complete the
    *                               handshake, receive one TX message, and - only if it correctly
    *                               spends a UTXO from this node's own view of the chain - append
    *                               it to chain_path's persisted mempool for a later run_listener
    *                               to mine.
    *  @param   port                TCP port to listen on.
    *  @param   chain_path          this node's chain - source of its own UTXO view and mempool file.
    */
    inline void run_receive_tx(uint16_t port, const std::string& chain_path) {
        std::string prefix = "run_receive_tx: ";

        std::vector<block::CBlock> chain;
        try {
            chain = storage::load_chain(chain_path);
        }
        catch (const std::exception&) {
            Logger::instance().error("{}No chain at '{}' - seed it first, this node needs a UTXO view to validate incoming transactions.", prefix, chain_path);
            return;
        }
        std::map<transaction::COutPoint, transaction::CTxOut> utxo_set = build_utxo_set(chain);

        socket_t listener = create_listener(port);
        socket_t client_sock = accept_connection(listener);
        Peer peer(client_sock);

        Message msg;
        if (!peer.Receive(msg) || msg.command != command::VERSION) {
            Logger::instance().error("{}Expected VERSION, got '{}'. Dropping connection.", prefix, msg.command);
            close_socket(listener);
            return;
        }
        peer.Send(command::VERSION, build_version_payload());

        if (!peer.Receive(msg) || msg.command != command::VERACK) {
            Logger::instance().error("{}Expected VERACK, got '{}'. Dropping connection.", prefix, msg.command);
            close_socket(listener);
            return;
        }
        peer.Send(command::VERACK, {});
        Logger::instance().info("{}Handshake complete.", prefix);

        if (!peer.Receive(msg) || msg.command != command::TX) {
            Logger::instance().error("{}Expected TX, got '{}'. Dropping connection.", prefix, msg.command);
            close_socket(listener);
            return;
        }

        size_t offset = 0;
        transaction::Transaction tx = transaction::Transaction::Deserialize(msg.payload, offset);
        std::array<uint8_t, 32> tx_hash = tx.GetHash();

        auto it = utxo_set.find(tx.vin.at(0).prevout);
        bool valid = !tx.IsCoinbase() && it != utxo_set.end() && wallet::verify_transaction_signature(tx, it->second.scriptPubKey);

        if (valid) {
            std::vector<transaction::Transaction> mempool = load_mempool_or_empty(chain_path + ".mempool");
            mempool.push_back(tx);
            storage::save_mempool(mempool, chain_path + ".mempool");
            Logger::instance().info("{}Accepted transaction {} into mempool ({} pending).", prefix,
                bytes_to_hex(tx_hash.data(), tx_hash.size()), mempool.size());
        }
        else {
            Logger::instance().error("{}Rejected transaction {} - signature/UTXO check failed.", prefix,
                bytes_to_hex(tx_hash.data(), tx_hash.size()));
        }

        close_socket(listener);
    }
}

#endif
