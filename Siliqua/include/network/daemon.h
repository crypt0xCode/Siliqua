#ifndef DAEMON_H
#define DAEMON_H

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "node.h"

// Everything in node.h (run_listener, run_connector, run_send_tx, run_receive_tx) handles
// exactly one exchange with exactly one peer and then exits - fine for the CLI/testing tools,
// but not how a real Bitcoin node behaves: a real node stays up, accepts any number of peers at
// once, mines continuously, and keeps its known peers' chains in sync with its own. Node is
// that - built out of the exact same building blocks (build_next_block, try_extend_chain,
// try_reorg, validate_and_get_fee, ...), just run persistently instead of once.
namespace network {
    constexpr int DAEMON_CYCLE_SECONDS = 5; // how often the mining loop and the peer-poll loop each run

    struct PeerAddress {
        std::string host;
        uint16_t port;
    };

    class Node {
    public:
        /* @brief                   Load (or create) chain_path and its wallet, then be ready to
        *                           Run() as a persistent node.
        *  @param   listen_port     TCP port to accept incoming peers on.
        *  @param   chain_path      this node's persisted chain, wallet and mempool.
        *  @param   known_peers     other daemons to periodically sync with (may be empty).
        */
        Node(uint16_t listen_port, const std::string& chain_path, std::vector<PeerAddress> known_peers)
            : listen_port_(listen_port), chain_path_(chain_path), known_peers_(std::move(known_peers)),
              wallet_(load_or_create_wallet(chain_path + ".wallet")) {
            chain_ = load_or_create_chain(chain_path, wallet_.Address());
            storage::save_chain(chain_, chain_path_);
        }

        // Runs forever: accepts incoming peers, mines on a timer, and polls known_peers_ on a
        // timer. Never returns under normal operation - stop the process to stop the node.
        [[noreturn]] void Run() {
            Logger::instance().info("daemon: Starting on port {}. Address: {}. Chain height: {}. {} known peer(s).",
                listen_port_, wallet::encode_address(wallet_.Address()), chain_.size(), known_peers_.size());

            std::thread accept_thread(&Node::AcceptLoop, this);
            std::thread mining_thread(&Node::MiningLoop, this);
            std::thread poll_thread(&Node::PeerPollLoop, this);

            accept_thread.join();
            mining_thread.join();
            poll_thread.join();
            std::abort(); // unreachable - the loops above never return
        }

    private:
        // Accepts connections forever, handling each on its own detached thread so one slow or
        // misbehaving peer can never block the others (or the mining/poll loops).
        [[noreturn]] void AcceptLoop() {
            socket_t listener = create_listener(listen_port_);
            while (true) {
                socket_t client_sock = accept_connection(listener);
                std::thread(&Node::HandleConnection, this, client_sock).detach();
            }
        }

