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
    constexpr size_t MAX_MEMPOOL_SIZE = 1000; // caps how much unconfirmed data a weak node has to hold/rescan

    constexpr int64_t INITIAL_REWARD = 5000000000; // 50 coins, block 0's coinbase before any halving
    constexpr uint32_t HALVING_INTERVAL = 10;       // blocks per halving (small, so it's actually testable)
    constexpr int64_t DEFAULT_FEE = 1000;           // flat fee run_send_tx pays - no fee market/estimation here

    /* @brief           Block reward at a given height: halves every HALVING_INTERVAL blocks,
    *                   the same idea as Bitcoin's 210,000-block halving (just on a scale small
    *                   enough to actually observe within a test run).
    *  @param   height  position of the block in the chain (0 = genesis).
    *  @return          coinbase reward before fees, in satoshi.
    */
    inline int64_t calculate_block_reward(uint32_t height) {
        uint32_t halvings = height / HALVING_INTERVAL;
        if (halvings >= 63) {
            return 0; // reward would already be 0 from right-shifting past the sign bit anyway
        }
        return INITIAL_REWARD >> halvings;
    }

    inline transaction::Transaction build_coinbase_tx(const std::array<uint8_t, wallet::ADDRESS_SIZE>& reward_address, uint32_t height, int64_t total_fees) {
        transaction::CTxIn coinbase_in;
        // Embed the height (same idea as Bitcoin's BIP34). Without this, two blocks mined by the
        // same address for the same reward+fees - the common case before the first halving -
        // would produce byte-identical coinbase transactions: same tx_hash, colliding in the
        // UTXO set (the second one silently fails to insert, its reward effectively vanishing).
        coinbase_in.scriptSig = {
            static_cast<uint8_t>(height & 0xFF), static_cast<uint8_t>((height >> 8) & 0xFF),
            static_cast<uint8_t>((height >> 16) & 0xFF), static_cast<uint8_t>((height >> 24) & 0xFF)
        };
        int64_t reward = calculate_block_reward(height) + total_fees;
        transaction::Transaction tx(1, { coinbase_in },
            { transaction::CTxOut(reward, script::build_p2pkh_script_pubkey(reward_address)) }, 0);
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
    inline transaction::UtxoSet build_utxo_set(const std::vector<block::CBlock>& chain) {
        transaction::UtxoSet utxos;
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

    inline transaction::UtxoSet filter_utxos_by_address(
        const transaction::UtxoSet& utxos, const std::array<uint8_t, wallet::ADDRESS_SIZE>& address) {
        transaction::UtxoSet owned;
        for (const auto& [outpoint, txout] : utxos) {
            std::array<uint8_t, wallet::ADDRESS_SIZE> extracted{};
            if (script::extract_p2pkh_address(txout.scriptPubKey, extracted) && extracted == address) {
                owned.emplace(outpoint, txout);
            }
        }
        return owned;
    }

    /* @brief               Check every input of a non-coinbase transaction against utxo_set:
    *                       each must reference an existing entry and unlock it (script::evaluate
    *                       via wallet::verify_transaction_signature), and outputs must not
    *                       exceed inputs (the difference is the fee).
    *  @param   tx          transaction to check (must not be a coinbase).
    *  @param   utxo_set    UTXO state to check inputs against.
    *  @return              the fee (>= 0) if the transaction is valid, or -1 if not.
    */
    inline int64_t validate_and_get_fee(const transaction::Transaction& tx, const transaction::UtxoSet& utxo_set) {
        if (tx.vin.empty()) {
            return -1;
        }
        int64_t input_total = 0;
        for (size_t i = 0; i < tx.vin.size(); ++i) {
            auto it = utxo_set.find(tx.vin.at(i).prevout);
            if (it == utxo_set.end() || !wallet::verify_transaction_signature(tx, i, it->second.scriptPubKey)) {
                return -1;
            }
            input_total += it->second.nValue;
        }
        int64_t output_total = 0;
        for (const auto& out : tx.vout) {
            output_total += out.nValue;
        }
        if (output_total > input_total) {
            return -1;
        }
        return input_total - output_total;
    }

    /* @brief           Validate an entire candidate chain from its own genesis: every block's
    *                   Merkle root, PoW, previous-hash linkage, nBits (replaying
    *                   get_next_work_required() the same way the chain was supposedly built)
    *                   and coinbase amount (calculate_block_reward() + that block's total fees),
    *                   plus every non-coinbase transaction's signatures and value balance,
    *                   checked incrementally against the UTXO state as of just before it. Used
    *                   for fork resolution (see run_connector) - a block that does not extend
    *                   our own tip could still belong to a valid, heavier competing chain, and
    *                   the only way to know is to check the whole thing from scratch.
    *  @param   chain   candidate chain, oldest first.
    *  @return          true if every block and every transaction in it checks out.
    */
    inline bool validate_full_chain(const std::vector<block::CBlock>& chain) {
        if (chain.empty()) {
            return false;
        }

        transaction::UtxoSet utxo_set;
        std::vector<block::CBlockHeader> headers_so_far;
        std::array<uint8_t, 32> expected_prev{};

        for (const auto& blk : chain) {
            if (!blk.IsMerkleRootValid() || !proof_of_work::check_proof_of_work(blk)) {
                return false;
            }
            if (blk.hashPrevBlock != expected_prev) {
                return false;
            }
            uint32_t expected_bits = headers_so_far.empty()
                ? INITIAL_BITS
                : proof_of_work::get_next_work_required(headers_so_far, RETARGET_INTERVAL, TARGET_TIMESPAN_SECONDS);
            if (blk.nBits != expected_bits) {
                return false;
            }
            if (blk.vtx.empty() || !blk.vtx.front().IsCoinbase()) {
                return false;
            }

            int64_t block_fees = 0;
            for (const auto& tx : blk.vtx) {
                if (!tx.IsCoinbase()) {
                    int64_t fee = validate_and_get_fee(tx, utxo_set);
                    if (fee < 0) {
                        return false;
                    }
                    block_fees += fee;
                    for (const auto& in : tx.vin) {
                        utxo_set.erase(in.prevout);
                    }
                }
                for (uint32_t i = 0; i < tx.vout.size(); ++i) {
                    utxo_set.emplace(transaction::COutPoint(tx.tx_hash, i), tx.vout.at(i));
                }
            }

            int64_t coinbase_paid = 0;
            for (const auto& out : blk.vtx.front().vout) {
                coinbase_paid += out.nValue;
            }
            uint32_t height = static_cast<uint32_t>(headers_so_far.size());
            if (coinbase_paid != calculate_block_reward(height) + block_fees) {
                return false;
            }

            headers_so_far.push_back(static_cast<const block::CBlockHeader&>(blk));
            expected_prev = blk.GetHash();
        }

        return true;
    }

    /* @brief                   Try to append received_block directly onto chain's current tip -
    *                           same checks as run_connector's fast path (Merkle root, PoW, links
    *                           to tip, correct nBits, valid txs, correct coinbase amount), packaged
    *                           as a reusable decision (mutates chain in place) instead of inline
    *                           logic, so network::Node's daemon loop can share it with run_connector
    *                           rather than duplicating the whole check list.
    *  @param   chain           chain to extend, mutated in place on success.
    *  @param   received_block  candidate next block.
    *  @return                  true if it was valid and got appended.
    */
    inline bool try_extend_chain(std::vector<block::CBlock>& chain, const block::CBlock& received_block) {
        std::array<uint8_t, 32> my_tip_hash = chain.back().GetHash();
        uint32_t expected_bits = proof_of_work::get_next_work_required(extract_headers(chain), RETARGET_INTERVAL, TARGET_TIMESPAN_SECONDS);
        uint32_t expected_height = static_cast<uint32_t>(chain.size());
        transaction::UtxoSet utxo_set = build_utxo_set(chain);

        if (!received_block.IsMerkleRootValid() || !proof_of_work::check_proof_of_work(received_block)) {
            return false;
        }
        if (received_block.hashPrevBlock != my_tip_hash || received_block.nBits != expected_bits) {
            return false;
        }

        int64_t total_fees = 0;
        for (const auto& tx : received_block.vtx) {
            if (tx.IsCoinbase()) {
                continue;
            }
            int64_t fee = validate_and_get_fee(tx, utxo_set);
            if (fee < 0) {
                return false;
            }
            total_fees += fee;
        }

        if (received_block.vtx.empty() || !received_block.vtx.front().IsCoinbase()) {
            return false;
        }
        int64_t coinbase_paid = 0;
        for (const auto& out : received_block.vtx.front().vout) {
            coinbase_paid += out.nValue;
        }
        if (coinbase_paid != calculate_block_reward(expected_height) + total_fees) {
            return false;
        }

        chain.push_back(received_block);
        return true;
    }

    /* @brief               Try to switch chain onto peer_chain - Bitcoin's actual fork rule:
    *                       only if peer_chain shares chain's genesis, is fully valid
    *                       (validate_full_chain), and carries more cumulative work.
    *  @param   chain       chain to possibly replace, mutated in place on success.
    *  @param   peer_chain  candidate competing chain.
    *  @return              true if chain was switched to peer_chain.
    */
    inline bool try_reorg(std::vector<block::CBlock>& chain, const std::vector<block::CBlock>& peer_chain) {
        bool same_genesis = !peer_chain.empty() && !chain.empty() && peer_chain.front().GetHash() == chain.front().GetHash();
        if (!same_genesis || !validate_full_chain(peer_chain)) {
            return false;
        }
        double my_work = proof_of_work::calculate_chain_work(extract_headers(chain));
        double peer_work = proof_of_work::calculate_chain_work(extract_headers(peer_chain));
        if (peer_work > my_work) {
            chain = peer_chain;
            return true;
        }
        return false;
    }

    /* @brief                       Mine a new block extending prev_hash: a fresh coinbase paying
    *                               reward_address (block reward for this height, plus total_fees),
    *                               plus any extra_txs, at the difficulty get_next_work_required()
    *                               computes from headers so far.
    *  @param   headers             this node's chain headers so far (empty for the very first block).
    *  @param   prev_hash           hash of the block being extended (all-zero for the genesis block).
    *  @param   reward_address      who the coinbase reward pays.
    *  @param   total_fees          sum of extra_txs' fees, added on top of the block subsidy.
    *  @param   extra_txs           already-built and signed transactions to include, if any.
    *  @return                      mined block, ready to append to the chain.
    */
    inline block::CBlock build_next_block(const std::vector<block::CBlockHeader>& headers, const std::array<uint8_t, 32>& prev_hash,
        const std::array<uint8_t, wallet::ADDRESS_SIZE>& reward_address, int64_t total_fees = 0, const std::vector<transaction::Transaction>& extra_txs = {}) {
        uint32_t height = static_cast<uint32_t>(headers.size());
        uint32_t next_bits = headers.empty()
            ? INITIAL_BITS
            : proof_of_work::get_next_work_required(headers, RETARGET_INTERVAL, TARGET_TIMESPAN_SECONDS);

        block::CBlock blk;
        blk.nVersion = 1;
        blk.hashPrevBlock = prev_hash;
        blk.nTime = static_cast<uint32_t>(std::time(nullptr));
        blk.vtx.push_back(build_coinbase_tx(reward_address, height, total_fees));
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
    *                               (transactions received earlier via run_receive_tx), keeps
    *                               every entry that still checks out against the current UTXO
    *                               set, includes them and collects their fees into the coinbase,
    *                               then clears the mempool.
    *  @param   port                TCP port to listen on.
    *  @param   chain_path          path to this node's persisted chain (created fresh if missing).
    */
    inline void run_listener(uint16_t port, const std::string& chain_path) {
        std::string prefix = "run_listener: ";

        wallet::Wallet my_wallet = load_or_create_wallet(chain_path + ".wallet");
        std::vector<block::CBlock> chain = load_or_create_chain(chain_path, my_wallet.Address());

        transaction::UtxoSet utxo_set = build_utxo_set(chain);
        std::vector<transaction::Transaction> mempool = load_mempool_or_empty(chain_path + ".mempool");
        std::vector<transaction::Transaction> extra_txs;
        int64_t total_fees = 0;
        for (const auto& tx : mempool) {
            int64_t fee = validate_and_get_fee(tx, utxo_set);
            if (fee >= 0) {
                extra_txs.push_back(tx);
                total_fees += fee;
                for (const auto& in : tx.vin) {
                    utxo_set.erase(in.prevout);
                }
                for (uint32_t i = 0; i < tx.vout.size(); ++i) {
                    utxo_set.emplace(transaction::COutPoint(tx.tx_hash, i), tx.vout.at(i));
                }
            }
            else {
                Logger::instance().error("{}Dropping stale/invalid mempool transaction {}.", prefix,
                    bytes_to_hex(tx.tx_hash.data(), tx.tx_hash.size()));
            }
        }
        if (!extra_txs.empty()) {
            Logger::instance().info("{}Including {} mempool transaction(s), {} satoshi in fees, in the new block.",
                prefix, extra_txs.size(), total_fees);
        }

        block::CBlock new_block = build_next_block(extract_headers(chain), chain.back().GetHash(), my_wallet.Address(), total_fees, extra_txs);
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

        // Serve whatever the peer asks for: just the new tip block (GETDATA, the common case -
        // the peer's chain already matches ours up to this point), or, if the peer finds that
        // this block does not extend its own tip, the whole chain (GETBLOCKS) so it can run its
        // own fork-resolution comparison. A peer may ask for either, or both in sequence.
        while (peer.Receive(msg)) {
            if (msg.command == command::GETDATA) {
                peer.Send(command::BLOCK, new_block.Serialize());
                Logger::instance().info("{}Served block {} to peer.", prefix, bytes_to_hex(block_hash.data(), block_hash.size()));
            }
            else if (msg.command == command::GETBLOCKS) {
                peer.Send(command::CHAIN, storage::serialize_chain(chain));
                Logger::instance().info("{}Served full chain ({} block(s)) to peer.", prefix, chain.size());
            }
            else {
                Logger::instance().error("{}Unexpected command '{}'. Dropping connection.", prefix, msg.command);
                break;
            }
        }

        close_socket(listener);
    }

    /* @brief               Connect to a peer, fetch the block it announces, and either extend
    *                       our own chain with it directly, or - if it does not extend our tip,
    *                       meaning it belongs to a competing branch or the peer is simply ahead
    *                       by more than one block - fetch the peer's whole chain and reorg onto
    *                       it if it is fully valid, shares our genesis, and has more cumulative
    *                       work than ours (Bitcoin's actual fork rule: most work wins, not
    *                       "whichever block arrived last").
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
        uint32_t expected_height = static_cast<uint32_t>(chain.size());
        transaction::UtxoSet utxo_set = build_utxo_set(chain);

        bool merkle_ok = received_block.IsMerkleRootValid();
        bool pow_ok = proof_of_work::check_proof_of_work(received_block);
        bool links_to_tip = received_block.hashPrevBlock == my_tip_hash;
        bool bits_ok = received_block.nBits == expected_bits;

        int64_t total_fees = 0;
        bool txs_ok = true;
        for (const auto& tx : received_block.vtx) {
            if (tx.IsCoinbase()) {
                continue;
            }
            int64_t fee = validate_and_get_fee(tx, utxo_set);
            if (fee < 0) {
                txs_ok = false;
                break;
            }
            total_fees += fee;
        }

        bool coinbase_ok = false;
        if (!received_block.vtx.empty() && received_block.vtx.front().IsCoinbase()) {
            int64_t coinbase_paid = 0;
            for (const auto& out : received_block.vtx.front().vout) {
                coinbase_paid += out.nValue;
            }
            coinbase_ok = coinbase_paid == calculate_block_reward(expected_height) + total_fees;
        }

        if (merkle_ok && pow_ok && links_to_tip && bits_ok && txs_ok && coinbase_ok) {
            chain.push_back(received_block);
            storage::save_chain(chain, chain_path);
            Logger::instance().info("{}Accepted block {}. Chain height now {}.", prefix,
                bytes_to_hex(received_hash.data(), received_hash.size()), chain.size());
            return;
        }

        if (merkle_ok && pow_ok && !links_to_tip) {
            // Might not be junk - could be the tip of a heavier competing branch. Ask for the
            // whole thing and compare, instead of just discarding it.
            Logger::instance().info("{}Block {} does not extend our tip - requesting peer's full chain to compare.",
                prefix, bytes_to_hex(received_hash.data(), received_hash.size()));

            peer.Send(command::GETBLOCKS, {});
            if (!peer.Receive(msg) || msg.command != command::CHAIN) {
                Logger::instance().error("{}Expected CHAIN, got '{}'. Dropping connection.", prefix, msg.command);
                return;
            }
            std::vector<block::CBlock> peer_chain = storage::deserialize_chain(msg.payload);

            bool same_genesis = !peer_chain.empty() && peer_chain.front().GetHash() == chain.front().GetHash();
            bool peer_chain_valid = same_genesis && validate_full_chain(peer_chain);
            double my_work = proof_of_work::calculate_chain_work(extract_headers(chain));
            double peer_work = peer_chain_valid ? proof_of_work::calculate_chain_work(extract_headers(peer_chain)) : 0.0;

            if (peer_chain_valid && peer_work > my_work) {
                storage::save_chain(peer_chain, chain_path);
                Logger::instance().info("{}Reorg: switched to peer's chain (height {} -> {}, more cumulative work: {} > {}).",
                    prefix, chain.size(), peer_chain.size(), peer_work, my_work);
            }
            else {
                Logger::instance().info("{}Keeping our own chain. Peer chain valid: {}, same genesis: {}, peer work {} vs ours {}.",
                    prefix, peer_chain_valid, same_genesis, peer_work, my_work);
            }
            return;
        }

        Logger::instance().error("{}Rejected block {}. Merkle: {}, PoW: {}, links to tip: {}, bits match: {}, txs valid: {}, coinbase amount valid: {}.",
            prefix, bytes_to_hex(received_hash.data(), received_hash.size()), merkle_ok, pow_ok, links_to_tip, bits_ok, txs_ok, coinbase_ok);
    }

    /* @brief                       Connect to a peer, build and sign a transaction spending
    *                               enough of this node's own UTXOs (possibly several - see
    *                               Wallet::CreateTransaction) to cover amount + DEFAULT_FEE, and
    *                               send it as a TX message for the peer to pick up into its
    *                               mempool (see run_receive_tx).
    *  @param   host                peer IPv4 address.
    *  @param   port                peer TCP port.
    *  @param   chain_path          this node's chain - source of its own wallet and UTXOs.
    *  @param   recipient_address   who gets paid.
    *  @param   amount              how much they get, before the fee.
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

        transaction::UtxoSet my_utxos = filter_utxos_by_address(build_utxo_set(chain), my_wallet.Address());

        transaction::Transaction tx;
        try {
            tx = my_wallet.CreateTransaction(my_utxos, recipient_address, amount, DEFAULT_FEE);
        }
        catch (const std::exception& e) {
            Logger::instance().error("{}Cannot build transaction: {}.", prefix, e.what());
            return;
        }
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
        Logger::instance().info("{}Sent transaction {} ({} input(s), {} satoshi to recipient, fee {}).", prefix,
            bytes_to_hex(tx_hash.data(), tx_hash.size()), tx.vin.size(), amount, DEFAULT_FEE);
    }

    /* @brief                       Listen on a port, accept exactly one peer, complete the
    *                               handshake, receive one TX message, and - only if every one of
    *                               its inputs correctly spends a UTXO from this node's own view
    *                               of the chain - append it to chain_path's persisted mempool
    *                               for a later run_listener to mine.
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
        transaction::UtxoSet utxo_set = build_utxo_set(chain);

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

        int64_t fee = tx.IsCoinbase() ? -1 : validate_and_get_fee(tx, utxo_set);
        bool valid = fee >= 0;

        std::vector<transaction::Transaction> mempool = load_mempool_or_empty(chain_path + ".mempool");
        if (valid && mempool.size() >= MAX_MEMPOOL_SIZE) {
            Logger::instance().error("{}Rejected transaction {} - mempool is full ({} pending).", prefix,
                bytes_to_hex(tx_hash.data(), tx_hash.size()), mempool.size());
        }
        else if (valid) {
            mempool.push_back(tx);
            storage::save_mempool(mempool, chain_path + ".mempool");
            Logger::instance().info("{}Accepted transaction {} ({} satoshi fee) into mempool ({} pending).", prefix,
                bytes_to_hex(tx_hash.data(), tx_hash.size()), fee, mempool.size());
        }
        else {
            Logger::instance().error("{}Rejected transaction {} - signature/UTXO/value check failed.", prefix,
                bytes_to_hex(tx_hash.data(), tx_hash.size()));
        }

        close_socket(listener);
    }
}

#endif
