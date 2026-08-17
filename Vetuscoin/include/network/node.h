#ifndef NODE_H
#define NODE_H

#include <ctime>
#include "socket.h"
#include "message.h"
#include "peer.h"
#include "core/block.h"
#include "consensus/pow.h"
#include "storage/storage.h"
#include "logger.h"
#include "utils.h"

namespace network {
    constexpr uint32_t RETARGET_INTERVAL = 5;
    constexpr uint32_t TARGET_TIMESPAN_SECONDS = 60;
    constexpr uint32_t INITIAL_BITS = 0x207fffff;
    constexpr int64_t COINBASE_REWARD = 5000000000;

    inline transaction::Transaction build_coinbase_tx() {
        transaction::CTxIn coinbase_in;
        coinbase_in.scriptSig = std::vector<uint8_t>{ 0x00 };
        transaction::Transaction tx(1, { coinbase_in }, { transaction::CTxOut(COINBASE_REWARD, std::vector<uint8_t>{0xAA, 0xBB}) }, 0);
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

    /* @brief               Mine a new block extending prev_hash, with a fresh coinbase and the
    *                       difficulty get_next_work_required() computes from headers so far.
    *  @param   headers     this node's chain headers so far (empty for the very first block).
    *  @param   prev_hash   hash of the block being extended (all-zero for the genesis block).
    *  @return              mined block, ready to append to the chain.
    */
    inline block::CBlock build_next_block(const std::vector<block::CBlockHeader>& headers, const std::array<uint8_t, 32>& prev_hash) {
        uint32_t next_bits = headers.empty()
            ? INITIAL_BITS
            : proof_of_work::get_next_work_required(headers, RETARGET_INTERVAL, TARGET_TIMESPAN_SECONDS);

        block::CBlock blk;
        blk.nVersion = 1;
        blk.hashPrevBlock = prev_hash;
        blk.nTime = static_cast<uint32_t>(std::time(nullptr));
        blk.vtx.push_back(build_coinbase_tx());
        blk.hashMerkleRoot = blk.BuildMerkleRoot();
        proof_of_work::mine_block(blk, next_bits);
        return blk;
    }

    inline std::vector<block::CBlock> build_genesis_chain() {
        return { build_next_block({}, std::array<uint8_t, 32>{}) };
    }

    // Loads chain_path, or mines a fresh single-block genesis chain if it does not exist yet.
    inline std::vector<block::CBlock> load_or_create_chain(const std::string& chain_path) {
        try {
            return storage::load_chain(chain_path);
        }
        catch (const std::exception&) {
            Logger::instance().info("load_or_create_chain: '{}' not found, mining a fresh genesis block.", chain_path);
            return build_genesis_chain();
        }
    }

    /* @brief                       Extend the chain at chain_path by one mined block, then listen
    *                               on port, accept exactly one peer, complete the VERSION/VERACK
    *                               handshake and announce/serve that new tip block.
    *  @param   port                TCP port to listen on.
    *  @param   chain_path          path to this node's persisted chain (created fresh if missing).
    */
    inline void run_listener(uint16_t port, const std::string& chain_path) {
        std::string prefix = "run_listener: ";

        std::vector<block::CBlock> chain = load_or_create_chain(chain_path);
        block::CBlock new_block = build_next_block(extract_headers(chain), chain.back().GetHash());
        chain.push_back(new_block);
        storage::save_chain(chain, chain_path);

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
    *                       hash, right nBits, valid Merkle root and PoW) - append and persist it.
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

        bool merkle_ok = received_block.IsMerkleRootValid();
        bool pow_ok = proof_of_work::check_proof_of_work(received_block);
        bool links_to_tip = received_block.hashPrevBlock == my_tip_hash;
        bool bits_ok = received_block.nBits == expected_bits;

        if (merkle_ok && pow_ok && links_to_tip && bits_ok) {
            chain.push_back(received_block);
            storage::save_chain(chain, chain_path);
            Logger::instance().info("{}Accepted block {}. Chain height now {}.", prefix,
                bytes_to_hex(received_hash.data(), received_hash.size()), chain.size());
        }
        else {
            Logger::instance().error("{}Rejected block {}. Merkle: {}, PoW: {}, links to tip: {}, bits match: {}.",
                prefix, bytes_to_hex(received_hash.data(), received_hash.size()), merkle_ok, pow_ok, links_to_tip, bits_ok);
        }
    }
}

#endif