        // Handshake, announce our tip, then keep responding to whatever this peer asks for
        // (GETDATA/GETBLOCKS) or sends us (TX) until they disconnect.
        void HandleConnection(socket_t client_sock) {
            std::string prefix = "daemon(peer): ";
            Peer peer(client_sock);

            Message msg;
            if (!peer.Receive(msg) || msg.command != command::VERSION) {
                return;
            }
            peer.Send(command::VERSION, build_version_payload());
            if (!peer.Receive(msg) || msg.command != command::VERACK) {
                return;
            }
            peer.Send(command::VERACK, {});

            {
                std::lock_guard<std::mutex> lock(mutex_);
                std::array<uint8_t, 32> tip_hash = chain_.back().GetHash();
                peer.Send(command::INV, std::vector<uint8_t>(tip_hash.begin(), tip_hash.end()));
            }

            while (peer.Receive(msg)) {
                if (msg.command == command::GETDATA) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    peer.Send(command::BLOCK, chain_.back().Serialize());
                }
                else if (msg.command == command::GETBLOCKS) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    peer.Send(command::CHAIN, storage::serialize_chain(chain_));
                }
                else if (msg.command == command::TX) {
                    size_t offset = 0;
                    transaction::Transaction tx = transaction::Transaction::Deserialize(msg.payload, offset);
                    AcceptTransaction(tx, prefix);
                }
                else {
                    Logger::instance().error("{}Unexpected command '{}'. Dropping connection.", prefix, msg.command);
                    break;
                }
            }
        }

        // Same acceptance rule as run_receive_tx: valid signature(s) against our current UTXO
        // view, and room left in the mempool.
        void AcceptTransaction(const transaction::Transaction& tx, const std::string& prefix) {
            std::lock_guard<std::mutex> lock(mutex_);
            transaction::UtxoSet utxo_set = build_utxo_set(chain_);
            int64_t fee = tx.IsCoinbase() ? -1 : validate_and_get_fee(tx, utxo_set);
            if (fee < 0) {
                Logger::instance().error("{}Rejected transaction - signature/UTXO/value check failed.", prefix);
                return;
            }
            std::vector<transaction::Transaction> mempool = load_mempool_or_empty(chain_path_ + ".mempool");
            if (mempool.size() >= MAX_MEMPOOL_SIZE) {
                Logger::instance().error("{}Rejected transaction - mempool is full ({} pending).", prefix, mempool.size());
                return;
            }
            mempool.push_back(tx);
            storage::save_mempool(mempool, chain_path_ + ".mempool");
            Logger::instance().info("{}Accepted transaction ({} satoshi fee) into mempool ({} pending).", prefix, fee, mempool.size());
        }

        // Every DAEMON_CYCLE_SECONDS: pull the mempool, mine one block on top of our tip - same
        // logic as run_listener, just repeated forever instead of once.
        [[noreturn]] void MiningLoop() {
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(DAEMON_CYCLE_SECONDS));

                std::lock_guard<std::mutex> lock(mutex_);
                transaction::UtxoSet utxo_set = build_utxo_set(chain_);
                std::vector<transaction::Transaction> mempool = load_mempool_or_empty(chain_path_ + ".mempool");
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
                }

                block::CBlock new_block = build_next_block(extract_headers(chain_), chain_.back().GetHash(), wallet_.Address(), total_fees, extra_txs);
                chain_.push_back(new_block);
                storage::save_chain(chain_, chain_path_);
                storage::save_mempool({}, chain_path_ + ".mempool");
                Logger::instance().info("daemon(mine): Mined block. Chain height {}, {} tx(s) included.", chain_.size(), extra_txs.size() + 1);
            }
        }

        // Every DAEMON_CYCLE_SECONDS: connect out to each known peer and try to extend or reorg
        // onto their chain - the same request flow as run_connector, just repeated forever
        // against a shared in-memory chain_ instead of loading/saving a file each time, and
        // against every known peer instead of just one.
        [[noreturn]] void PeerPollLoop() {
            while (true) {
                for (const auto& addr : known_peers_) {
                    PollPeer(addr);
                }
                std::this_thread::sleep_for(std::chrono::seconds(DAEMON_CYCLE_SECONDS));
            }
        }

        void PollPeer(const PeerAddress& addr) {
            std::string prefix = "daemon(poll): ";

            socket_t sock;
            try {
                sock = connect_to(addr.host, addr.port);
            }
            catch (const std::exception&) {
                return; // peer unreachable this cycle, try again next time
            }
            Peer peer(sock);

            peer.Send(command::VERSION, build_version_payload());
            Message msg;
            if (!peer.Receive(msg) || msg.command != command::VERSION) {
                return;
            }
            peer.Send(command::VERACK, {});
            if (!peer.Receive(msg) || msg.command != command::VERACK) {
                return;
            }
            if (!peer.Receive(msg) || msg.command != command::INV) {
                return;
            }

            peer.Send(command::GETDATA, msg.payload);
            if (!peer.Receive(msg) || msg.command != command::BLOCK) {
                return;
            }

            size_t offset = 0;
            block::CBlock received_block = block::CBlock::Deserialize(msg.payload, offset);

            bool extended;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                extended = try_extend_chain(chain_, received_block);
                if (extended) {
                    storage::save_chain(chain_, chain_path_);
                }
            }
            if (extended) {
                Logger::instance().info("{}Extended chain from {}:{}. Height {}.", prefix, addr.host, addr.port, chain_.size());
                return;
            }

            if (!received_block.IsMerkleRootValid() || !proof_of_work::check_proof_of_work(received_block)) {
                return; // not even a validly-mined block, not worth asking for their whole chain
            }

            Logger::instance().info("{}{}:{}'s tip does not extend ours - requesting their full chain to compare.", prefix, addr.host, addr.port);
            peer.Send(command::GETBLOCKS, {});
            if (!peer.Receive(msg) || msg.command != command::CHAIN) {
                return;
            }
            std::vector<block::CBlock> peer_chain = storage::deserialize_chain(msg.payload);

            std::lock_guard<std::mutex> lock(mutex_);
            if (try_reorg(chain_, peer_chain)) {
                storage::save_chain(chain_, chain_path_);
                Logger::instance().info("{}Reorged onto {}:{}'s chain. Height {}.", prefix, addr.host, addr.port, chain_.size());
            }
            else {
                Logger::instance().info("{}Not switching to {}:{}'s chain (their height {}, ours {}).",
                    prefix, addr.host, addr.port, peer_chain.size(), chain_.size());
            }
        }

        uint16_t listen_port_;
        std::string chain_path_;
        std::vector<PeerAddress> known_peers_;
        wallet::Wallet wallet_;

        std::mutex mutex_; // guards chain_ - AcceptLoop's per-connection threads, MiningLoop and PeerPollLoop all touch it
        std::vector<block::CBlock> chain_;
    };
}

#endif
